#include "core/script/EngineScriptApplication.h"

#include <utility>

#include "core/CoreEngine.h"
#include "core/script/ScriptSoundCommand.h"
#include "core/sound/SoundSpec.h"

namespace awtrix::script {

bool EngineScriptApplication::notify(const std::string& json, const std::string& script) {
  DispatchDetail detail;
  return engine_.notifyFromScript(script, json, detail) == DispatchResult::Ok;
}

const Settings* EngineScriptApplication::settings() {
  return &engine_.state().settings();
}

const RuntimeState* EngineScriptApplication::runtime() {
  return &engine_.state().runtime();
}

bool EngineScriptApplication::setSettings(const std::string& json) {
  Command command(CommandType::SetSettings);
  command.payload = json;
  command.source = Source::Internal;
  return engine_.submit(std::move(command));
}

bool EngineScriptApplication::setDisplayPower(bool on) {
  Command command(CommandType::SetDisplay);
  command.payload = on ? "{\"power\":true}" : "{\"power\":false}";
  command.source = Source::Internal;
  return engine_.submit(std::move(command));
}

// Execute in the script call. Unavailable answers 1 if an output can play the choices, else 0.
int EngineScriptApplication::sound(SoundAction action, const std::string& json,
                                    const std::string& script, std::string& error) {
  Command command = scriptSoundCommand(action, json, script);
  const DispatchResult r = engine_.execute(command);
  if (r == DispatchResult::ValidationError) {
    const DispatchDetail& detail = engine_.lastDetail();
    error = detail.field;
    if (!error.empty()) error += ": ";
    error += detail.message;
    return -1;
  }
  if (r != DispatchResult::Unavailable) return 1;
  sound::Choices choices;
  DispatchDetail ignored;
  return sound::parse(json, sound::Origin::Script, choices, ignored) &&
         audio_.canPlay(choices);
}

bool EngineScriptApplication::soundPlaying() {
  return audio_.appSoundPlaying();
}

bool EngineScriptApplication::audioPlaying() {
  return audio_.alertStatus().playing || audio_.appStatus().playing;
}

int EngineScriptApplication::soundCaps() {
  const sound::Caps c = audio_.caps();
  return (c.mp3 ? 1 : 0) | (c.rtttl ? 2 : 0) | (c.song ? 4 : 0) | (c.speech ? 8 : 0) |
         (c.track ? 16 : 0) | (c.radio ? 32 : 0) | (c.url ? 64 : 0) | (c.effect ? 128 : 0) |
         (c.clip ? 256 : 0);
}

void EngineScriptApplication::rotateNext() {
  engine_.scriptNextApp();
}

void EngineScriptApplication::rotatePrevious() {
  engine_.scriptPreviousApp();
}

bool EngineScriptApplication::showApp(const std::string& id) {
  return engine_.scriptShowApp(id);
}

void EngineScriptApplication::holdRotation(bool hold) {
  engine_.setScriptRotationPaused(hold);
}

void EngineScriptApplication::restartTurn() {
  engine_.scriptRestartTurn();
}

bool EngineScriptApplication::closeSession(const std::string& id) {
  return engine_.requestSessionEnd(id);
}

}
