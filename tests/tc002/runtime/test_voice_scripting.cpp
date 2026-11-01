#include "../../support.h"
#include "../../LogLines.h"
#include "FakeMicrophoneDaemon.h"

#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/voice/VoiceScripting.h"
#include "platform/tc002/voice/WavWriter.h"
#include "speech_samples.h"
#include "test_mp3pcm/vectors.h"

using namespace awtrix;
using namespace awtrix::test::speech;
using awtrix::test::check;
using awtrix::test::logAfter;
using awtrix::test::logMark;
using awtrix::test::require;
using tc002::StreamControl;
using tc002::StreamEvent;
using tc002::voice::VoiceScripting;

namespace {

std::string vadPath;
std::shared_ptr<awtrix::speech::ModelVoice> voice;

struct Speaker final : tc002::voice::VoiceSpeaker {
  bool owned = false, music = false, done = false, silent = true;
  std::vector<std::string> played;
  std::string holds;
  bool available() const override { return true; }
  posix::Hold claimVoiceChannel() override {
    if (owned) return {};
    owned = true;
    holds += "own ";
    return posix::Hold([this] {
      owned = false;
      holds += "-own ";
    });
  }
  posix::Hold holdMusic() override {
    music = true;
    holds += "music ";
    return posix::Hold([this] {
      music = false;
      holds += "-music ";
    });
  }
  bool quiet() const override { return silent; }
  bool play(const std::string& path) override {
    played.push_back(path);
    done = false;
    return true;
  }
  void stop() override { done = true; }
  bool finished() const override { return done; }
  bool heard() const override { return true; }
};

std::string recorder(const std::string& opts) {
  return "import voice\n"
         "class App\n"
         "var log\n"
         "def init() self.log = '' end\n"
         "def setup() voice.record(" + opts + ", def (c, s)\n"
         "  self.log += 'rec ' + str(s) + (c != nil ? ' ' + c.kind : '') + ';'\n"
         "  if c != nil\n"
         "    voice.play(c, def (ok) self.log += 'first ' + str(ok) + ';' end)\n"
         "    self.log += 'play ' + str(voice.play(c, def (ok) self.log += 'done ' + str(ok) + ';' end)) + ';'\n"
         "  end\n"
         "end) end\n"
         "def draw() end\n"
         "def check() return self.log + voice.state() end\n"
         "end\n"
         "return App()";
}

std::string app(const std::string& setup, const std::string& draw = {}) {
  return "import voice\n"
         "class App\n"
         "var log\n"
         "def init() self.log = '' end\n"
         "def note(label) return def (c, s)\n"
         "  self.log += label + ' ' + str(s) + (c != nil ? ' ' + c.kind : '') + ';'\n"
         "end end\n"
         "def setup() " + setup + " end\n"
         "def draw() " + draw + " end\n"
         "def check() return self.log + voice.state() end\n"
         "end\n"
         "return App()";
}

const char* const kCollector =
    "import voice\nclass App\nvar log, clips\ndef init() self.log = '' self.clips = [] end\n"
    "def go() voice.record({'from': 'now'}, def (c, s)\n"
    "  self.log += str(s) + ';'\n"
    "  if c != nil self.clips.push(c) self.go() end\n"
    "end) end\n"
    "def setup() self.go() end\n"
    "def draw() if voice.level() > 0 voice.stop() end end\n"
    "def check() return self.log + voice.state() + ' ' + str(voice.level()) end\nend\nreturn App()";

std::string assistant(const std::string& origin) {
  return "import voice\nclass App\nvar log\ndef init() self.log = '' end\n"
         "def setup() voice.record({'from': 'now'}, def (c, s)\n"
         "  self.log += 'rec ' + str(s) + ';'\n"
         "  voice.send(c, '" + origin + "/stt', {'fields': {'model': 'whisper-1'}, 'headers': {'X-Key': 'k'},\n"
         "      'find': '\"text\":', 'keep': 14}, def (body, status)\n"
         "    self.log += 'stt ' + str(status) + ' ' + str(body) + ';'\n"
         "    voice.fetch('" + origin + "/tts', {'body': body}, def (answer, status)\n"
         "      self.log += 'tts ' + str(status) + ' ' + (answer != nil ? answer.kind : 'nil') + ';'\n"
         "      if answer != nil voice.play(answer, def (ok) self.log += 'done;' end) end\n"
         "    end)\n"
         "  end)\n"
         "end) end\n"
         "def draw() end\ndef check() return self.log + voice.state() end\nend\nreturn App()";
}

const char* const kListener =
    "import voice\nclass App\nvar log\ndef init() self.log = '' end\n"
    "def on_voice(trigger) self.log += trigger + ';' voice.record({}, def (c, s) self.log += 'rec ' + str(s) + ';' end) end\n"
    "def draw() pixel(0, 0, 0xFFFFFF) end\ndef check() return self.log + voice.state() end\nend\nreturn App()";

const char* const kBystander =
    "import voice\nclass App\nvar log\ndef init() self.log = '' end\n"
    "def on_voice(trigger) self.log += trigger + ';' end\n"
    "def draw() end\ndef check() return self.log + voice.state() end\nend\nreturn App()";

const std::string kAnswer = tc002::voice::wavHeader(160, 16000) + std::string(320, '\0');

std::string keeper(const std::string& draw) {
  return "import voice\nclass App\nvar log, clip\ndef init() self.log = '' end\n"
         "def setup() voice.record({'from': 'now'}, def (c, s) self.log += 'rec ' + str(s) + ';' self.clip = c end) end\n"
         "def draw() if self.clip == nil return end " + draw + " end\n"
         "def check() return self.log end\nend\nreturn App()";
}

const char* const kReplayer =
    "import voice\nclass App\nvar log\ndef init() self.log = '' end\n"
    "def setup() voice.record({'from': 'now'}, def (c, s)\n"
    "  for i : 1 .. 40 voice.play(c, def (ok) self.log += '.' end) end\n"
    "end) end\n"
    "def draw() end\ndef check() return self.log end\nend\nreturn App()";

struct Gate {
  std::atomic<bool> open{false}, entered{false};
  void pass() {
    entered = true;
    while (!open) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  bool reached() const {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) {
      if (entered) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
  }
};

struct Loopback {
  httplib::Server server;
  std::thread listener;
  std::string origin;
  void listen() {
    const int port = server.bind_to_any_port("127.0.0.1");
    require(port > 0, "a loopback server");
    origin = "http://127.0.0.1:" + std::to_string(port);
    listener = std::thread([this] { server.listen_after_bind(); });
    server.wait_until_ready();
  }
  void answerAfter(const std::string& path, Gate& gate) {
    server.Get(path, [&gate](const httplib::Request&, httplib::Response& response) {
      gate.pass();
      response.set_content(kAnswer, "audio/wav");
    });
  }
  ~Loopback() {
    server.stop();
    if (listener.joinable()) listener.join();
  }
};

std::string makeFolder() {
  char name[] = "/tmp/voice-scripting-test-XXXXXX";
  require(::mkdtemp(name) != nullptr, "a test folder");
  return name;
}

std::size_t files(const std::string& dir) {
  std::size_t count = 0;
  DIR* d = ::opendir(dir.c_str());
  if (!d) return 0;
  while (const dirent* entry = ::readdir(d)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    struct stat st {};
    const std::string path = dir + "/" + name;
    if (::stat(path.c_str(), &st) != 0) continue;
    count += S_ISDIR(st.st_mode) ? files(path) : 1;
  }
  ::closedir(d);
  return count;
}

std::string firstFile(const std::string& dir) {
  std::string found;
  DIR* d = ::opendir(dir.c_str());
  if (!d) return found;
  while (const dirent* entry = ::readdir(d)) {
    const std::string name = entry->d_name;
    struct stat st {};
    if (name == "." || name == ".." || ::stat((dir + "/" + name).c_str(), &st) != 0) continue;
    found = S_ISDIR(st.st_mode) ? firstFile(dir + "/" + name) : name;
    if (!found.empty()) break;
  }
  ::closedir(d);
  return found;
}

struct Folder {
  const std::string path = makeFolder();
  ~Folder() { ::rmdir(path.c_str()); }
};

struct Rig {
  test::FakeMicrophoneDaemon daemon;
  std::vector<tc002::MicrophoneMark> starts;
  int64_t now = 1000;
  bool assistantBusy = false;
  std::atomic<uint64_t> available{uint64_t{1} << 40};
  Folder folder;
  const std::string clipDirectory;
  const std::string model;
  Speaker speaker;
  tc002::MicrophoneHub hub{daemon.link(), [](const StreamEvent&, int64_t) {}};
  tc002::MicrophoneHub::Consumer microphone = hub.attach([this](const StreamEvent& e, int64_t at) {
    module->receive(e, at);
  });
  std::unique_ptr<VoiceScripting> module = std::make_unique<VoiceScripting>(VoiceScripting::Services{
      microphone.port(),
      [this](tc002::MicrophoneMark mark) {
        starts.push_back(mark);
        microphone.startFrom(mark);
      },
      [this](tc002::MicrophoneMark mark) { return hub.live(mark); },
      &speaker,
      [this] { return assistantBusy; },
      [](const std::string& app) { return app == "S"; },
      [this] { return now; },
      clipDirectory,
      [this](uint64_t& bytes) {
        bytes = available;
        return true;
      },
      model});
  AppRegistry registry;
  script::ScriptServices services;
  script::ScriptHost host{registry, services, nullptr, nullptr};
  std::unique_ptr<script::ExtensionHost> extensions;
  Canvas canvas{32, 8};

  explicit Rig(const std::string& clipSubfolder = {}, std::string modelPath = vadPath)
      : clipDirectory(folder.path + clipSubfolder), model(std::move(modelPath)) {
    hub.tick(now);
    extensions = std::make_unique<script::ExtensionHost>(host, std::vector<script::ScriptExtension*>{module.get()});
  }
  bool install(const std::string& name, const std::string& source) {
    return host.set(name, source) && host.errorOf(name).empty();
  }
  std::string log(const std::string& name = "S") {
    std::string out;
    if (auto* app = static_cast<script::ScriptApp*>(registry.find(name))) app->callCheckForTest(out);
    return out;
  }
  void frame() {
    hub.tick(now);
    module->update(now);
    RenderCtx ctx;
    host.tick(ctx, "S");
  }
  bool until(const std::string& text) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < end) {
      frame();
      if (log().find(text) != std::string::npos) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
  }
  void draw() {
    canvas.clear();
    RenderCtx ctx;
    if (auto* app = registry.find("S")) app->render(canvas, ctx);
  }
  void upstream(StreamEvent::Kind kind) { hub.receive(daemon.event(kind), now); }
  void stream() {
    frame();
    hub.tick(now);
    upstream(StreamEvent::Kind::Support);
    hub.tick(now);
    upstream(StreamEvent::Kind::Started);
    frame();
  }
  template <class AfterBlock>
  void blocks(const std::vector<int16_t>& pcm, AfterBlock after) {
    for (std::vector<int16_t>& block : test::FakeMicrophoneDaemon::blocks(pcm)) {
      now += 30;
      hub.receive(daemon.audio(std::move(block), now), now);
      after();
    }
  }
  void play(const std::vector<int16_t>& pcm) { blocks(pcm, [this] { frame(); }); }
  void hear(const std::vector<int16_t>& pcm) { blocks(pcm, [] {}); }
  void sentence() { play(join({noise(300, -60, 1), say(*voice, "Turn on the light."), noise(2500, -60, 2)})); }
};

void recordsAndPlays() {
  Rig rig;
  require(rig.install("S", recorder("{'from': 'now'}")), "a script imports voice");
  check(rig.module->holds("S") && rig.speaker.owned && rig.log() == "recording",
        "recording holds the app, owns the speaker and shows in voice.state()");
  rig.stream();
  rig.sentence();
  check(rig.log() == "rec 200 wav;play true;first false;playing",
        "the sentence arrives as a WAV clip, and a second play replaces the first: " + rig.log());
  check(rig.speaker.played.size() == 2 && rig.speaker.owned && rig.module->holds("S"),
        "playing owns the speaker and holds the app");
  check(files(rig.folder.path) == 1, "the clip is one file");
  rig.speaker.done = true;
  rig.frame();
  rig.frame();
  check(rig.log() == "rec 200 wav;play true;first false;done true;idle", "done(true) after the clip: " + rig.log());
  check(!rig.speaker.owned && !rig.module->holds("S") && !rig.module->busy(), "then everything is free again");
  rig.module->hidden(*rig.extensions, "S");
  check(files(rig.folder.path) == 0, "a hidden app loses its clips");
}

void refusals() {
  Rig rig;
  rig.assistantBusy = true;
  require(rig.install("S", recorder("{'from': 'now'}")), "the script loads");
  check(rig.log() == "rec -2;idle", "Home Assistant Voice keeps the module busy");
  posix::Hold taken = rig.speaker.claimVoiceChannel();
  rig.module->hidden(*rig.extensions, "S");
  check(rig.speaker.owned, "a hidden app leaves Voice's hold on the speaker alone");
  rig.assistantBusy = false;
  require(rig.install("S", recorder("{'from': 'now'}")), "the script loads while Voice holds the voice channel");
  check(rig.log() == "rec -2;idle" && !rig.module->busy() && files(rig.folder.path) == 0,
        "a recording finds the voice channel taken and keeps no clip");
  taken.reset();
  for (const char* options : {"{'silence': 100}", "{'wait': 15.5}", "{'max': 0.5}", "{'max': 'long'}",
                              "{'from': 'later'}", "{'from': 1}"}) {
    require(rig.install("S", recorder(options)), std::string("the script loads with ") + options);
    check(rig.log() == "rec -6;idle", std::string("options out of range or of the wrong type are BAD: ") + options);
  }
  require(rig.install("T", recorder("{}")), "a second script loads");
  check(rig.log("T") == "rec -2;idle", "an app not on the panel cannot record");
  require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts");
  rig.frame();
  check(rig.daemon.last().operation == StreamControl::Probe, "the recording probes the microphone");
  rig.host.remove("S");
  check(!rig.module->busy() && !rig.speaker.owned && files(rig.folder.path) == 0,
        "removing the app while its recording probes ends it and deletes its clip");
  rig.upstream(StreamEvent::Kind::Support);
  check(!rig.hub.active(), "the hub lets the probe of the removed app go");
  require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts again");
  rig.frame();
  rig.upstream(StreamEvent::Kind::Support);
  check(rig.module->busy() && rig.module->starting() && rig.daemon.last().operation == StreamControl::Start,
        "the recording waits for the stream");
  rig.host.remove("S");
  check(!rig.module->busy() && !rig.speaker.owned && rig.daemon.last().operation == StreamControl::Stop &&
            files(rig.folder.path) == 0,
        "removing the app ends its recording and deletes its clip");
}

void failedRecording() {
  Rig rig;
  require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts");
  rig.stream();
  rig.play(noise(300, -60, 15));
  check(rig.speaker.owned && files(rig.folder.path) == 1 && rig.log() == "recording",
        "the recording holds the voice channel and writes its clip: " + rig.log());
  const uint32_t mark = logMark();
  rig.hub.receive(rig.daemon.end("microphone UART frame lost or corrupt", rig.now), rig.now);
  rig.frame();
  check(rig.log() == "rec -2;idle" && files(rig.folder.path) == 0 && !rig.speaker.owned && !rig.module->busy(),
        "a recording that fails is BUSY and gives back its clip and the voice channel: " + rig.log());
  check(logAfter(mark).find("voice: recording failed: microphone UART frame lost or corrupt") != std::string::npos,
        "and the log names the reason");
}

void noClipFolder() {
  Rig rig("/missing");
  require(rig.install("S", recorder("{'from': 'now'}")), "the script loads");
  check(rig.log() == "rec -2;idle", "without its folder the clip store keeps the module BUSY: " + rig.log());
}

void silence() {
  Rig rig;
  require(rig.install("S", recorder("{'from': 'now', 'wait': 1}")), "the script loads");
  rig.stream();
  rig.play(noise(1500, -60, 3));
  check(rig.log() == "rec -1;idle", "nothing said is SILENCE");
  check(files(rig.folder.path) == 0, "a silent recording leaves no file");
}

void cancelThenRecord() {
  Rig rig;
  require(rig.install("S", app("voice.record({'from': 'now'}, self.note('first')) voice.cancel() "
                               "voice.record({'from': 'now'}, self.note('second'))")),
          "a script cancels a recording and records again in one turn");
  rig.stream();
  check(rig.log() == "first -3;recording", "the cancelled recording reaches its own callback: " + rig.log());
  rig.sentence();
  check(rig.log() == "first -3;second 200 wav;idle", "the next recording reaches the next callback: " + rig.log());
}

void stopThenRecord() {
  Rig rig;
  require(rig.install("S", app("voice.record({'from': 'now'}, self.note('first'))",
                               "voice.stop() voice.record({'from': 'now'}, self.note('second'))")),
          "the script loads");
  rig.stream();
  rig.play(noise(300, -60, 6));
  rig.draw();
  check(rig.log() == "second -2;recording",
        "a recording stopped in the same turn still holds the microphone: " + rig.log());
  rig.frame();
  check(rig.log() == "second -2;first 200 wav;idle", "the stopped recording reaches its callback: " + rig.log());
}

void waitsForQuietSpeaker() {
  {
    Rig rig;
    rig.speaker.silent = false;
    require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts while music plays");
    rig.frame();
    check(rig.daemon.controls.empty() && rig.module->starting() && rig.module->busy() && rig.module->holds("S") &&
              rig.speaker.owned && rig.log() == "recording",
          "the recording waits for the speaker to go quiet");
    rig.speaker.silent = true;
    rig.frame();
    check(rig.daemon.controls.size() == 1 && rig.daemon.last().operation == StreamControl::Probe,
          "it starts once the speaker is quiet");
  }
  {
    Rig rig;
    rig.speaker.silent = false;
    require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts while music plays");
    rig.now += 999;
    rig.frame();
    check(rig.daemon.controls.empty(), "it waits up to a second");
    rig.now += 1;
    rig.frame();
    check(rig.daemon.controls.size() == 1 && rig.daemon.last().operation == StreamControl::Probe,
          "then it starts while the speaker still plays");
  }
  {
    Rig rig;
    rig.speaker.silent = false;
    require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts while music plays");
    rig.module->stop("S");
    rig.frame();
    check(rig.log() == "rec -1;idle" && rig.daemon.controls.empty() && !rig.speaker.owned && files(rig.folder.path) == 0,
          "stop() while it waits is SILENCE: " + rig.log());
    require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts again");
    rig.module->cancel("S");
    rig.frame();
    check(rig.log() == "rec -3;idle" && rig.daemon.controls.empty() && !rig.speaker.owned && files(rig.folder.path) == 0,
          "cancel() while it waits is CANCELLED: " + rig.log());
  }
}

void fromTheTrigger() {
  Rig rig;
  const tc002::MicrophoneHub::Consumer other = rig.hub.attach([](const StreamEvent&, int64_t) {});
  other.port()({1, StreamControl::Probe});
  other.port()({1, StreamControl::Start});
  rig.stream();
  rig.hub.hold(true);
  rig.play(noise(300, -60, 4));
  const tc002::MicrophoneMark mark = rig.hub.mark();
  rig.module->trigger("S", "wake", mark);
  const auto words = say(*voice, "Turn on the light.");
  const auto at = [&words](std::ptrdiff_t ms) {
    return words.begin() + ms * static_cast<std::ptrdiff_t>(kSamplesPerMs);
  };
  rig.play({words.begin(), at(300)});
  rig.speaker.silent = false;
  require(rig.install("S", app("voice.record({}, def (c, s)\n"
                               "  self.note('rec')(c, s)\n"
                               "  voice.record({}, self.note('again'))\n"
                               "end)")),
          "a script records from the trigger while the speaker plays");
  check(rig.starts.size() == 1 && rig.starts[0].epoch == mark.epoch && rig.starts[0].sample == mark.sample &&
            rig.module->starting(),
        "the recording starts at once at the trigger's mark, without waiting for a quiet speaker");
  rig.play(join({{at(300), words.end()}, noise(2500, -60, 5)}));
  check(rig.log().rfind("rec 200 wav;", 0) == 0, "the recording from the trigger holds the sentence: " + rig.log());
  check(rig.starts.size() == 2 && rig.starts[1].epoch == 0,
        "the mark serves one recording: the next starts now, once it waited for the speaker");
}

void markEndsWithTheInteraction() {
  Rig rig;
  const tc002::MicrophoneHub::Consumer other = rig.hub.attach([](const StreamEvent&, int64_t) {});
  other.port()({1, StreamControl::Probe});
  other.port()({1, StreamControl::Start});
  rig.stream();
  rig.play(noise(300, -60, 4));
  require(rig.install("S", kBystander), "an app whose on_voice does not record");
  rig.frame();
  rig.module->trigger("S", "wake", rig.hub.mark());
  rig.draw();
  rig.frame();
  check(!rig.module->triggered() && rig.log() == "wake;idle", "the trigger reached the app: " + rig.log());
  rig.module->dropTrigger();
  rig.speaker.silent = false;
  check(rig.module->record("S", {800, 5000, 15000}, true) > 0 && rig.starts.empty() && rig.module->starting(),
        "once the interaction ended, a recording from the trigger has no mark and waits for a quiet speaker");
}

void staleMark() {
  Rig rig;
  const tc002::MicrophoneHub::Consumer other = rig.hub.attach([](const StreamEvent&, int64_t) {});
  other.port()({1, StreamControl::Probe});
  other.port()({1, StreamControl::Start});
  rig.stream();
  rig.play(noise(300, -60, 4));
  const tc002::MicrophoneMark mark = rig.hub.mark();
  rig.upstream(StreamEvent::Kind::Ended);
  require(!rig.hub.live(mark), "the stream of the mark ended");
  require(rig.install("S", app("")), "the app loads");
  rig.module->trigger("S", "wake", mark);
  rig.speaker.silent = false;
  check(rig.module->record("S", {800, 5000, 15000}, true) > 0 && rig.starts.empty() && rig.module->starting(),
        "a mark of a stream that ended waits for a quiet speaker like a recording from now");
  rig.now += 1000;
  rig.frame();
  check(rig.starts.size() == 1 && rig.starts[0].epoch == 0, "and then starts from now");
}

void aGoneClipIsRefused() {
  Rig rig;
  require(rig.install("S", keeper("self.log += 'play ' + str(voice.play(self.clip, nil)) + ';'\n"
                                  "voice.send(self.clip, 'http://127.0.0.1:1/x', nil, "
                                  "def (b, s) self.log += 'send ' + str(s) + ';' end) self.clip = nil")),
          "a script keeps its recording");
  rig.stream();
  rig.sentence();
  require(rig.log() == "rec 200;", "the clip arrives: " + rig.log());
  rig.module->hidden(*rig.extensions, "S");
  rig.draw();
  check(rig.log() == "rec 200;play false;send -5;",
        "a clip the module let go of when the app was hidden does not play, and sending it is GONE: " + rig.log());
}

void voiceBusyRefusesPlay() {
  Rig rig;
  require(rig.install("S", keeper("self.log += 'play ' + str(voice.play(self.clip, nil)) + ';'")),
          "a script keeps its recording");
  rig.stream();
  rig.sentence();
  require(rig.log() == "rec 200;", "the clip arrives: " + rig.log());
  rig.assistantBusy = true;
  rig.draw();
  rig.assistantBusy = false;
  rig.draw();
  check(rig.log() == "rec 200;play false;play true;",
        "a clip does not play while Home Assistant Voice is busy, and plays after: " + rig.log());
}

std::ptrdiff_t played(const std::string& log) { return std::count(log.begin(), log.end(), '.'); }

void deliveriesPerTick() {
  Rig rig;
  require(rig.install("S", kReplayer), "a script that plays its clip forty times in one callback");
  rig.stream();
  rig.hear(join({noise(300, -60, 1), say(*voice, "Turn on the light."), noise(2500, -60, 2)}));
  rig.frame();
  check(played(rig.log()) == 0, "the plays replace each other in the callback: " + rig.log());
  rig.frame();
  check(played(rig.log()) == VoiceScripting::kPerTick,
        "one tick delivers at most " + std::to_string(VoiceScripting::kPerTick) + " events: " + rig.log());
  rig.frame();
  check(played(rig.log()) == 39, "the next tick delivers the rest: " + rig.log());
}

void anMp3TravelsAsMp3() {
  const std::string mp3(reinterpret_cast<const char*>(mp3vectors::ksine_mono_64k_mp3),
                        sizeof mp3vectors::ksine_mono_64k_mp3);
  Loopback loopback;
  loopback.server.Get("/answer.mp3", [&mp3](const httplib::Request&, httplib::Response& response) {
    response.set_content(mp3, "audio/mpeg");
  });
  loopback.server.Post("/stt", [](const httplib::Request& request, httplib::Response& response) {
    const auto file = request.get_file_value("file");
    response.set_content(file.filename + " " + file.content_type + " " + std::to_string(file.content.size()),
                         "text/plain");
  });
  loopback.listen();
  Rig rig;
  require(rig.install("S", app("voice.fetch('" + loopback.origin + "/answer.mp3', nil, def (c, s)\n"
                               "  self.note('fetch')(c, s)\n"
                               "  voice.send(c, '" + loopback.origin + "/stt', nil, def (body, s)\n"
                               "    self.log += 'send ' + str(s) + ' ' + str(body) + ';'\n"
                               "  end)\n"
                               "end)")),
          "a script downloads an MP3 and uploads it");
  check(rig.until("send "), "both transfers end: " + rig.log());
  check(rig.log() == "fetch 200 mp3;send 200 speech.mp3 audio/mpeg " + std::to_string(mp3.size()) + ";idle",
        "the clip keeps its kind, and the form names it speech.mp3 as audio/mpeg: " + rig.log());
}

void garbageCollection() {
  Rig rig;
  require(rig.install("S", app("voice.record({'from': 'now'}, self.note('rec'))")), "the script loads");
  rig.stream();
  rig.sentence();
  check(rig.log() == "rec 200 wav;idle", "the callback drops its clip: " + rig.log());
  require(rig.install("T", app("")), "installing another app runs the garbage collector");
  check(files(rig.folder.path) == 0, "the collector releases the dropped clip through its deinit");
}

void deliveryFails() {
  Rig rig;
  require(rig.install("S", app("voice.record({'from': 'now'}, self.note('rec'))")), "the script loads");
  rig.stream();
  rig.host.setRunningScripts({});
  rig.sentence();
  check(rig.log() == "idle" && files(rig.folder.path) == 0 && !rig.module->busy(),
        "a clip whose app no longer runs when it arrives is released: " + rig.log());
}

void fullAndLevel() {
  Rig rig;
  require(rig.install("S", kCollector), "the script loads");
  rig.stream();
  for (uint32_t block = 0; block < 60 && rig.log().find("-4") == std::string::npos; ++block) {
    rig.play(noise(30, -20, 10 + block));
    rig.draw();
  }
  check(rig.log() == "200;200;200;200;-4;idle 0",
        "voice.level() shows the recording, voice.stop() ends it, and a fifth clip is FULL: " + rig.log());
  check(files(rig.folder.path) == 4, "the app keeps its four clips");
}

void assistantRoundTrip() {
  Loopback loopback;
  loopback.server.Post("/stt", [](const httplib::Request& request, httplib::Response& response) {
    const bool good = request.get_header_value("X-Key") == "k" && request.has_file("file") &&
                      request.get_file_value("file").filename == "speech.wav" &&
                      request.get_file_value("file").content.compare(0, 4, "RIFF") == 0 &&
                      request.get_file_value("model").content == "whisper-1";
    response.set_content(good ? "{\"text\":\"hello\"}" : "{}", "application/json");
  });
  loopback.server.Post("/tts", [](const httplib::Request& request, httplib::Response& response) {
    if (request.body == "\"text\":\"hello\"")
      response.set_content(kAnswer, "audio/wav");
    else
      response.status = 400;
  });
  loopback.listen();
  Rig rig;
  require(rig.install("S", assistant(loopback.origin)), "the assistant loads");
  rig.stream();
  rig.sentence();
  check(rig.until("tts 200 wav;"), "record, send and fetch run in a row: " + rig.log());
  check(rig.log() == "rec 200;stt 200 \"text\":\"hello\";tts 200 wav;playing",
        "the kept answer reaches the script and goes on to the next request: " + rig.log());
  check(rig.module->holds("S") && rig.speaker.played.size() == 1, "the answer plays and holds the app");
  rig.speaker.done = true;
  check(rig.until("done;idle"), "it ends: " + rig.log());
  check(rig.speaker.holds == "own music -own own -music -own " && !rig.module->busy(),
        "the module owns the voice channel while it records and plays and holds the music in between: " +
            rig.speaker.holds);
}

void sendOptions() {
  Gate gate;
  Loopback loopback;
  loopback.server.Get("/wav", [](const httplib::Request&, httplib::Response& response) {
    response.set_content(kAnswer, "audio/wav");
  });
  loopback.server.Post("/raw", [&gate](const httplib::Request& request, httplib::Response& response) {
    gate.pass();
    response.set_content(request.get_header_value("Content-Type") + " " + std::to_string(request.body.size()),
                         "text/plain");
  });
  loopback.listen();
  const std::string raw = "'" + loopback.origin + "/raw'";
  Rig rig;
  require(rig.install("S", "import voice\nclass App\nvar log\ndef init() self.log = '' end\n"
                           "def answer(label) return def (body, s)\n"
                           "  self.log += label + ' ' + str(s) + ' ' + str(body) + ';'\n"
                           "end end\n"
                           "def setup() voice.fetch('" + loopback.origin + "/wav', nil, def (c, s)\n"
                           "  var sixteen = {} for i : 1 .. 16 sixteen[str(i)] = 'x' end\n"
                           "  var seventeen = {} for i : 0 .. 16 seventeen[str(i)] = 'x' end\n"
                           "  voice.send(c, " + raw + ", {'as': 'raw'}, self.answer('raw'))\n"
                           "  voice.send(c, " + raw + ", {'as': 'raw', 'headers': {'Content-Type': 'audio/l16'}},\n"
                           "      self.answer('typed'))\n"
                           "  voice.send(c, " + raw + ", {'as': 'raw'}, self.answer('third'))\n"
                           "  voice.send(c, " + raw + ", {'as': 'json'}, self.answer('as'))\n"
                           "  voice.send(c, " + raw + ", {'fields': {'a=b': 'c'}}, self.answer('name'))\n"
                           "  voice.send(c, " + raw + ", {'fields': ['x']}, self.answer('list'))\n"
                           "  voice.send(c, " + raw + ", {'keep': -1}, self.answer('keep'))\n"
                           "  voice.send(c, " + raw + ", {'fields': sixteen}, self.answer('sixteen'))\n"
                           "  voice.send(c, " + raw + ", {'fields': seventeen}, self.answer('seventeen'))\n"
                           "  voice.send(c, " + raw + ", {'headers': {'content-type': 'text/plain'}},\n"
                           "      self.answer('formtype'))\n"
                           "end) end\n"
                           "def draw() end\ndef check() return self.log end\nend\nreturn App()"),
          "the script loads");
  check(rig.until("formtype "), "the uploads start: " + rig.log());
  gate.open = true;
  check(rig.until("typed "), "the uploads end: " + rig.log());
  check(rig.log() ==
            "third -2 nil;as -6 nil;name -6 nil;list -6 nil;keep -6 nil;sixteen -2 nil;seventeen -6 nil;"
            "formtype -6 nil;raw 200 audio/wav 364;typed 200 audio/l16 364;",
        "a raw upload is the clip with its type unless a header names one, a third upload is BUSY and bad "
        "options, also a 17th form field or a Content-Type for a form, are BAD: " + rig.log());
}

void refusedRequests() {
  Rig rig;
  require(rig.install("S", "import voice\nclass App\nvar log\ndef init() self.log = '' end\n"
                           "def setup() voice.fetch('ftp://x/y', {}, def (c, s) self.log += str(s) + ';' end)\n"
                           "  voice.fetch('http://127.0.0.1:1/a', {'method': 'BREW'}, def (c, s) self.log += str(s) + ';' end)\n"
                           "  voice.fetch('http://127.0.0.1:1/a', {'headers': {'A\\nB': 'x'}}, def (c, s) self.log += str(s) + ';' end)\n"
                           "end\ndef draw() end\ndef check() return self.log end\nend\nreturn App()"),
          "the script loads");
  check(rig.log() == "-6;-6;-6;", "a bad URL, method or header is BAD at once: " + rig.log());
}

void fetchStatuses() {
  Loopback loopback;
  loopback.server.Get("/text", [](const httplib::Request&, httplib::Response& response) {
    response.set_content("hello", "audio/mpeg");
  });
  loopback.server.Get("/missing", [](const httplib::Request&, httplib::Response& response) { response.status = 404; });
  loopback.server.Get("/wav", [](const httplib::Request&, httplib::Response& response) {
    response.set_content(kAnswer, "audio/wav");
  });
  loopback.server.Get("/moved", [](const httplib::Request&, httplib::Response& response) {
    response.set_redirect("/wav");
  });
  loopback.server.Post("/moved", [](const httplib::Request&, httplib::Response& response) {
    response.set_redirect("/wav");
  });
  loopback.listen();
  const std::string at = "'" + loopback.origin;
  Rig rig;
  require(rig.install("S", app("voice.fetch(" + at + "/text', nil, def (c, s)\n"
                               "  self.note('text')(c, s)\n"
                               "  voice.fetch(" + at + "/missing', nil, def (c, s)\n"
                               "    self.note('missing')(c, s)\n"
                               "    voice.fetch('http://127.0.0.1:1/x', nil, def (c, s)\n"
                               "      self.note('unreachable')(c, s)\n"
                               "      voice.fetch(" + at + "/moved', nil, def (c, s)\n"
                               "        self.note('get')(c, s)\n"
                               "        voice.fetch(" + at + "/moved', {'body': 'x'}, self.note('post'))\n"
                               "      end)\n"
                               "    end)\n"
                               "  end)\n"
                               "end)")),
          "the script loads");
  check(rig.until("post "), "the downloads end: " + rig.log());
  check(rig.log() == "text -6;missing 404;unreachable 0;get 200 wav;post 302;idle",
        "200 without audio is BAD, an error status stays, no server is 0, and only a GET follows a redirect: " +
            rig.log());
}

void forgetDuringTransfer() {
  Gate gate;
  Loopback loopback;
  loopback.answerAfter("/slow", gate);
  loopback.listen();
  {
    Rig rig;
    const std::string slow = loopback.origin + "/slow";
    require(rig.install("S", app("voice.fetch('" + slow + "', nil, self.note('fetch'))")), "the script loads");
    check(rig.speaker.music && rig.module->holds("S"), "a running download holds the music and the app");
    rig.host.remove("S");
    check(!rig.speaker.music && !rig.module->holds("S"), "removing the app lets the music go");
    check(rig.module->fetch("S", slow, "GET", "", "") > 0 && rig.module->fetch("S", slow, "GET", "", "") > 0,
          "and the clip its download reserved: two more downloads fit in its 4 MB");
  }
  gate.open = true;
}

void cancelReachesPolledResults() {
  Gate gate, later;
  Loopback loopback;
  loopback.answerAfter("/gate", gate);
  loopback.answerAfter("/later", later);
  loopback.listen();
  const std::string gated = "voice.fetch('" + loopback.origin + "/gate', nil, ";
  const auto finishDownloads = [&](Rig& rig) {
    gate.open = true;
    require(rig.module->fetch("T", loopback.origin + "/later", "GET", "", "") > 0, "a download of another app");
    require(later.reached(), "it reaches the server, so the downloads queued before it have ended");
  };
  {
    Rig rig;
    require(rig.install("S", app(gated + "self.note('fetch')) "
                                 "voice.record({'from': 'now'}, def (c, s) self.note('rec')(c, s) voice.cancel() end)")),
            "a script downloads and records");
    rig.stream();
    rig.hear(join({noise(300, -60, 1), say(*voice, "Turn on the light."), noise(2500, -60, 2)}));
    finishDownloads(rig);
    rig.frame();
    check(rig.log() == "rec 200 wav;fetch -3;idle",
          "voice.cancel() in the recording's callback also cancels a download polled in the same update: " +
              rig.log());
    check(files(rig.folder.path) == 2,
          "and releases the clip of that download; the recording's and the other app's download in progress stay");
  }
  later.open = true;
}

void cancelReachesTakenResults() {
  Gate gate, later;
  Loopback loopback;
  loopback.answerAfter("/gate", gate);
  loopback.answerAfter("/later", later);
  loopback.listen();
  {
    Rig rig;
    const std::string gated = "voice.fetch('" + loopback.origin + "/gate', nil, ";
    require(rig.install("S", app(gated + "def (c, s) self.note('first')(c, s) voice.cancel() end) " + gated +
                                 "self.note('second'))")),
            "a script starts two downloads");
    gate.open = true;
    require(rig.module->fetch("T", loopback.origin + "/later", "GET", "", "") > 0, "a download of another app");
    require(later.reached(), "it reaches the server, so both downloads have ended");
    rig.module->update(rig.now);
    check(rig.module->holds("S"), "results not yet delivered still hold the app");
    RenderCtx ctx;
    rig.host.tick(ctx, "S");
    check(rig.log() == "first 200 wav;second -3;idle",
          "voice.cancel() in one callback cancels a result the same tick was about to deliver: " + rig.log());
    check(files(rig.folder.path) == 2,
          "and releases its clip; the delivered one and the other app's download in progress stay");
  }
  later.open = true;
}

void transfersRunOn() {
  Gate gate;
  Loopback loopback;
  loopback.answerAfter("/slow", gate);
  loopback.listen();
  {
    Rig rig;
    const std::string slow = "voice.fetch('" + loopback.origin + "/slow', nil, ";
    require(rig.install("S", app(slow + "self.note('first')) " + slow + "self.note('second')) " + slow +
                                 "self.note('third'))")),
            "the script loads");
    check(rig.log() == "third -4;idle" && rig.module->holds("S"),
          "two downloads reserve the app's 4 MB, a third is FULL, and they hold the app: " + rig.log());
    rig.module->hidden(*rig.extensions, "S");
    rig.frame();
    check(rig.module->holds("S") && rig.log() == "third -4;idle", "hiding the app leaves its transfers running");
    rig.module->cancel("S");
    rig.frame();
    check(rig.log() == "third -4;first -3;second -3;idle" && !rig.module->holds("S"),
          "voice.cancel() ends them with CANCELLED: " + rig.log());
    check(rig.module->fetch("S", loopback.origin + "/slow", "GET", "", "") > 0,
          "and releases the clips they reserved");
  }
  gate.open = true;
}

void transfersHoldTheMusic() {
  Gate gate;
  Loopback loopback;
  loopback.answerAfter("/slow", gate);
  loopback.listen();
  const std::string fetcher = app("voice.fetch('" + loopback.origin + "/slow', nil, self.note('fetch'))");
  Rig rig;
  rig.assistantBusy = true;
  require(rig.install("S", fetcher), "the script loads");
  rig.frame();
  check(rig.speaker.music && !rig.speaker.owned && !rig.module->busy(),
        "a transfer holds the music and leaves the voice channel to Voice, which may be busy meanwhile");
  gate.open = true;
  check(rig.until("fetch 200 wav;"), "the download ends: " + rig.log());
  check(rig.speaker.holds == "music -music ",
        "its callback runs without the voice channel, and the music plays again after it: " + rig.speaker.holds);
}

void destructionLetsGo() {
  Gate gate;
  Loopback loopback;
  loopback.answerAfter("/slow", gate);
  loopback.listen();
  Rig rig;
  Speaker speaker;
  {
    const tc002::MicrophoneHub::Consumer consumer = rig.hub.attach([](const StreamEvent&, int64_t) {});
    VoiceScripting module(VoiceScripting::Services{consumer.port(),
                                                   [](tc002::MicrophoneMark) {},
                                                   [](tc002::MicrophoneMark) { return false; },
                                                   &speaker,
                                                   [] { return false; },
                                                   [](const std::string&) { return true; },
                                                   [&rig] { return rig.now; },
                                                   rig.clipDirectory,
                                                   [](uint64_t& bytes) {
                                                     bytes = uint64_t{1} << 40;
                                                     return true;
                                                   },
                                                   vadPath});
    check(module.record("S", {800, 5000, 15000}, false) > 0 &&
              module.fetch("S", loopback.origin + "/slow", "GET", "", "") > 0 && speaker.owned && speaker.music,
          "a recording owns the voice channel and a download holds the music");
  }
  check(!speaker.owned && !speaker.music, "a module that goes away lets go of both: " + speaker.holds);
  gate.open = true;
}

void memoryRoom() {
  Gate gate;
  Loopback loopback;
  loopback.answerAfter("/gate", gate);
  loopback.listen();
  {
    Rig rig;
    rig.available = kKeepFreeBytes + 1000;
    require(rig.install("S", recorder("{'from': 'now'}")), "a recording starts with little memory left");
    check(rig.log() == "rec -4;idle" && !rig.speaker.owned,
          "a recording whose clip does not fit beside the memory kept free is FULL: " + rig.log());
    rig.available = uint64_t{1} << 40;
    require(rig.install("S", app("voice.fetch('" + loopback.origin + "/gate', nil, self.note('fetch'))")),
            "a download starts");
    require(gate.reached(), "it reaches the server");
    rig.available = kKeepFreeBytes + 100;
    gate.open = true;
    check(rig.until("fetch "), "the download ends: " + rig.log());
    check(rig.log() == "fetch -4;idle" && files(rig.folder.path) == 0,
          "a download that runs out of memory is FULL and leaves no file: " + rig.log());
  }
}

void modelPerRecording() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("voice-scripting-model-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path source(vadPath), settings = fs::path(vadPath).replace_extension(".json");
  {
    Rig rig({}, (dir / source.filename()).string());
    const std::string recorder = app("voice.record({'from': 'now'}, self.note('rec'))");
    require(rig.install("S", recorder), "the module starts without its model, and a script records");
    check(rig.log() == "rec -2;idle" && !rig.speaker.owned,
          "without the voice activity model a recording is BUSY: " + rig.log());
    fs::copy_file(source, dir / source.filename());
    fs::copy_file(settings, dir / settings.filename());
    require(rig.install("S", recorder), "the model is there for the next recording");
    rig.stream();
    rig.sentence();
    check(rig.log() == "rec 200 wav;idle", "the recording reads it: " + rig.log());
    fs::remove_all(dir);
    require(rig.install("S", recorder), "the model is gone again");
    check(rig.log() == "rec -2;idle", "the module let go of it when the recording ended: " + rig.log());
  }
}

void releaseOnlyReady() {
  Gate gate;
  Loopback loopback;
  loopback.answerAfter("/gate", gate);
  loopback.listen();
  {
    Rig rig;
    require(rig.install("S", app("voice.fetch('" + loopback.origin + "/gate', nil, self.note('fetch'))")),
            "a download starts");
    require(gate.reached(), "it reaches the server");
    const std::string reserved = firstFile(rig.folder.path);
    require(!reserved.empty(), "the clip it fills has its file");
    rig.module->release("S", std::stoull(reserved));
    gate.open = true;
    check(rig.until("fetch "), "the download ends: " + rig.log());
    check(rig.log() == "fetch 200 wav;idle" && files(rig.folder.path) == 1,
          "a script cannot release the clip a download still fills, even with its id: " + rig.log());
  }
}

void onVoice() {
  Rig rig;
  require(rig.install("S", kListener), "the listener loads");
  rig.frame();
  rig.module->trigger("S", "wake", {});
  rig.frame();
  check(rig.module->triggered() && rig.log() == "idle", "nothing reaches the app before it draws");
  rig.module->beginFrame(*rig.extensions, "T");
  rig.frame();
  check(rig.module->triggered() && rig.log() == "idle", "another app's frame leaves the trigger waiting");
  rig.draw();
  check(rig.canvas.getPixel(0, 0) == 0xFFFFFFu, "the app's first frame after the trigger draws as the app");
  check(rig.module->triggered() && rig.log() == "idle", "on_voice waits for the script tick after that frame");
  rig.frame();
  check(!rig.module->triggered() && rig.log() == "wake;recording",
        "on_voice names the trigger on the next tick, and the app records: " + rig.log());
  rig.module->trigger("S", "api", {});
  rig.module->hidden(*rig.extensions, "S");
  check(!rig.module->triggered(), "hiding the app drops its trigger");
  rig.module->trigger("S", "knob", {});
  rig.host.remove("S");
  check(!rig.module->triggered(), "removing the app drops its trigger");
}

}

int main(int argc, char** argv) {
  require(argc == 3, "usage: tc002-voice-scripting-test <vad.tflite> <voice.atts>");
  vadPath = argv[1];
  std::string error;
  const auto model = awtrix::speech::SpeechModel::load(argv[2], error);
  require(model != nullptr, error);
  voice = awtrix::speech::ModelVoice::create(model, error);
  require(voice != nullptr, error);
  recordsAndPlays();
  refusals();
  failedRecording();
  noClipFolder();
  silence();
  cancelThenRecord();
  stopThenRecord();
  waitsForQuietSpeaker();
  fromTheTrigger();
  markEndsWithTheInteraction();
  staleMark();
  aGoneClipIsRefused();
  voiceBusyRefusesPlay();
  deliveriesPerTick();
  anMp3TravelsAsMp3();
  garbageCollection();
  deliveryFails();
  fullAndLevel();
  assistantRoundTrip();
  sendOptions();
  refusedRequests();
  fetchStatuses();
  forgetDuringTransfer();
  cancelReachesPolledResults();
  cancelReachesTakenResults();
  transfersRunOn();
  transfersHoldTheMusic();
  destructionLetsGo();
  memoryRoom();
  modelPerRecording();
  releaseOnlyReady();
  onVoice();
  return awtrix::test::finish("tc002-voice-scripting");
}
