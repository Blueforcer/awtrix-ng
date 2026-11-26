#include "platform/linux/remote/RemoteClockApplication.h"

#include <utility>

#include "core/Command.h"
#include "core/CoreEngine.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/sound/SoundSpec.h"
#include "platform/linux/remote/RemoteRole.h"

namespace awtrix::remote {

namespace {
const char* const kSoundActions[] = {"play", "effect", "stop", "stopMusic", "release"};

std::string simpleAct(const char* act) {
  std::string body;
  api::JsonWriter(body).beginObject().key("act").value(act).endObject();
  return body;
}

std::string appAct(const char* act, const char* key, const std::string& name) {
  std::string body;
  api::JsonWriter(body).beginObject().key("act").value(act).key(key).value(name).endObject();
  return body;
}
}

bool RemoteClockApplication::notify(const std::string& json, const std::string& script) {
  DispatchDetail detail;
  if (engine_.checkNotification(json, detail) != DispatchResult::Ok) return false;
  std::string body;
  api::JsonWriter(body).beginObject().key("act").value("notify").key("script").value(script)
      .key("notification").raw(json).endObject();
  role_.act(std::move(body));
  return true;
}

const Settings* RemoteClockApplication::settings() { return &engine_.state().settings(); }

const RuntimeState* RemoteClockApplication::runtime() { return &engine_.state().runtime(); }

bool RemoteClockApplication::setSettings(const std::string& json) {
  Command command(CommandType::SetSettings);
  command.payload = json;
  command.source = Source::Internal;
  engine_.submit(std::move(command));
  role_.clock().request("PATCH", "/api/v1/settings", json);
  return true;
}

bool RemoteClockApplication::setDisplayPower(bool on) {
  role_.clock().request("PATCH", "/api/v1/display", on ? "{\"power\":true}" : "{\"power\":false}");
  return true;
}

int RemoteClockApplication::sound(script::SoundAction action, const std::string& json,
                                  const std::string& script, std::string& error) {
  const bool plays = action == script::SoundAction::Play || action == script::SoundAction::Effect;
  if (plays) {
    sound::Choices choices;
    DispatchDetail detail;
    if (!sound::parse(json, sound::Origin::Script, choices, detail)) {
      error = detail.field;
      if (!error.empty()) error += ": ";
      error += detail.message;
      return -1;
    }
    const sound::Caps* caps = role_.soundCaps();
    if (caps && !sound::canPlay(*caps, choices)) return 0;
    sounding_.insert(script);
  } else if (action == script::SoundAction::Release) {
    if (!sounding_.erase(script)) return 1;
  } else if (action == script::SoundAction::Stop) {
    sounding_.erase(script);
  }
  std::string body;
  api::JsonWriter w(body);
  w.beginObject().key("act").value("sound").key("script").value(script)
      .key("action").value(kSoundActions[static_cast<int>(action)]);
  if (plays) w.key("sound").raw(json);
  w.endObject();
  role_.act(std::move(body));
  return 1;
}

bool RemoteClockApplication::soundPlaying() { return role_.audioPlaying(); }

bool RemoteClockApplication::audioPlaying() { return role_.audioPlaying(); }

int RemoteClockApplication::soundCaps() {
  const sound::Caps* caps = role_.soundCaps();
  return caps ? sound::capsBits(*caps) : 0;
}

void RemoteClockApplication::rotateNext() { role_.act(simpleAct("next")); }

void RemoteClockApplication::rotatePrevious() { role_.act(simpleAct("previous")); }

bool RemoteClockApplication::showApp(const std::string& id) {
  if (!engine_.isInLoop(id)) return false;
  role_.act(appAct("show", "app", id));
  return true;
}

void RemoteClockApplication::holdRotation(bool hold) { role_.hold(hold); }

void RemoteClockApplication::restartTurn() {
  const std::string& shown = role_.shown();
  if (!shown.empty()) role_.act(appAct("restart", "app", shown));
}

bool RemoteClockApplication::closeSession(const std::string& id) {
  if (id.empty() || engine_.sessionApp() != id) return false;
  role_.act(appAct("endSession", "id", id));
  return true;
}

sound::Caps soundCapsFrom(const std::string& capabilities) {
  const api::JsonReader audio = api::memberValue(capabilities, "audio");
  sound::Caps caps;
  const std::pair<const char*, bool*> outputs[] = {
      {"mp3", &caps.mp3},   {"rtttl", &caps.rtttl}, {"song", &caps.song}, {"speech", &caps.speech},
      {"track", &caps.track}, {"radio", &caps.radio}, {"url", &caps.url},   {"effect", &caps.effect},
      {"clip", &caps.clip}};
  for (const auto& output : outputs) api::memberValue(audio, output.first).asBool(*output.second);
  return caps;
}

}
