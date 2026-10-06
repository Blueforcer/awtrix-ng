#include "../support.h"
#include "../../test/EngineFakes.h"
#include "platform/linux/host/HostButtonInput.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/apps/AppRegistry.h"
#include "core/script/ScriptHost.h"
#include "persistence/DeviceConfig.h"

namespace {
using namespace awtrix;

using awtrix::test::require;

using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;
struct Rig {
  sound::AudioRouter audio;
  Display display;
  System system;
  CoreEngine engine{audio, display, system};
  DeviceConfig config;
  HostButtonInput input;
  std::vector<std::array<bool, 3>> changes;

  Rig() {
    engine.state().settings().autoTransition = false;
    engine.state().settings().transitionDurationMs = 0;
    engine.tick(0);
    input.begin(engine, config, nullptr);
    engine.state().subscribe([this](StateEvent event) {
      if (event == StateEvent::ButtonsChanged) changes.push_back(engine.state().runtime().buttons);
    });
  }
  void click(int button, int64_t now) {
    input.transition(button, true, now);
    input.transition(button, false, now);
    engine.tick(now);
  }
  void notification() {
    Command command(CommandType::Notify);
    command.payload = "{\"text\":\"held\",\"hold\":true}";
    require(engine.execute(command) == DispatchResult::Ok, "install held notification");
  }
};

void orderedEdgesAndNavigation() {
  Rig rig;
  for (int i = 0; i < 3; ++i) {
    rig.input.transition(2, true, 10);
    rig.input.transition(2, false, 10);
  }
  require(rig.engine.currentAppId() == "Time", "actions stay queued until shared engine tick");
  rig.engine.tick(10);
  require(rig.engine.currentAppId() == "Humidity", "three same-frame detents advance three apps");
  require(rig.changes.size() == 6, "every short press and release retains its state event");
  for (std::size_t i = 0; i < rig.changes.size(); ++i)
    require(rig.changes[i] == std::array<bool, 3>{false, false, i % 2 == 0}, "physical state edges stay ordered");
  rig.input.tickHeld(20);
  rig.input.transition(-1, true, 20);
  rig.input.transition(3, true, 20);
  rig.engine.tick(20);
  require(rig.changes.size() == 6 && rig.engine.currentAppId() == "Humidity", "idle and invalid input add no actions");
  rig.click(0, 30);
  require(rig.engine.currentAppId() == "Temperature", "left uses shared previous-app command");
}

void webhooksFollowPhysicalEdgesAfterRouting() {
  Rig rig;
  rig.config.rotate = true;
  std::vector<std::pair<int, bool>> posted;
  const auto post = [&](int button, bool pressed) {
    require(rig.engine.state().runtime().buttons[button] == pressed, "state is published before webhook delivery");
    posted.emplace_back(button, pressed);
  };
  rig.input.transition(0, true, 10, post);
  rig.input.transition(0, true, 11, post);
  rig.input.tickHeld(12);
  rig.input.transition(0, false, 13, post);
  require(posted == std::vector<std::pair<int, bool>>{{0, true}, {0, false}},
          "rotation keeps physical webhook names and repeated states add no edges");
}

void mappingAndBlockedNavigation() {
  for (int flags = 0; flags < 4; ++flags) {
    Rig rig;
    rig.config.rotate = (flags & 1) != 0;
    rig.config.swapButtons = (flags & 2) != 0;
    int scriptButton = -1;
    rig.input.setButtonHook([&](int button, bool pressed, bool) {
      if (pressed) scriptButton = button;
      return false;
    });
    rig.click(0, 10);
    const bool swapped = rig.config.rotate != rig.config.swapButtons;
    require(rig.engine.currentAppId() == (swapped ? "Date" : "Battery"), "rotation and swap use XOR navigation");
    require(scriptButton == (swapped ? 2 : 0), "script button uses logical mapping");
    require(rig.changes.front() == std::array<bool, 3>{true, false, false}, "published state retains physical names");
  }
  Rig rig;
  rig.engine.state().settings().blockNavigation = true;
  rig.click(2, 10);
  require(rig.engine.currentAppId() == "Time", "blockNavigation suppresses previous/next");
  rig.notification();
  rig.click(1, 100);
  require(!rig.engine.hasNotification(), "select still dismisses while navigation is blocked");
  rig.click(1, 200);
  require(!rig.engine.state().runtime().matrixOff, "blockNavigation suppresses double-press power toggle");
}

void selectTimingAndConsumption() {
  Rig rig;
  rig.notification();
  rig.click(1, 100);
  require(!rig.engine.hasNotification() && !rig.engine.state().runtime().matrixOff, "single select dismisses without power toggle");
  rig.click(1, 400);
  require(rig.engine.state().runtime().matrixOff, "select double press includes the 300ms boundary");
  rig.click(1, 701);
  require(rig.engine.state().runtime().matrixOff, "301ms interval does not toggle");
  rig.click(1, 900);
  require(!rig.engine.state().runtime().matrixOff, "next double press restores display power");

  Rig consumed;
  consumed.notification();
  bool take = true;
  consumed.input.setButtonHook([&](int, bool pressed, bool) { return pressed && take; });
  consumed.click(1, 100);
  consumed.click(2, 150);
  require(consumed.engine.hasNotification() && consumed.engine.currentAppId() == "Time", "script refusal precedes every default action");
  take = false;
  consumed.click(1, 200);
  require(!consumed.engine.hasNotification() && !consumed.engine.state().runtime().matrixOff,
          "consumed select cannot pair with later unconsumed select");
}

void batchedSelectPowerOrdering() {
  for (const bool initiallyOff : {false, true}) {
    Rig sequential;
    sequential.engine.state().runtime().matrixOff = initiallyOff;
    for (const int64_t now : {100, 200, 300}) sequential.click(1, now);
    for (const bool firstClickInPreviousFrame : {false, true}) {
      Rig batched;
      batched.engine.state().runtime().matrixOff = initiallyOff;
      for (const int64_t now : {100, 200, 300}) {
        batched.input.transition(1, true, now);
        batched.input.transition(1, false, now);
        if (firstClickInPreviousFrame && now == 100) batched.engine.tick(now);
      }
      batched.engine.tick(300);
      require(batched.engine.state().runtime().matrixOff == sequential.engine.state().runtime().matrixOff,
              "three select clicks keep power-toggle parity across frame boundaries");
    }
  }
  for (const bool externalPower : {false, true}) {
    Rig rig;
    rig.click(1, 100);
    rig.input.transition(1, true, 200);
    rig.input.transition(1, false, 200);
    Command external(CommandType::SetDisplay);
    external.payload = externalPower ? "{\"power\":true}" : "{\"power\":false}";
    require(rig.engine.execute(external) == DispatchResult::Ok, "intervening API display command");
    rig.input.transition(1, true, 300);
    rig.input.transition(1, false, 300);
    rig.engine.tick(300);
    require(rig.engine.state().runtime().matrixOff == externalPower,
            "later select toggles the actual state after an intervening API command");
  }
}

void sharedScriptHoldLifecycle() {
  Rig rig;
  int64_t now = 0;
  script::ScriptServices services;
  services.monotonicMs = [&] { return now; };
  AppRegistry registry;
  script::ScriptHost scripts(registry, services, nullptr, nullptr);
  require(scripts.set("control", "class App\nvar trace\ndef init() self.trace = '' end\n"
      "def draw() end\ndef on_button_event(button, event)\n"
      " self.trace += button + ':' + event + ','\n return true\nend\n"
      "def check() return self.trace end\nend\nreturn App()"), "install real Berry input handler");
  RenderCtx context;
  scripts.tick(context, "control");
  rig.input.setButtonHook([&](int button, bool pressed, bool) {
    return scripts.handleButtonState("control", button, pressed);
  });
  now = 10;
  rig.input.transition(2, true, now);
  rig.input.tickHeld(now);
  now = 609;
  rig.input.tickHeld(now);
  now = 610;
  rig.input.tickHeld(now);
  now = 760;
  rig.input.tickHeld(now);
  now = 770;
  rig.input.transition(2, false, now);
  rig.engine.tick(now);
  auto* app = static_cast<script::ScriptApp*>(registry.find("control"));
  std::string trace;
  require(app && app->callCheckForTest(trace), "read Berry event trace");
  require(trace == "right:press,right:long,right:repeat,right:release,", "shared ScriptHost owns capture and hold timing without duplicate presses");
  require(rig.engine.currentAppId() == "Time", "captured Berry input does not navigate");
}
}

void knobReportsWithoutNavigating() {
  Rig rig;
  auto& runtime = rig.engine.state().runtime();
  rig.input.knob(true);
  rig.input.knob(true);
  require(runtime.knob && rig.changes.size() == 1, "a knob push publishes once");
  rig.input.knob(false);
  require(!runtime.knob && rig.changes.size() == 2, "a knob release publishes once");
  for (int i = 0; i < 3; ++i) rig.input.turn(1);
  rig.input.turn(-1);
  require(runtime.knobTurns == 2u, "detents count clockwise positive");
  for (int i = 0; i < 4; ++i) rig.input.turn(-1);
  require(runtime.knobTurns == static_cast<uint32_t>(-2), "the detent count wraps below zero");
  rig.engine.tick(10);
  require(rig.engine.currentAppId() == "Time", "the knob never switches apps");
  require(rig.changes.back() == std::array<bool, 3>{false, false, false}, "the knob leaves the buttons alone");
}

int main() {
  orderedEdgesAndNavigation();
  webhooksFollowPhysicalEdgesAfterRouting();
  mappingAndBlockedNavigation();
  selectTimingAndConsumption();
  batchedSelectPowerOrdering();
  sharedScriptHoldLifecycle();
  knobReportsWithoutNavigating();
  std::puts("Host button contracts passed: ordered input, shared actions, capture and timing");
}
