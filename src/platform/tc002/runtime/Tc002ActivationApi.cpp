#include "platform/tc002/runtime/Tc002ActivationApi.h"

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/tc002/runtime/Tc002Activation.h"

namespace awtrix {

namespace {

int fail(std::string& body, int status, const char* code, const char* message, const std::string& field = {}) {
  body = api::errorJson(code, message, field);
  return status;
}

}

int Tc002ActivationApi::handle(const std::string& method, const std::string& path, const std::string& request,
                               std::string& body) {
  if (path != "/api/v1/activation") return 0;
  if (method == "PUT") {
    if (!api::isWellFormed(request)) return fail(body, 400, "invalidJson", "invalid JSON");
    bool given = false;
    if (!api::memberValue(api::JsonReader(request), "consent").asBool(given))
      return fail(body, 422, "validationFailed", "expected true or false", "consent");
    if (!activation_.setConsent(given)) return fail(body, 507, "insufficientStorage", "not saved");
  } else if (method != "GET") {
    return fail(body, 405, "methodNotAllowed", "allowed: GET, PUT");
  }
  body.clear();
  api::JsonWriter w(body);
  w.beginObject();
  switch (activation_.consent()) {
    case Tc002Activation::Consent::Unasked: w.memberNull("consent"); break;
    case Tc002Activation::Consent::Given: w.member("consent", true); break;
    case Tc002Activation::Consent::Declined: w.member("consent", false); break;
  }
  w.endObject();
  return 200;
}

}
