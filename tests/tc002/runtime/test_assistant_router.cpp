#include "../../support.h"
#include "../../LogLines.h"
#include "../../ManualExecutor.h"
#include "FakeMicrophoneDaemon.h"

#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/script/ScriptMeta.h"
#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/voice/AssistantRouter.h"
#include "platform/tc002/voice/VoiceErrors.h"
#include "platform/tc002/wake/WakeListener.h"
#include "platform/tc002/wake/WakeSensitivity.h"

using namespace awtrix;
using awtrix::test::check;
using awtrix::test::logAfter;
using awtrix::test::logMark;
using awtrix::test::require;
using tc002::MicrophoneHub;
using tc002::MicrophoneMark;
using tc002::StreamControl;
using tc002::StreamEvent;
using tc002::voice::AssistantRouter;
using tc002::voice::KnobGesture;
using tc002::voice::VoiceTrigger;
using tc002::wake::WakeSensitivity;

namespace {
std::size_t allocations = 0, blockAllocations = 0;

void* counted(std::size_t size) noexcept {
  ++allocations;
  if (size >= tc002::kStreamBlockSamples * sizeof(int16_t)) ++blockAllocations;
  return std::malloc(size ? size : 1);
}

void* countedOrThrow(std::size_t size) {
  if (void* memory = counted(size)) return memory;
  throw std::bad_alloc();
}
}

[[gnu::noinline]] void* operator new(std::size_t size) { return countedOrThrow(size); }
[[gnu::noinline]] void* operator new[](std::size_t size) { return countedOrThrow(size); }
[[gnu::noinline]] void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
[[gnu::noinline]] void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return counted(size); }
[[gnu::noinline]] void operator delete(void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }

namespace {
namespace fs = std::filesystem;

struct FakeVoice final : AssistantRouter::Voice {
  bool assistantOff = false, homeAssistantChosen = false, wakeWordOn = true, isReady = false, isBusy = false,
       isStarting = false;
  std::string app = "Chat";
  const std::string none;
  WakeSensitivity sensitivity = WakeSensitivity::Normal;
  unsigned begins = 0;
  WakeSensitivity wakeSensitivity() const override { return sensitivity; }
  bool off() const override { return assistantOff; }
  bool homeAssistant() const override { return homeAssistantChosen; }
  const std::string& assistantApp() const override { return assistantOff || homeAssistantChosen ? none : app; }
  bool wakeWord() const override { return wakeWordOn; }
  bool ready() const override { return isReady; }
  bool busy() const override { return isBusy; }
  bool starting() const override { return isStarting; }
  bool begin(int64_t) override {
    ++begins;
    return true;
  }
};

struct Triggered {
  std::string app, name;
  MicrophoneMark mark;
};

struct FakeModule final : AssistantRouter::Module {
  bool isBusy = false, isStarting = false, waiting = false;
  std::set<std::string> holding;
  std::vector<Triggered> triggers;
  unsigned drops = 0;
  bool busy() const override { return isBusy; }
  bool starting() const override { return isStarting; }
  bool triggered() const override { return waiting; }
  bool holds(const std::string& app) const override { return holding.count(app) != 0; }
  void trigger(const std::string& app, const std::string& name, MicrophoneMark mark) override {
    triggers.push_back({app, name, mark});
    waiting = true;
  }
  void dropTrigger() override {
    waiting = false;
    ++drops;
  }
  void deliver() { waiting = false; }
};

script::ScriptMeta meta(const std::string& needs, bool module = false, bool headless = false) {
  script::ScriptMeta out;
  out.needs = needs;
  out.module = module;
  out.headless = headless;
  return out;
}

struct Rig {
  int64_t now = 1000;
  test::FakeMicrophoneDaemon daemon;
  MicrophoneHub hub{daemon.link(), {}};
  KnobGesture gesture;
  FakeVoice voice;
  FakeModule module;
  AssistantRouter::NoModule noModule;
  bool allowed = true;
  std::map<std::string, script::ScriptMeta> metas{{"Chat", meta("voice")}};
  std::vector<std::string> installed{"Chat"};
  std::set<std::string> disabled, failing, hookless;
  std::vector<std::string> opened;
  bool openFails = false;
  std::string shown;
  bool covered = false, speaking = false, slowAnswer = false, onDemand = false;
  std::string session;
  unsigned sessionsEnded = 0;
  bool panelOff = false;
  std::vector<bool> power;
  unsigned dismissed = 0;
  int music = 0;
  MicrophoneHub::Consumer keeper = hub.attach([](const StreamEvent&, int64_t) {});
  bool keeping = false;
  MicrophoneHub::Consumer wakeMicrophone;
  std::vector<StreamControl> wakeSent;
  test::ManualExecutor* wakeWorker = nullptr;
  std::unique_ptr<tc002::wake::WakeListener> wake;
  std::optional<AssistantRouter> router;

  explicit Rig(const fs::path* wakeModel = nullptr, bool withModule = true) {
    if (wakeModel) {
      wakeMicrophone = hub.attach([this](const StreamEvent& e, int64_t at) {
        if (wake) wake->receive(e, at);
      });
      std::string error;
      auto worker = std::make_unique<test::ManualExecutor>();
      wakeWorker = worker.get();
      wake = tc002::wake::WakeListener::open(wakeModel->string(), [this](StreamControl control) {
        wakeSent.push_back(control);
        return wakeMicrophone.port()(control);
      }, std::move(worker), error);
      require(wake != nullptr, "the wake word opens: " + error);
    }
    router.emplace(
        AssistantRouter::Ports{
            [this] { return allowed; },
            [this](const std::string& app) -> const script::ScriptMeta* {
              const auto it = metas.find(app);
              return it == metas.end() ? nullptr : &it->second;
            },
            [this] { return installed; },
            [this](const std::string& app, std::string& out) {
              out = hookless.count(app) ? "class App end" : "def on_assistant(trigger) end";
              return true;
            },
            [this](const std::string& app) { return metas.count(app) && !disabled.count(app); },
            [this](const std::string& app) { return disabled.count(app) != 0; },
            [this](const std::string& app) { return failing.count(app) != 0; },
            [this](const std::string& json, DispatchDetail& detail) {
              opened.push_back(json);
              if (!openFails) {
                if (onDemand) session = voice.app;
                return DispatchResult::Ok;
              }
              detail.message = "no such app";
              return DispatchResult::NotFound;
            },
            [this](const std::string& app) { return shown == app; },
            [this] { return covered; },
            [this] { return speaking; },
            [this](const std::string& app) { return slowAnswer && shown == app; },
            [this](const std::string& app) { return session == app; },
            [this] {
              session.clear();
              ++sessionsEnded;
            },
            [this] { return panelOff; },
            [this](bool on) {
              power.push_back(on);
              panelOff = !on;
            },
            [this] { ++dismissed; },
            [this] {
              ++music;
              return posix::Hold([this] { --music; });
            }},
        AssistantRouter::Parts{voice, withModule ? static_cast<AssistantRouter::Module&>(module) : noModule, gesture,
                               hub, wake.get(), wakeMicrophone});
    hub.tick(now);
  }

  void answer() {
    for (const StreamEvent& e : daemon.answers(now)) hub.receive(e, now);
    if (daemon.streaming()) hub.receive(daemon.ramp(now), now);
  }
  void frame(bool knob = true) {
    now += 30;
    answer();
    hub.tick(now);
    if (keeping) keeper.port()({1, StreamControl::Keepalive});
    if (wakeWorker) wakeWorker->run();
    router->tick(now, knob);
  }
  void frames(int64_t ms, bool knob = true) {
    for (const int64_t end = now + ms; now < end;) frame(knob);
  }
  template <class Done>
  void framesUntil(Done done, int64_t ms) {
    for (const int64_t end = now + ms; !done() && now < end;) frame();
  }
  void keep() {
    keeping = true;
    keeper.port()({1, StreamControl::Start});
    frames(90);
  }
  void release() {
    keeping = false;
    keeper.port()({1, StreamControl::Stop});
  }
  bool start(VoiceTrigger trigger) { return router->start(trigger, now); }
  void shows(const std::string& app) {
    shown = app;
    module.deliver();
  }
};

constexpr int64_t kWakeWithinMs = 3000;

bool same(const MicrophoneMark& a, const MicrophoneMark& b) { return a.epoch == b.epoch && a.sample == b.sample; }

bool lastIs(const std::vector<StreamControl>& sent, StreamControl::Operation operation) {
  return !sent.empty() && sent.back().operation == operation;
}

unsigned count(const std::vector<StreamControl>& sent, StreamControl::Operation operation) {
  unsigned n = 0;
  for (const StreamControl& control : sent) n += control.operation == operation;
  return n;
}

void everyTriggerReachesTheApp() {
  Rig rig;
  rig.keep();
  const MicrophoneMark api = rig.hub.mark();
  check(api.epoch != 0, "the stream runs, so the hub has a mark");
  check(rig.start(VoiceTrigger::Api), "the API starts the assistant app");
  check(rig.module.triggers.size() == 1 && rig.module.triggers[0].app == "Chat" &&
            rig.module.triggers[0].name == "api" && same(rig.module.triggers[0].mark, api),
        "on_assistant gets \"api\" and the hub's position at the trigger");
  check(rig.opened.size() == 1 && rig.opened[0] == R"({"name":"Chat","fast":true})",
        "the app opens at once: " + (rig.opened.empty() ? std::string() : rig.opened[0]));
  check(rig.dismissed == 1 && rig.music == 1, "the quick settings close and the music waits");
  rig.shows("Chat");
  rig.frame();
  const MicrophoneMark mqtt = rig.hub.mark();
  check(rig.start(VoiceTrigger::Mqtt) && rig.module.triggers.size() == 2 && rig.module.triggers.back().name == "mqtt" &&
            same(rig.module.triggers.back().mark, mqtt),
        "MQTT passes \"mqtt\" and the hub's position");
  rig.module.deliver();
  rig.frame();
  const int64_t down = rig.now;
  rig.gesture.down(down);
  while (rig.module.triggers.size() == 2 && rig.now - down < 2 * KnobGesture::kHoldMs) rig.frame();
  check(rig.module.triggers.size() == 3 && rig.module.triggers.back().name == "knob" &&
            same(rig.module.triggers.back().mark, rig.hub.mark()),
        "the knob held passes \"knob\" and the hub's position in that frame");
  check(rig.now - down >= KnobGesture::kHoldMs && rig.now - down < KnobGesture::kHoldMs + 30,
        "after 500 ms: " + std::to_string(rig.now - down));
  rig.gesture.up(rig.now);
  check(rig.music == 1, "one music hold for the whole interaction");
}

void theWakeWordReachesTheApp(const fs::path& model) {
  Rig rig(&model);
  rig.keep();
  check(!rig.wakeSent.empty() && rig.wakeSent[0].operation == StreamControl::Probe, "the wake word listens");
  rig.framesUntil([&rig] { return !rig.module.triggers.empty(); }, kWakeWithinMs);
  require(rig.module.triggers.size() == 1, "the wake word fires");
  const Triggered& heard = rig.module.triggers[0];
  const MicrophoneMark now = rig.hub.mark();
  check(heard.app == "Chat" && heard.name == "wake", "on_assistant gets \"wake\"");
  check(heard.mark.epoch == now.epoch && heard.mark.sample > 0 && heard.mark.sample <= now.sample,
        "the mark is the detection's position in the hub's stream");
  rig.frames(1800);
  check(rig.module.triggers.size() == 1 && lastIs(rig.wakeSent, StreamControl::Keepalive) &&
            count(rig.wakeSent, StreamControl::Probe) == 1,
        "while the trigger waits for the app, the wake word keeps its stream and reports nothing");
}

void theWakeWordIsMuted(const fs::path& model) {
  struct Case {
    const char* what;
    void (*arrange)(Rig&);
  };
  const Case cases[] = {
      {"a script records or plays", [](Rig&) {}},
      {"a script records or plays while Home Assistant is the assistant", [](Rig& r) {
         r.voice.homeAssistantChosen = true;
         r.voice.isReady = true;
       }},
  };
  for (const Case& c : cases) {
    Rig rig(&model);
    c.arrange(rig);
    rig.keep();
    rig.module.isBusy = true;
    rig.frames(3000);
    check(rig.module.triggers.empty() && rig.voice.begins == 0 && count(rig.wakeSent, StreamControl::Probe) == 1 &&
              count(rig.wakeSent, StreamControl::Stop) == 0,
          std::string("the wake word keeps its stream but reports nothing while ") + c.what);
    rig.module.isBusy = false;
    rig.framesUntil([&rig] { return !rig.module.triggers.empty() || rig.voice.begins != 0; }, kWakeWithinMs);
    check((!rig.module.triggers.empty() || rig.voice.begins == 1) && count(rig.wakeSent, StreamControl::Probe) == 1,
          std::string("afterwards it starts the assistant on the same stream, without a new warm-up: ") + c.what);
  }
}

void theClockDoesNotWakeItself(const fs::path& model) {
  Rig rig(&model);
  rig.keep();
  rig.speaking = true;
  rig.frames(3000);
  check(rig.module.triggers.empty() && count(rig.wakeSent, StreamControl::Probe) == 1 &&
            count(rig.wakeSent, StreamControl::Stop) == 0,
        "the wake word keeps its stream but reports nothing while the clock speaks");
  rig.speaking = false;
  rig.framesUntil([&rig] { return !rig.module.triggers.empty(); }, kWakeWithinMs);
  check(!rig.module.triggers.empty(), "and hears again when the clock is done");
}

void refusals() {
  struct Case {
    const char* what;
    void (*arrange)(Rig&);
  };
  const Case cases[] = {
      {"an app that is not installed", [](Rig& r) { r.metas.clear(); }},
      {"a disabled app", [](Rig& r) { r.disabled.insert("Chat"); }},
      {"a headless script", [](Rig& r) { r.metas["Chat"] = meta("voice", false, true); }},
      {"a module", [](Rig& r) { r.metas["Chat"] = meta("voice", true); }},
      {"an app without @needs voice", [](Rig& r) { r.metas["Chat"] = meta("tcp"); }},
      {"a busy voice module", [](Rig& r) { r.module.isBusy = true; }},
      {"a trigger that waits", [](Rig& r) { r.module.waiting = true; }},
      {"an app that holds something", [](Rig& r) { r.module.holding.insert("Chat"); }},
      {"speech on the speaker", [](Rig& r) { r.speaking = true; }},
      {"a busy Home Assistant Voice", [](Rig& r) { r.voice.isBusy = true; }},
  };
  for (const Case& c : cases) {
    Rig rig;
    c.arrange(rig);
    const uint32_t mark = logMark();
    check(!rig.start(VoiceTrigger::Api) && rig.opened.empty() && rig.module.triggers.empty() && rig.music == 0,
          std::string("refused: ") + c.what);
    check(logAfter(mark).find("voice: assistant app Chat unavailable") != std::string::npos,
          std::string("logged: ") + c.what);
  }
  {
    Rig rig;
    rig.allowed = false;
    const uint32_t mark = logMark();
    check(!rig.start(VoiceTrigger::Api) && rig.opened.empty() && rig.dismissed == 0 &&
              logAfter(mark).find("voice: assistant app") == std::string::npos,
          "the boot intro, an update or setup mode refuse every trigger without a word");
  }
  {
    Rig rig(nullptr, false);
    check(!rig.start(VoiceTrigger::Api) && rig.router->apps().empty(), "without the voice module no app answers");
  }
  {
    Rig rig;
    rig.openFails = true;
    const uint32_t mark = logMark();
    check(!rig.start(VoiceTrigger::Api) && rig.module.triggers.empty() && rig.music == 0 && rig.dismissed == 0,
          "an app the engine does not open gets no trigger");
    check(logAfter(mark).find("voice: assistant app Chat did not open: no such app") != std::string::npos,
          "and the log says why");
  }
}

void panelAndMusic() {
  {
    Rig rig;
    rig.panelOff = true;
    check(rig.start(VoiceTrigger::Api) && rig.power == std::vector<bool>{true}, "a panel that is off goes on");
    const uint32_t mark = logMark();
    rig.frames(AssistantRouter::kShowTimeoutMs - 30);
    check(rig.power.size() == 1 && rig.music == 1, "the trigger waits up to 3 s for the app to draw");
    rig.frame();
    check(rig.module.drops == 1 && rig.power == std::vector<bool>{true, false} && rig.music == 0,
          "an app that does not draw within 3 s loses the trigger, the panel goes off again, the music plays");
    check(logAfter(mark).find("voice: assistant app Chat did not show") != std::string::npos, "and the log says so");
  }
  {
    Rig rig;
    rig.panelOff = true;
    rig.start(VoiceTrigger::Api);
    rig.shows("Chat");
    rig.module.holding.insert("Chat");
    rig.frames(15000);
    check(rig.power.size() == 1 && rig.music == 1, "the interaction lasts while the app holds something");
    rig.module.holding.clear();
    rig.frames(AssistantRouter::kAssistantIdleMs - 30);
    check(rig.power.size() == 1 && rig.music == 1, "and 10 s after");
    rig.frame();
    check(rig.power == std::vector<bool>{true, false} && rig.music == 0,
          "then the panel goes off again and the music plays");
    check(rig.module.drops == 1, "and the module drops the trigger and its mark");
  }
  {
    Rig rig;
    rig.panelOff = true;
    rig.start(VoiceTrigger::Api);
    rig.shows("Chat");
    rig.frames(300);
    rig.shown = "Clock";
    rig.frame();
    check(rig.power == std::vector<bool>{true, false} && rig.music == 0 && rig.module.drops == 1,
          "the interaction ends when the app leaves, and the trigger's mark with it");
  }
  {
    Rig rig;
    rig.panelOff = true;
    rig.start(VoiceTrigger::Api);
    rig.shows("Chat");
    rig.frames(300);
    check(rig.start(VoiceTrigger::Mqtt) && rig.power == std::vector<bool>{true} && rig.music == 1,
          "a second trigger while the panel is on keeps it on and keeps one music hold");
    rig.shows("Chat");
    rig.frames(AssistantRouter::kAssistantIdleMs);
    check(rig.power == std::vector<bool>{true, false} && rig.music == 0,
          "the second interaction keeps the first one's panel state and switches it off at the end");
  }
  {
    Rig rig;
    rig.panelOff = true;
    rig.start(VoiceTrigger::Api);
    rig.shows("Chat");
    rig.panelOff = true;
    rig.frames(AssistantRouter::kAssistantIdleMs);
    check(rig.power == std::vector<bool>{true} && rig.music == 0, "a panel switched off meanwhile stays off");
  }
  {
    Rig rig;
    rig.start(VoiceTrigger::Api);
    rig.shows("Chat");
    rig.frames(AssistantRouter::kAssistantIdleMs);
    check(rig.power.empty() && rig.music == 0, "a panel that was on is left alone");
  }
}

void speechKeepsTheInteraction() {
  Rig rig;
  rig.panelOff = true;
  rig.start(VoiceTrigger::Api);
  rig.shows("Chat");
  rig.speaking = true;
  rig.frames(3 * AssistantRouter::kAssistantIdleMs);
  check(rig.power.size() == 1 && rig.music == 1, "the panel stays on and the radio waits while the clock speaks");
  rig.speaking = false;
  rig.frames(AssistantRouter::kAssistantIdleMs - 30);
  check(rig.power.size() == 1 && rig.music == 1, "and 10 s after");
  rig.frame();
  check(rig.power == std::vector<bool>{true, false} && rig.music == 0, "then the interaction ends");
}

void aSlowAnswerKeepsTheInteraction() {
  Rig rig;
  rig.panelOff = true;
  rig.start(VoiceTrigger::Api);
  rig.shows("Chat");
  rig.slowAnswer = true;
  rig.frames(3 * AssistantRouter::kAssistantIdleMs);
  check(rig.power.size() == 1 && rig.music == 1, "the panel stays on and the radio waits for a slow answer");
  rig.slowAnswer = false;
  rig.frames(AssistantRouter::kAssistantIdleMs - 30);
  check(rig.power.size() == 1 && rig.music == 1, "and 10 s after");
  rig.frame();
  check(rig.power == std::vector<bool>{true, false} && rig.music == 0, "then the interaction ends");
}

void homeAssistantIgnoresLocalSpeech() {
  Rig rig;
  rig.voice.homeAssistantChosen = true;
  rig.voice.isReady = true;
  rig.speaking = true;
  check(rig.start(VoiceTrigger::Api) && rig.voice.begins == 1, "Home Assistant Voice starts as it always did");
}

void aSessionKeepsTheInteraction() {
  Rig rig;
  rig.panelOff = true;
  rig.onDemand = true;
  rig.start(VoiceTrigger::Api);
  rig.shows("Chat");
  rig.frames(3 * AssistantRouter::kAssistantIdleMs);
  check(rig.power.size() == 1 && rig.music == 1 && rig.sessionsEnded == 0,
        "an idle @ondemand app it opened stays on the panel, lit, with the radio waiting");
  rig.session.clear();
  rig.shown = "Clock";
  rig.frame();
  check(rig.power == std::vector<bool>{true, false} && rig.music == 0, "until its session ends");
}

void aCoveredPanelStopsTheClock() {
  Rig rig;
  rig.onDemand = true;
  rig.covered = true;
  check(rig.start(VoiceTrigger::Api) && rig.module.waiting, "a trigger waits behind a notification");
  rig.frames(3 * AssistantRouter::kShowTimeoutMs);
  check(rig.module.waiting && rig.module.drops == 0 && rig.sessionsEnded == 0 && rig.music == 1,
        "as long as the notification lasts");
  rig.covered = false;
  rig.frames(AssistantRouter::kShowTimeoutMs - 30);
  check(rig.module.waiting && rig.module.drops == 0, "the 3 s start when the app can draw again");
  rig.shows("Chat");
  rig.frame();
  check(rig.module.drops == 0 && rig.music == 1 && rig.sessionsEnded == 0, "and the app answers");
}

void aLapseEndsTheSessionOfTheTrigger() {
  {
    Rig rig;
    rig.onDemand = true;
    check(rig.start(VoiceTrigger::Api) && rig.session == "Chat", "the trigger opens the app's session");
    rig.frames(AssistantRouter::kShowTimeoutMs);
    check(rig.module.drops == 1 && rig.session.empty() && rig.sessionsEnded == 1 && rig.music == 0,
          "an app that never draws loses the trigger and the session it opened");
  }
  {
    Rig rig;
    rig.onDemand = true;
    rig.session = "Chat";
    check(rig.start(VoiceTrigger::Api), "the trigger finds the session open");
    rig.frames(AssistantRouter::kShowTimeoutMs);
    check(rig.module.drops == 1 && rig.session == "Chat" && rig.sessionsEnded == 0,
          "a session someone else opened stays when the trigger lapses");
  }
  {
    Rig rig;
    rig.onDemand = true;
    check(rig.start(VoiceTrigger::Api), "the first trigger opens the session");
    rig.shows("Chat");
    rig.frame();
    check(rig.start(VoiceTrigger::Mqtt) && rig.module.waiting, "a second one finds it open");
    rig.frames(AssistantRouter::kShowTimeoutMs);
    check(rig.module.drops == 1 && rig.session.empty() && rig.sessionsEnded == 1,
          "it is still the session of this interaction when the second trigger lapses");
  }
  {
    Rig rig;
    check(rig.start(VoiceTrigger::Api), "an app of the rotation opens");
    rig.frames(AssistantRouter::kShowTimeoutMs);
    check(rig.module.drops == 1 && rig.sessionsEnded == 0, "and there is no session to end");
  }
}

void holdConditions() {
  struct Case {
    const char* what;
    void (*set)(Rig&, bool);
  };
  const Case cases[] = {
      {"Home Assistant Voice starts", [](Rig& r, bool on) { r.voice.isStarting = on; }},
      {"a recording of the voice module starts", [](Rig& r, bool on) { r.module.isStarting = on; }},
      {"a trigger waits for its app", [](Rig& r, bool on) { r.module.waiting = on; }},
  };
  for (const Case& c : cases) {
    Rig rig;
    rig.keep();
    c.set(rig, true);
    rig.frame();
    rig.release();
    rig.frames(3 * MicrophoneHub::kLingerMs);
    check(rig.hub.active() && rig.daemon.streaming(), std::string("the hub keeps the stream while ") + c.what);
    c.set(rig, false);
    rig.frames(2 * MicrophoneHub::kLingerMs);
    check(!rig.hub.active() && !rig.daemon.streaming(), std::string("and lets it go after: ") + c.what);
  }
}

void wakeGating(const fs::path& model) {
  struct Case {
    const char* what;
    void (*set)(Rig&, bool);
  };
  const Case cases[] = {
      {"the panel is busy", [](Rig& r, bool on) { r.allowed = on; }},
      {"the wake word is off", [](Rig& r, bool on) { r.voice.wakeWordOn = on; }},
      {"the assistant is off", [](Rig& r, bool on) { r.voice.assistantOff = !on; }},
      {"the assistant app cannot start", [](Rig& r, bool on) {
         if (on) r.disabled.clear();
         else r.disabled.insert("Chat");
       }},
  };
  for (const Case& c : cases) {
    Rig rig(&model);
    c.set(rig, false);
    rig.frames(300);
    check(rig.wakeSent.empty() && !rig.router->listening(), std::string("the wake word does not listen while ") + c.what);
    c.set(rig, true);
    rig.frame();
    check(rig.wakeSent.size() == 1 && rig.wakeSent[0].operation == StreamControl::Probe && rig.router->listening(),
          std::string("it listens once ") + c.what + " no more");
    rig.frames(300);
    c.set(rig, false);
    rig.frame();
    check(lastIs(rig.wakeSent, StreamControl::Stop), std::string("and stops when ") + c.what);
  }
  {
    Rig rig(&model);
    rig.voice.homeAssistantChosen = true;
    rig.frames(300);
    check(rig.wakeSent.empty() && !rig.router->listening(), "with Home Assistant, the wake word waits until Voice is ready");
    rig.voice.isReady = true;
    rig.frame();
    check(lastIs(rig.wakeSent, StreamControl::Probe) && rig.router->listening(), "then it listens");
  }
  {
    Rig rig;
    check(!rig.router->listening(), "a clock without a wake word never listens");
  }
}

void theVoiceSetsTheLevel(const fs::path& model) {
  const fs::path dir = model.parent_path();
  fs::copy_file(model, dir / "doubtful.tflite");
  std::ofstream(dir / "doubtful.json") << R"({"probability_cutoff":0.44,"sliding_window_size":1})";
  const fs::path doubtful = dir / "doubtful.tflite";
  Rig rig(&doubtful);
  rig.keep();
  rig.frames(kWakeWithinMs);
  check(rig.module.triggers.empty(), "the rig's stream stays under the model's cutoff");
  rig.voice.sensitivity = WakeSensitivity::High;
  rig.framesUntil([&rig] { return !rig.module.triggers.empty(); }, kWakeWithinMs);
  check(rig.module.triggers.size() == 1 && count(rig.wakeSent, StreamControl::Probe) == 1,
        "the level of the settings reaches the listener without a new stream");
}

void offStartsNothing() {
  Rig rig;
  rig.voice.assistantOff = true;
  const uint32_t mark = logMark();
  check(!rig.start(VoiceTrigger::Api), "off: the API starts nothing");
  check(!rig.start(VoiceTrigger::Mqtt), "off: MQTT starts nothing");
  rig.gesture.down(rig.now);
  rig.frames(2 * KnobGesture::kHoldMs);
  check(rig.gesture.up(rig.now) == KnobGesture::Action::None, "off: a held knob is no click");
  check(rig.voice.begins == 0 && rig.opened.empty() && rig.module.triggers.empty() && rig.dismissed == 0 &&
            rig.music == 0,
        "off: no trigger reaches anything");
  check(logAfter(mark).find("voice: assistant app") == std::string::npos, "off: and nothing is logged");
  rig.voice.assistantOff = false;
  check(rig.start(VoiceTrigger::Api), "the same clock starts the app once it is the target again");
}

void homeAssistant() {
  Rig rig;
  rig.voice.homeAssistantChosen = true;
  check(!rig.start(VoiceTrigger::Api) && rig.voice.begins == 0 && rig.dismissed == 0,
        "Home Assistant Voice that is not ready starts nothing");
  rig.voice.isReady = true;
  rig.module.isBusy = true;
  check(!rig.start(VoiceTrigger::Api) && rig.voice.begins == 0, "nor while the voice module is busy");
  rig.module.isBusy = false;
  check(rig.start(VoiceTrigger::Mqtt) && rig.voice.begins == 1 && rig.dismissed == 1 && rig.opened.empty() &&
            rig.module.triggers.empty() && rig.music == 0,
        "a ready Voice begins, the quick settings close, no app opens");
  rig.gesture.down(rig.now);
  rig.frames(KnobGesture::kHoldMs);
  check(rig.voice.begins == 2, "the knob held for 500 ms begins Voice");
  rig.gesture.up(rig.now);
  rig.gesture.down(rig.now);
  rig.frames(KnobGesture::kHoldMs, false);
  check(rig.voice.begins == 2, "not while the knob may not start it");
  rig.gesture.up(rig.now);
}

void aFrameAllocatesNothing() {
  Rig rig;
  const std::string name = "AnAssistantWithALongName";
  rig.voice.app = name;
  rig.metas = {{name, meta("tcp mqtt voice")}};
  rig.keep();
  rig.frame();
  const std::size_t before = allocations;
  for (int i = 0; i < 10; ++i) rig.router->tick(rig.now, true);
  const std::size_t made = allocations - before;
  check(made == 0, "a frame of the router allocates nothing: " + std::to_string(made));
}

void aBlockCopiesItsSamplesOnce(const fs::path& model) {
  Rig rig(&model);
  rig.keep();
  require(rig.daemon.streaming() && !rig.wakeSent.empty(), "the keeper and the wake word take the stream");
  std::vector<StreamEvent> blocks;
  for (int i = 0; i < 40; ++i) blocks.push_back(rig.daemon.ramp(rig.now));
  const auto deliver = [&rig](const StreamEvent& block) {
    rig.hub.receive(block, rig.now);
    rig.wakeWorker->run();
  };
  for (int i = 0; i < 4; ++i) deliver(blocks[static_cast<std::size_t>(i)]);
  const std::size_t before = blockAllocations;
  for (std::size_t i = 4; i < blocks.size(); ++i) deliver(blocks[i]);
  const std::size_t made = blockAllocations - before;
  check(made == 0, "a block reaches all consumers without a buffer of its own: " + std::to_string(made));
}

void assistantApps() {
  Rig rig;
  rig.metas = {{"Chat", meta("voice")}, {"Talk", meta("tcp voice")}, {"Clock", meta("")},
               {"Lib", meta("voice", true)}, {"Bot", meta("voice", false, true)}, {"Off", meta("voice")},
               {"Near", meta("voices tcp")}, {"Echo", meta("voice")}};
  rig.installed = {"Bot", "Chat", "Clock", "Echo", "Gone", "Lib", "Near", "Off", "Talk"};
  rig.hookless.insert("Echo");
  rig.disabled.insert("Off");
  check(rig.router->apps() == std::vector<std::string>{"Chat", "Off", "Talk"},
        "the apps that can answer: installed with @needs voice and on_assistant, neither module nor headless, "
        "disabled or not");
}

void theAppSaysWhyItCannotAnswer() {
  namespace errors = tc002::voice::errors;
  Rig rig;
  const auto problem = [&rig] { return std::string(rig.router->appProblem()); };
  check(problem().empty(), "an installed app that can answer has no problem");
  rig.voice.app = "Gone";
  check(problem() == errors::appMissing, "an app that is not installed");
  rig.voice.app = "Chat";
  rig.metas["Chat"] = meta("speech");
  check(problem() == errors::appMissing, "an app that no longer needs voice cannot answer");
  rig.metas["Chat"] = meta("voice");
  rig.disabled.insert("Chat");
  check(problem() == errors::appOff, "an app switched off");
  check(!rig.start(VoiceTrigger::Api), "and it starts nothing, as it says");
  rig.disabled.clear();
  rig.failing.insert("Chat");
  check(problem() == errors::appBroken, "an app with a script error");
  rig.failing.clear();
  check(problem().empty(), "fixed, it answers again");
  Rig bare(nullptr, false);
  check(std::string(bare.router->appProblem()) == errors::appMissing, "without the voice module no app can answer");
}

}

int main(int argc, char** argv) {
  require(argc == 2, "usage: tc002-assistant-router-test <wake word model>");
  const fs::path dir = fs::temp_directory_path() / ("assistant-router-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path model = dir / "word.tflite";
  fs::copy_file(argv[1], model);
  std::ofstream(dir / "word.json") << R"({"probability_cutoff":0,"sliding_window_size":1})";

  everyTriggerReachesTheApp();
  theWakeWordReachesTheApp(model);
  theWakeWordIsMuted(model);
  theClockDoesNotWakeItself(model);
  refusals();
  panelAndMusic();
  speechKeepsTheInteraction();
  aSlowAnswerKeepsTheInteraction();
  homeAssistantIgnoresLocalSpeech();
  aSessionKeepsTheInteraction();
  aCoveredPanelStopsTheClock();
  aLapseEndsTheSessionOfTheTrigger();
  holdConditions();
  wakeGating(model);
  theVoiceSetsTheLevel(model);
  offStartsNothing();
  homeAssistant();
  aFrameAllocatesNothing();
  aBlockCopiesItsSamplesOnce(model);
  assistantApps();
  theAppSaysWhyItCannotAnswer();
  fs::remove_all(dir);
  return awtrix::test::finish("tc002-assistant-router");
}
