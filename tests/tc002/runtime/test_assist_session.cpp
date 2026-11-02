#include "../../support.h"
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "platform/tc002/voice/AssistSession.h"
#include "platform/tc002/voice/StreamClock.h"
using namespace awtrix::tc002;
using namespace awtrix::tc002::voice;
constexpr auto check = awtrix::test::require;
struct Rig {
  std::vector<std::pair<std::string, bool>> sent;
  std::vector<StreamControl> controls;
  bool disconnected = false;
  std::string tts;
  AssistSession session{{[&](std::string s, bool b) {
                           sent.emplace_back(s, b);
                           return true;
                         },
                         [&] { disconnected = true; },
                         [&](StreamControl c) {
                           controls.push_back(c);
                           return true;
                         },
                         [&](const std::string& s) {
                           tts = s;
                           return true;
                         },
                         [] {}}};
  explicit Rig(std::string device = "") {
    connect(0, device);
    check(session.ready(), "pipeline validated");
    disconnected = false;
  }
  void connect(int64_t now, const std::string& device = "") {
    session.connecting({false, "http://ha:8123", "secret", "", device}, now);
    answer(now);
  }
  void answer(int64_t now) {
    session.receive({session.captureEpoch(), StreamEvent::Kind::Support, 0, 0, now, true, {}, {}}, now);
    session.text(R"({"type":"auth_required"})", now + 1);
    check(sent.back().first.find("secret") != std::string::npos,
          "authenticate on challenge");
    session.text(R"({"type":"auth_ok"})", now + 2);
    session.text(
        R"({"id":1,"type":"result","success":true,"result":{"preferred_pipeline":"p","pipelines":[{"id":"p","stt_engine":"stt","tts_engine":"tts"}]}})",
        now + 3);
  }
  void begin() { check(session.begin(100), "begin"); }
  void stt(bool acknowledge = true, int64_t now = 110) {
    session.text(
        R"({"id":2,"type":"event","event":{"type":"run-start","data":{"runner_data":{"stt_binary_handler_id":7}}}})",
        now);
    session.text(
        R"({"id":2,"type":"event","event":{"type":"stt-start","data":{}}})",
        now + 1);
    if (acknowledge)
      session.receive(
          {2, StreamEvent::Kind::Started, 0, 0, now + 2, false, {}, {}},
          now + 2);
  }
};
void assistantSetting() {
  char folder[] = "/tmp/voice-config-XXXXXX";
  check(::mkdtemp(folder) != nullptr, "a private folder");
  {
    ConfigStore store(folder);
    check(store.load() && store.get().assistant == "off" && store.assistantApp().empty(),
          "nothing answers on a fresh clock");
    check(store.publicJson().find("\"enabled\":false") != std::string::npos, "and enabled says so");
    ConfigStore::Rejection rejection;
    check(store.update(R"({"assistant":"app:Chat"})", rejection) && store.get().assistant == "app:Chat" &&
              store.assistantApp() == "Chat",
          "an app can answer");
    check(store.publicJson().find("\"assistant\":\"app:Chat\"") != std::string::npos, "the setting is public");
    for (const char* bad : {R"({"assistant":"app:"})", R"({"assistant":"app:a/b"})", R"({"assistant":"hass"})",
                            R"({"assistant":"app:next"})", R"({"assistant":1})"}) {
      rejection = {};
      check(!store.update(bad, rejection) && rejection.field == "assistant", std::string("refused: ") + bad);
    }
    Config off;
    Config chosen = store.get();
    chosen.wakeWord = !off.wakeWord;
    check(!onlyWakeChanged(off, chosen), "a change of the assistant is a change of the connection");
  }
  ConfigStore again(folder);
  check(again.load() && again.get().assistant == "app:Chat" && again.assistantApp() == "Chat",
        "the setting survives a restart");
  check(again.erase() && again.assistantApp().empty() && again.get().assistant == "off",
        "erasing the settings leaves no assistant");
  ::unlink((std::string(folder) + "/voice.json").c_str());
  ::rmdir(folder);
}
void savedFilesKeepTheirMeaning() {
  struct Shape {
    const char* file;
    const char* assistant;
  };
  const Shape shapes[] = {
      {R"({"enabled":false})", "off"},
      {R"({"enabled":true,"url":"http://ha:8123","token":"t"})", "ha"},
      {R"({"enabled":false,"assistant":"ha","url":"http://ha:8123","token":"t"})", "off"},
      {R"({"enabled":true,"assistant":"ha","url":"http://ha:8123","token":"t"})", "ha"},
      {R"({"enabled":false,"assistant":"app:Chat"})", "app:Chat"},
      {R"({"enabled":true,"assistant":"app:Chat"})", "app:Chat"},
      {R"({"assistant":"ha"})", "ha"},
      {R"({"assistant":"off"})", "off"},
      {R"({"url":"http://ha:8123"})", "off"},
  };
  char folder[] = "/tmp/voice-config-XXXXXX";
  check(::mkdtemp(folder) != nullptr, "a private folder");
  const std::string path = std::string(folder) + "/voice.json";
  for (const Shape& shape : shapes) {
    std::ofstream(path, std::ios::trunc) << shape.file;
    ::chmod(path.c_str(), 0600);
    ConfigStore store(folder);
    check(store.load() && store.get().assistant == shape.assistant, std::string("loaded: ") + shape.file);
  }
  std::ofstream(path, std::ios::trunc) << R"({"enabled":false,"assistant":"ha","url":"http://ha:8123","token":"t"})";
  ConfigStore store(folder);
  ConfigStore::Rejection rejection;
  check(store.load() && store.get().url == "http://ha:8123" && store.get().token == "t",
        "an assistant switched off keeps its address and token");
  check(store.update(R"({"pipeline":"p"})", rejection), "saving rewrites the file");
  std::string saved;
  std::getline(std::ifstream(path), saved);
  check(saved.find("enabled") == std::string::npos && saved.find("\"assistant\":\"off\"") != std::string::npos,
        "one stored field only: " + saved);
  ConfigStore again(folder);
  check(again.load() && again.get().assistant == "off", "and it reads back the same");
  ::unlink(path.c_str());
  ::rmdir(folder);
}
void enabledFollowsTheAssistant() {
  char folder[] = "/tmp/voice-config-XXXXXX";
  check(::mkdtemp(folder) != nullptr, "a private folder");
  ConfigStore store(folder);
  ConfigStore::Rejection rejection;
  const auto write = [&](const char* json) {
    rejection = {};
    return store.update(json, rejection);
  };
  const auto reads = [&](bool enabled) {
    return store.publicJson().find(enabled ? "\"enabled\":true" : "\"enabled\":false") != std::string::npos;
  };
  check(store.load() && write(R"({"url":"http://ha:8123","token":"t"})") && store.get().assistant == "off" && reads(false),
        "an address and a token switch nothing on");
  check(write(R"({"enabled":true})") && store.get().assistant == "ha" && reads(true), "enabled true picks Home Assistant");
  check(write(R"({"enabled":false})") && store.get().assistant == "off" && reads(false), "enabled false picks off");
  check(store.get().url == "http://ha:8123" && store.get().token == "t", "off keeps address and token");
  check(write(R"({"assistant":"app:Chat"})") && reads(false), "with an app as the target enabled reads false");
  check(write(R"({"enabled":true})") && store.get().assistant == "ha" && store.assistantApp().empty(),
        "enabled true replaces the app");
  check(write(R"({"assistant":"app:Chat"})") && write(R"({"enabled":false})") && store.get().assistant == "off",
        "enabled false replaces the app");
  check(write(R"({"enabled":true,"assistant":"off"})") && store.get().assistant == "off", "assistant wins over true");
  check(write(R"({"enabled":false,"assistant":"ha"})") && store.get().assistant == "ha", "assistant wins over false");
  check(write(R"({"enabled":false,"assistant":"app:Chat"})") && store.get().assistant == "app:Chat",
        "assistant wins for an app");
  check(!write(R"({"enabled":"yes"})") && rejection.field == "enabled", "enabled is true or false");
  check(write(R"({"assistant":"ha","clearToken":true})") && store.get().assistant == "ha" && store.get().token.empty(),
        "removing the token changes no target");
  check(write(R"({"enabled":true})") && store.get().assistant == "ha", "the target needs no token");
  check(write(R"({"assistant":"off"})") && store.get().assistant == "off", "off is a valid target");
  ::unlink((std::string(folder) + "/voice.json").c_str());
  ::rmdir(folder);
}
void aBlockingReasonOutlivesTheRetry() {
  const Config config{false, "http://ha:8123", "secret", "", ""};
  {
    Rig r;
    r.session.lost(5);
    check(r.session.error() == "connectionLost" && r.session.lastError().empty(), "a lost idle connection blocks");
    r.session.connecting(config, 6);
    check(r.session.state() == AssistSession::State::Connecting && r.session.error() == "connectionLost",
          "the reason stays while the clock tries again");
    r.answer(7);
    check(r.session.ready() && r.session.error().empty(), "and goes once it is connected");
  }
  {
    Rig r;
    r.session.connecting(config, 10);
    r.session.text(R"({"type":"auth_invalid"})", 11);
    check(r.session.state() == AssistSession::State::Error && r.session.error() == "tokenRejected", "a refused token");
    r.session.connecting(config, 10011);
    check(r.session.error() == "tokenRejected", "stays named between two tries");
    r.session.cancel();
    check(r.session.error().empty(), "a new setting starts clean");
  }
  {
    Rig r;
    r.session.connecting(config, 1000);
    r.session.tick(11000);
    check(r.session.error() == "connectionLost" && r.session.lastError().empty(),
          "no answer while connecting is no connection, not a slow request");
  }
}
void aFailedRequestLeavesTheClockReady() {
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.receive({2, StreamEvent::Kind::Audio, 24, 0, 120, false, std::vector<int16_t>(24), {}}, 120);
    check(r.session.state() == AssistSession::State::Error && r.session.error().empty() &&
              r.session.lastError() == "microphoneFailed",
          "a request that fails names itself without blocking the assistant");
    r.connect(200);
    check(r.session.ready() && r.session.error().empty() && r.session.lastError() == "microphoneFailed",
          "connected again, the reason of the last try stays");
    check(r.session.begin(300) && r.session.lastError().empty(), "until the next request");
  }
  {
    Rig r;
    r.begin();
    r.session.tick(5100);
    check(r.session.lastError() == "timeout" && r.session.error().empty(), "a slow request is a timeout");
    r.connect(5200);
    r.session.tick(305099);
    check(r.session.lastError() == "timeout", "the reason stays for five minutes");
    r.session.tick(305100);
    check(r.session.lastError().empty(), "and then goes");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.text(R"({"id":2,"type":"event","event":{"type":"error","data":{"code":"stt-no-text-recognized"}}})", 120);
    check(r.session.lastError() == "nothingUnderstood", "nothing understood is the last try's reason");
    r.session.cancel();
    check(r.session.lastError().empty(), "a new setting forgets it");
  }
}
int main() {
  {
    int64_t local = 0;
    check(localizeCaptureTime(90000000, 90000030, 1030, local) && local == 1000,
          "daemon boot clock maps into young runtime clock by transport age");
    check(localizeCaptureTime(90000000, 90000030, 500030, local) &&
              local == 500000,
          "clock mapping remains correct after runtime restart");
    check(!localizeCaptureTime(90000000, 90000251, 1030, local),
          "clock translation does not hide stale audio");
    check(!localizeCaptureTime(90000031, 90000030, 1030, local),
          "future host capture rejected");
  }
  {
    Rig r;
    check(r.session.begin(100), "start while MCU acknowledgement pending");
    r.stt(false);
    check(r.session.state() == AssistSession::State::Starting,
          "listening indicator waits for actual MCU capture");
    r.session.receive({2, StreamEvent::Kind::Started, 0, 0, 120, false, {}, {}},
                      120);
    check(r.session.state() == AssistSession::State::Listening,
          "listening indicator starts after both sides are ready");
  }
  {
    char directory[] = "/tmp/awtrix-voice-config-XXXXXX";
    check(::mkdtemp(directory) != nullptr, "private test directory");
    ConfigStore store(directory);
    ConfigStore::Rejection error;
    check(store.load(), "new private store");
    check(store.update(
              R"({"url":"http://ha:8123","token":"secret","enabled":true})",
              error),
          "save config");
    check(store.publicJson().find("secret") == std::string::npos,
          "token never returned");
    struct stat info {};
    check(
        ::stat((std::string(directory) + "/voice.json").c_str(), &info) == 0 &&
            (info.st_mode & 0777) == 0600,
        "token file private");
    check(!store.update(R"({"url":"http://attacker:8123"})", error) &&
              error.field == "token",
          "retained token cannot change origin");
    check(!store.update(R"({"url":"http://ha:8123/lovelace"})", error) &&
              error.field == "url",
          "an address with a path names the url field");
    check(store.update(R"({"pipeline":"p1"})", error) &&
              store.get().token == "secret",
          "ordinary edits retain token");
    check(store.update(R"({"device":"0123456789abcdef0123456789abcdef"})",
                       error) &&
              store.publicJson().find("0123456789abcdef") != std::string::npos,
          "device saved and shown");
    check(!store.update(R"({"device":"../x"})", error) &&
              error.field == "device",
          "device is a plain identifier");
    check(!store.get().wakeWord && store.update(R"({"wakeWord":true})", error) && store.get().wakeWord &&
              store.publicJson().find("\"wakeWord\":true") != std::string::npos,
          "the wake word is off until switched on");
    check(!store.update(R"({"wakeWord":"yes"})", error) && error.field == "wakeWord",
          "the wake word is true or false");
    Config other = store.get();
    other.wakeWord = !other.wakeWord;
    check(onlyWakeChanged(store.get(), other), "the wake word is no connection setting");
    check(!onlyWakeChanged(store.get(), store.get()), "saving the same again is no wake word change");
    {
      Config level = store.get();
      level.wakeSensitivity = wake::WakeSensitivity::High;
      check(onlyWakeChanged(store.get(), level), "the sensitivity is none either");
    }
    check(store.get().wakeSensitivity == wake::WakeSensitivity::Normal &&
              store.publicJson().find("\"wakeSensitivity\":\"normal\"") != std::string::npos,
          "the sensitivity starts on normal");
    check(store.update(R"({"wakeSensitivity":"high"})", error) &&
              store.get().wakeSensitivity == wake::WakeSensitivity::High &&
              store.publicJson().find("\"wakeSensitivity\":\"high\"") != std::string::npos,
          "high is taken");
    for (const char* bad : {R"({"wakeSensitivity":"loud"})", R"({"wakeSensitivity":1})", R"({"wakeSensitivity":""})"}) {
      error = {};
      check(!store.update(bad, error) && error.field == "wakeSensitivity", std::string("refused: ") + bad);
    }
    {
      ConfigStore saved(directory);
      check(saved.load() && saved.get().wakeSensitivity == wake::WakeSensitivity::High,
            "the sensitivity survives a restart");
    }
    for (auto change : {+[](Config& c) { c.assistant = "off"; }, +[](Config& c) { c.url += "x"; },
                        +[](Config& c) { c.token += "x"; }, +[](Config& c) { c.pipeline += "x"; },
                        +[](Config& c) { c.device += "x"; }}) {
      Config changed = other;
      change(changed);
      check(!onlyWakeChanged(store.get(), changed), "every other setting is one");
    }
    ConfigStore stored(directory);
    check(stored.load() && stored.get().wakeWord, "the wake word setting survives a restart");
    check(store.erase(), "factory reset removes secret");
    ConfigStore reloaded(directory);
    check(reloaded.load() && reloaded.get().assistant == "off" &&
              reloaded.get().token.empty(),
          "reset stays off after restart");
    ::rmdir(directory);
  }
  assistantSetting();
  savedFilesKeepTheirMeaning();
  enabledFollowsTheAssistant();
  aBlockingReasonOutlivesTheRetry();
  aFailedRequestLeavesTheClockReady();
  {
    Rig r;
    r.begin();
    check(r.controls.back().operation == StreamControl::Probe,
          "prepare HA before opening microphone");
    r.session.tick(1500);
    check(
        !r.disconnected && r.session.state() == AssistSession::State::Starting,
        "slow cloud startup does not consume or overflow audio buffer");
    r.stt(true, 1600);
    check(r.controls.back().operation == StreamControl::Start,
          "HA readiness admits capture");
  }
  {
    Rig r;
    r.begin();
    r.session.endInput(150);
    check(r.disconnected && !r.session.busy(),
          "ending input during HA preparation cancels immediately");
    r.stt();
    check(r.controls.back().operation == StreamControl::Probe,
          "late HA readiness cannot capture after cancelling");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    std::vector<int16_t> pcm(24, -1234);
    r.session.receive({2, StreamEvent::Kind::Audio, 0, 0, 115, false, pcm, {}},
                      115);
    check(r.sent.back().second && r.sent.back().first.size() == 49 &&
              r.sent.back().first[0] == 7,
          "handler plus exact PCM");
    check(static_cast<unsigned char>(r.sent.back().first[1]) == 46 &&
              static_cast<unsigned char>(r.sent.back().first[2]) == 251,
          "signed little endian");
    r.session.endInput(150);
    check(r.controls.back().operation == StreamControl::Stop,
          "ending input stops capture");
    r.session.receive({2, StreamEvent::Kind::Ended, 24, 0, 160, false, {}, {}},
                      160);
    check(r.sent.back().second && r.sent.back().first == std::string(1, 7),
          "clean terminal sends EOF");
    r.session.text(
        R"({"id":2,"type":"event","event":{"type":"tts-end","data":{"tts_output":{"url":"/api/tts_proxy/answer.wav"}}}})",
        200);
    check(r.tts == "/api/tts_proxy/answer.wav", "nested tts output");
    r.session.text(
        R"({"id":2,"type":"event","event":{"type":"run-end","data":{}}})", 201);
    check(!r.session.ready(), "wait for audible completion");
    r.session.playbackDone(true, 250);
    check(r.session.ready(), "ready after playback");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.receive({2,
                       StreamEvent::Kind::Audio,
                       24,
                       0,
                       120,
                       false,
                       std::vector<int16_t>(24),
                       {}},
                      120);
    check(r.disconnected && r.session.state() == AssistSession::State::Error,
          "gap cancels pipeline without EOF");
    for (const auto& p : r.sent) check(!p.second, "broken capture never sent");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.receive({2,
                       StreamEvent::Kind::Audio,
                       0,
                       0,
                       100,
                       false,
                       std::vector<int16_t>(24),
                       {}},
                      351);
    check(r.disconnected, "stale transported audio fails closed");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.tick(270);
    check(r.controls.back().operation == StreamControl::Keepalive,
          "active capture lease renewed");
    r.session.cancel();
    check(r.controls.back().operation == StreamControl::Stop,
          "cancel revokes microphone");
  }
  {
    Rig r;
    r.begin();
    r.stt();
    r.session.text(
        R"({"id":2,"type":"event","event":{"type":"stt-end","data":{}}})", 130);
    const auto count = r.sent.size();
    r.session.receive({2,
                       StreamEvent::Kind::Audio,
                       0,
                       0,
                       131,
                       false,
                       std::vector<int16_t>(24),
                       {}},
                      131);
    r.session.receive({2, StreamEvent::Kind::Ended, 24, 0, 132, false, {}, {}},
                      132);
    check(r.sent.size() == count && !r.disconnected,
          "VAD closes handler; tail and EOF are not sent to a removed handler");
  }
  {
    Rig r;
    r.session.receive({1, StreamEvent::Kind::Support, 0, 0, 10, false, {}, {}},
                      10);
    check(!r.session.ready(), "unavailable microphone prevents PTT");
    r.session.tick(2100);
    check(r.controls.back().operation == StreamControl::Probe,
          "capability probe retried");
    r.session.receive({1, StreamEvent::Kind::Support, 0, 0, 2101, true, {}, {}},
                      2101);
    check(r.session.ready(),
          "late MCU discovery recovers without HA reconnect");
  }
  {
    Rig r;
    r.begin();
    check(r.sent.back().first.find("device_id") == std::string::npos,
          "no device, no device_id");
  }
  {
    Rig r("0123456789abcdef0123456789abcdef");
    r.begin();
    check(r.sent.back().first.find(
              R"("device_id":"0123456789abcdef0123456789abcdef")") !=
              std::string::npos,
          "run carries the device for its area");
  }
  WebSocket::Endpoint e;
  std::string path;
  check(parseOrigin("https://[::1]:8123", e) && e.tls && e.host == "::1",
        "IPv6 origin");
  for (const auto* bad : {"http://user@ha", "http://ha/path", "http://ha:0",
                          "http://ha?x", "http://ha\r\nX:bad", "file://ha", "http://ha:65536",
                          "http://ha:8123@evil", "http://@ha", "http://[1::2::3]", "http://ha#"})
    check(!parseOrigin(bad, e), "reject malformed origin");
  Config c{false, "http://ha:8123", "secret", "", ""};
  check(ttsTarget(c, "http://ha:8123/api/tts_proxy/a.wav", path),
        "same origin TTS");
  for (const auto* bad : {"//other/a", "http://evil/api/tts_proxy/a",
                          "/api/states", "https://ha:8123/api/tts_proxy/a",
                          "http://ha:8123@evil/api/tts_proxy/a", "http://@ha:8123/api/tts_proxy/a"})
    check(!ttsTarget(c, bad, path), "TTS cannot escape configured origin");
  std::puts(
      "Assist session: authentication, pipeline, PCM, EOF, playback, failure "
      "and URL boundaries passed");
}
