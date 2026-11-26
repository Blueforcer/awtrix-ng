#include "core/remote/HostActs.h"

#include "core/CoreEngine.h"
#include "core/api/JsonReader.h"
#include "core/remote/RemoteApps.h"
#include "core/remote/RemoteWire.h"
#include "core/script/ScriptSoundCommand.h"

namespace awtrix {
namespace remote {

namespace {
api::HttpResult ok() { return {200, "application/json", "{\"ok\":true}"}; }

api::HttpResult expected(const char* field) {
  return api::errorResult(422, "validationFailed", std::string("expected ") + field, field);
}

std::string text(const api::JsonReader& value) {
  std::string out;
  if (!value.appendString(out)) out.clear();
  return out;
}

api::HttpResult answer(CommandType type, DispatchResult result, const DispatchDetail& detail) {
  return api::httpResponse(Command(type), result, detail);
}
}

api::HttpResult HostActs::request(const std::string& method, bool keyOk, const std::string& body) {
  if (!keyOk) return api::unauthorized();
  if (method != "POST") return api::errorResult(405, "methodNotAllowed", "allowed: POST");
  return run(body);
}

api::HttpResult HostActs::run(const std::string& body) {
  api::JsonReader act, script, action, sound, notification, app, hold, button, id;
  if (!api::readMembers(body, {{"act", &act},
                               {"script", &script},
                               {"action", &action},
                               {"sound", &sound},
                               {"notification", &notification},
                               {"app", &app},
                               {"hold", &hold},
                               {"button", &button},
                               {"id", &id}}))
    return api::errorResult(400, "invalidJson", "invalid JSON");
  const std::string name = text(act);
  if (name == "next") {
    engine_.scriptNextApp();
    return ok();
  }
  if (name == "previous") {
    engine_.scriptPreviousApp();
    return ok();
  }
  if (name == "show") {
    const std::string target = text(app);
    if (target.empty()) return expected("app");
    if (engine_.scriptShowApp(target)) return ok();
    return api::errorResult(404, "notFound", "app not found", "app");
  }
  if (name == "hold") {
    bool on = false;
    if (!hold.asBool(on)) return expected("hold");
    apps_.hold(on);
    return ok();
  }
  if (name == "restart") {
    const std::string target = text(app);
    if (target.empty()) return expected("app");
    if (target == engine_.currentAppId()) engine_.scriptRestartTurn();
    return ok();
  }
  if (name == "press") {
    const std::string target = text(app);
    long long index = -1;
    if (target.empty()) return expected("app");
    if (!button.asLong(index) || index < 0 || index >= wire::kButtons) return expected("button");
    if (target == engine_.currentAppId() && press_) press_(static_cast<int>(index));
    return ok();
  }
  if (name == "endSession") {
    const std::string session = text(id);
    if (session.empty()) return expected("id");
    if (engine_.requestSessionEnd(session)) return ok();
    return api::errorResult(404, "notFound", "no such session", "id");
  }
  if (name == "sound") return this->sound(text(script), action, sound);
  if (name == "notify") {
    DispatchDetail detail;
    const DispatchResult result =
        engine_.notifyFromScript(text(script), std::string(notification.valueText()), detail);
    return answer(CommandType::Notify, result, detail);
  }
  return api::errorResult(422, "validationFailed", "invalid act", "act");
}

api::HttpResult HostActs::sound(const std::string& owner, const api::JsonReader& action,
                                const api::JsonReader& sound) {
  if (owner.empty()) return expected("script");
  const std::string kind = text(action);
  script::SoundAction what;
  if (kind == "play") what = script::SoundAction::Play;
  else if (kind == "effect") what = script::SoundAction::Effect;
  else if (kind == "stop") what = script::SoundAction::Stop;
  else if (kind == "stopMusic") what = script::SoundAction::StopMusic;
  else if (kind == "release") what = script::SoundAction::Release;
  else return api::errorResult(422, "validationFailed", "invalid action", "action");
  Command command = scriptSoundCommand(what, std::string(sound.valueText()), owner);
  const DispatchResult result = engine_.execute(command);
  return api::httpResponse(command, result, engine_.lastDetail());
}

}
}
