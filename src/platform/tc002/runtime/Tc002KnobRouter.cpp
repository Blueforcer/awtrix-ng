#include "platform/tc002/runtime/Tc002KnobRouter.h"

#include "core/CoreEngine.h"
#include "core/input/ButtonWebhook.h"
#include "persistence/DeviceConfig.h"
#include "platform/linux/host/HostButtonInput.h"
#include "platform/linux/LinuxButtonWebhook.h"
#include "platform/linux/script/KnobScripting.h"
#include "platform/tc002/runtime/Tc002QuickSettings.h"
#include "platform/tc002/voice/KnobGesture.h"
#include "platform/tc002/voice/VoiceRuntime.h"

namespace awtrix {

bool Tc002KnobRouter::voiceBusy() const { return t_.voice && t_.voice->busy(); }

bool Tc002KnobRouter::poll(Tc002Input& input, int64_t now, bool panelBusy, bool scriptsRunning) {
  const bool local = !panelBusy && !t_.engine.state().settings().blockNavigation;
  // The app on screen may take the knob, except while the clock is busy with it already.
  const bool app = scriptsRunning && !panelBusy && !t_.quickSettings.open(now) && !voiceBusy();
  Tc002InputDecoder::Sink sink{[&](int button, bool pressed) {
    t_.buttons.transition(button, pressed, now, [&](int changed, bool down) {
      t_.webhook.press(t_.cfg.buttonCallback, awtrix::input::kWebhookButtonNames[changed], down);
    });
  }, [&](Tc002InputDecoder::Knob turned) { knob(turned, now, local, app); }};
  if (!input.poll(sink)) return false;
  t_.buttons.tickHeld(now);
  if (t_.scripts) t_.scripts->held(t_.engine.currentAppId());
  return true;
}

void Tc002KnobRouter::knob(Tc002InputDecoder::Knob knob, int64_t now, bool local, bool app) {
  switch (knob) {
    case Tc002InputDecoder::Knob::Clockwise:
    case Tc002InputDecoder::Knob::Counterclockwise: {
      const int direction = knob == Tc002InputDecoder::Knob::Clockwise ? 1 : -1;
      t_.buttons.turn(direction);
      t_.webhook.turn(t_.cfg.buttonCallback, direction);
      if (app && t_.scripts && t_.scripts->turn(t_.engine.currentAppId(), direction)) break;
      if (local && t_.gesture.turn() && !voiceBusy()) t_.quickSettings.turn(direction, now);
      break;
    }
    case Tc002InputDecoder::Knob::Down:
      t_.buttons.knob(true);
      t_.webhook.press(t_.cfg.buttonCallback, awtrix::input::kWebhookKnobName, true);
      if (app && t_.scripts && t_.scripts->press(t_.engine.currentAppId())) break;
      if (local) t_.gesture.down(now);
      break;
    case Tc002InputDecoder::Knob::Up:
      t_.buttons.knob(false);
      t_.webhook.press(t_.cfg.buttonCallback, awtrix::input::kWebhookKnobName, false);
      if (t_.scripts && t_.scripts->release(t_.engine.currentAppId())) break;
      if (local && t_.gesture.up(now) == tc002::voice::KnobGesture::Action::Click) {
        if (voiceBusy()) t_.voice->endInput(now);
        else t_.quickSettings.push(now);
      }
      break;
  }
}

}
