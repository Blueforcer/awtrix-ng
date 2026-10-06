#pragma once

#include <cstdint>

#include "platform/tc002/runtime/Tc002Input.h"

namespace awtrix {
class CoreEngine;
class HostButtonInput;
class LinuxButtonWebhook;
class Tc002QuickSettings;
struct DeviceConfig;
namespace script { class KnobScripting; }
namespace tc002::voice {
class KnobGesture;
class VoiceRuntime;
}

// Hands the TC002 keys to the buttons and their webhook, and the knob to the app on screen, the
// quick settings or the voice gesture.
class Tc002KnobRouter {
 public:
  struct Targets {
    CoreEngine& engine;
    const DeviceConfig& cfg;
    HostButtonInput& buttons;
    LinuxButtonWebhook& webhook;
    script::KnobScripting* scripts;
    tc002::voice::KnobGesture& gesture;
    Tc002QuickSettings& quickSettings;
    tc002::voice::VoiceRuntime* voice;
  };

  explicit Tc002KnobRouter(const Targets& targets) : t_(targets) {}

  // panelBusy: the boot intro or an update owns the panel. False when the input failed.
  bool poll(Tc002Input& input, int64_t now, bool panelBusy, bool scriptsRunning);

 private:
  void knob(Tc002InputDecoder::Knob knob, int64_t now, bool local, bool app);
  bool voiceBusy() const;

  Targets t_;
};

}
