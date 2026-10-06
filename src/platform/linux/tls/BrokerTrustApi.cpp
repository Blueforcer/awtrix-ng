#include "platform/linux/tls/BrokerTrustApi.h"

#include "core/api/ApiRouter.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/tls/BrokerTrust.h"

namespace awtrix::tls {
namespace {

int fail(std::string& body, int status, const char* code, const std::string& message, const std::string& field = {}) {
  body = api::errorJson(code, message, field);
  return status;
}

const char* caName(BrokerTrust::Ca ca) {
  switch (ca) {
    case BrokerTrust::Ca::Uploaded: return "uploaded";
    case BrokerTrust::Ca::Unusable: return "unusable";
    case BrokerTrust::Ca::Public: break;
  }
  return "public";
}

}

std::string BrokerTrustApi::status() const {
  std::string out;
  api::JsonWriter w(out);
  w.beginObject();
  w.member("ca", caName(broker_.ca()));
  const std::string pending = broker_.peer()->pending();
  if (pending.empty()) w.memberNull("pending");
  else w.member("pending", pending);
  w.endObject();
  return out;
}

int BrokerTrustApi::handle(const std::string& method, const std::string& path, const std::string& request,
                           std::string& body) {
  if (path == "/api/v1/mqtt/tls") {
    if (method != "GET") return fail(body, 405, "methodNotAllowed", "allowed: GET");
    body = status();
    return 200;
  }
  if (path != "/api/v1/mqtt/tls/ca") return 0;
  if (method == "DELETE") {
    if (!broker_.removeCa()) return fail(body, 507, "insufficientStorage", "not saved");
    body = status();
    return 200;
  }
  if (method != "PUT") return fail(body, 405, "methodNotAllowed", "allowed: PUT, DELETE");
  if (!api::isWellFormed(request)) return fail(body, 400, "invalidJson", "invalid JSON");
  std::string certificate, error;
  api::JsonReader value = api::memberValue(api::JsonReader(request), "certificate");
  if (!value.isString() || !value.appendString(certificate) || certificate.empty())
    return fail(body, 422, "validationFailed", "expected a PEM string", "certificate");
  if (!broker_.uploadCa(certificate, error)) {
    if (error == "not saved") return fail(body, 507, "insufficientStorage", error);
    return fail(body, 422, "validationFailed", error, "certificate");
  }
  body = status();
  return 200;
}

}
