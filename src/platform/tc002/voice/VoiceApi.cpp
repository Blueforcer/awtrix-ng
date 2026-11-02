#include "VoiceErrors.h"
#include "platform/tc002/voice/VoiceApi.h"

#include <utility>

#include "core/api/ApiRouter.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/host/OwnPage.h"
#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::tc002::voice {
namespace {
void sendError(httplib::Response& res, int status, const char* code,
               const std::string& message, const std::string& field = {}) {
  res.status = status;
  res.set_content(api::errorJson(code, message, field), "application/json");
}
constexpr const char* kMarker = "X-Awtrix-Voice";
}  // namespace
VoiceApi::VoiceApi(VoiceRuntime& runtime, bool wakeWordAvailable, Hooks hooks)
    : runtime_(runtime), wakeWordAvailable_(wakeWordAvailable), hooks_(std::move(hooks)) {}
std::string VoiceApi::status() const {
  const ConfigStore& config = runtime_.config();
  const AssistSession& session = runtime_.session();
  const bool app = !config.assistantApp().empty();
  const std::string problem = app ? hooks_.appProblem() : "";
  std::string json;
  api::JsonWriter writer(json);
  writer.beginObject()
      .key("config")
      .raw(config.publicJson())
      .member("state", !app ? AssistSession::name(session.state()) : problem.empty() ? "ready" : "error")
      .member("error",
              !runtime_.loaded() ? errors::configUnavailable
              : app              ? problem
              : session.state() == AssistSession::State::Ready &&
                      !session.microphoneAvailable()
                  ? errors::microphoneUpdate
                  : session.error())
      .member("lastError", session.lastError())
      .member("wakeWordAvailable", wakeWordAvailable_)
      .member("wakeListening", hooks_.wakeListening())
      .key("assistantApps")
      .beginArray();
  for (const std::string& name : hooks_.assistantApps()) writer.value(name);
  writer.endArray()
      .key("pipelines")
      .raw(session.pipelines())
      .endObject();
  return json;
}
bool VoiceApi::handle(const httplib::Request& req, httplib::Response& res) {
  if (req.path == "/api/v1/voice/start") {
    res.set_header("Cache-Control", "no-store");
    if (req.method != "POST") {
      sendError(res, 405, "methodNotAllowed", "allowed: POST");
    } else if (!host::refuseForeign(req, res, kMarker)) {
      if (!hooks_.startAssistant())
        sendError(res, 409, "voiceUnavailable", "assistant not ready");
      else
        res.set_content("{\"ok\":true}", "application/json");
    }
    return true;
  }
  if (req.path != "/api/v1/voice") return false;
  res.set_header("Cache-Control", "no-store");
  if (req.method == "GET") {
    res.set_content(status(), "application/json");
  } else if (req.method == "POST") {
    if (host::refuseForeign(req, res, kMarker)) return true;
    ConfigStore::Rejection rejection;
    if (runtime_.updateConfig(req.body, rejection)) {
      res.set_content("{\"ok\":true}", "application/json");
    } else if (rejection.stored) {
      sendError(res, 422, "validationFailed", rejection.message, rejection.field);
    } else {
      sendError(res, 500, "internalError", rejection.message);
    }
  } else {
    sendError(res, 405, "methodNotAllowed", "allowed: GET, POST");
  }
  return true;
}
}  // namespace awtrix::tc002::voice
