#include "../support.h"

#include <unistd.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "core/api/JsonReader.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/secrets/SecretApi.h"

using namespace awtrix;
using awtrix::test::check;
namespace fs = std::filesystem;

namespace {

const std::string kValue = "sk-VALUE-123";

struct Rig {
  fs::path root = fs::temp_directory_path() / ("secret-api-" + std::to_string(::getpid()));
  std::map<std::string, std::string> sources = {
      {"Assistant", "# @secret key label=\"API key\" help=\"Your key\" hosts=api.test\n# @secret org hosts=org.test\n"},
      {"Broken", "# @secret 1bad\n"},
      {"NoHosts", "# @secret key\n"},
      {"Plain", "class A end\n"}};
  secrets::Service service{(root / "s").string(), [this](const std::string& app) {
                             const auto it = sources.find(app);
                             return it == sources.end() ? std::string() : it->second;
                           }};
  secrets::Api api{service, [this] {
                     std::vector<std::string> names;
                     for (const auto& entry : sources) names.push_back(entry.first);
                     return names;
                   }};
  std::vector<std::string> bodies;
  bool everyAnswerNoStore = true;

  Rig() { fs::remove_all(root); }
  ~Rig() { fs::remove_all(root); }

  httplib::Response send(const char* method, const std::string& path, const std::string& body = "",
                         const char* origin = "http://192.168.1.50", bool marked = true) {
    httplib::Request r;
    r.method = method;
    r.path = path;
    r.body = body;
    r.headers.emplace("Host", "192.168.1.50");
    if (origin) r.headers.emplace("Origin", origin);
    if (marked) r.headers.emplace("X-Awtrix-Secret", "1");
    httplib::Response res;
    if (!api.handle(r, res)) res.status = -1;
    bodies.push_back(res.body);
    if (res.status != -1 && res.get_header_value("Cache-Control") != "no-store") everyAnswerNoStore = false;
    return res;
  }
};

std::string errorCode(const httplib::Response& res) {
  std::string code;
  api::memberValue(api::memberValue(api::JsonReader(res.body), "error"), "code").appendString(code);
  return code;
}

}

int main() {
  Rig rig;
  check(rig.send("GET", "/api/v1/voice").status == -1, "other paths are not ours");
  check(rig.send("GET", "/api/v1/secretsx").status == -1, "a longer word is not ours");

  httplib::Response res = rig.send("GET", "/api/v1/secrets");
  check(res.status == 200 && res.body == "{\"apps\":[\"Assistant\",\"Broken\",\"NoHosts\"]}",
        "the list names the apps with @secret lines, valid or not: " + res.body);
  check(rig.send("POST", "/api/v1/secrets").status == 405, "the list is read-only");

  for (const char* path : {"/api/v1/secrets/Plain", "/api/v1/secrets/Missing", "/api/v1/secrets/..",
                           "/api/v1/secrets/Assistant/key/more"})
    check(rig.send("GET", path).status == 404, std::string("404: ") + path);

  res = rig.send("GET", "/api/v1/secrets/Assistant");
  check(res.status == 200 &&
            res.body == "{\"keys\":[{\"key\":\"key\",\"label\":\"API key\",\"help\":\"Your key\",\"set\":false,"
                        "\"hosts\":[\"api.test\"]},"
                        "{\"key\":\"org\",\"label\":\"\",\"help\":\"\",\"set\":false,\"hosts\":[\"org.test\"]}]}",
        "an app's keys: " + res.body);
  res = rig.send("GET", "/api/v1/secrets/Broken");
  check(res.status == 200 && res.body == "{\"invalid\":\"@secret: bad key\"}", "an invalid line: " + res.body);
  res = rig.send("GET", "/api/v1/secrets/NoHosts");
  check(res.status == 200 && res.body == "{\"invalid\":\"@secret: hosts needs 1-8 hosts\"}",
        "a line without hosts=: " + res.body);

  const std::string put = "{\"key\":\"" + kValue + "\"}";
  check(rig.send("PUT", "/api/v1/secrets/Assistant", put, "https://evil.example").status == 403, "a foreign origin");
  check(rig.send("PUT", "/api/v1/secrets/Assistant", put, "http://192.168.1.50", false).status == 403,
        "no marker header");
  check(rig.send("PUT", "/api/v1/secrets/Assistant", put, nullptr).status == 403, "no origin");
  check(errorCode(rig.send("DELETE", "/api/v1/secrets/Assistant/key", "", "https://evil.example")) ==
            "forbiddenOrigin",
        "a delete needs the device's own page too");
  check(!rig.service.has("Assistant", "key"), "nothing refused was stored");

  check(rig.send("PUT", "/api/v1/secrets/Assistant", put).status == 200 && rig.service.has("Assistant", "key"),
        "the owner sets a key");
  res = rig.send("GET", "/api/v1/secrets/Assistant");
  check(res.body.find("\"key\":\"key\",\"label\":\"API key\",\"help\":\"Your key\",\"set\":true") != std::string::npos,
        "the key shows as set: " + res.body);

  for (const std::string& body : {std::string("{\"nope\":\"x\"}"), "{\"key\":\"" + std::string(513, 'x') + "\"}",
                                  "{\"key\":\"" + kValue + "\\nX-Injected: 1\"}",
                                  "{\"key\":\"" + kValue + "\\u000d\"}", std::string("{\"key\":7}")}) {
    res = rig.send("PUT", "/api/v1/secrets/Assistant", body);
    check(res.status == 422 && errorCode(res) == "validationFailed", "422: " + body + " -> " + res.body);
  }
  check(rig.send("PUT", "/api/v1/secrets/Broken", "{\"key\":\"x\"}").status == 422, "an invalid line takes nothing");
  res = rig.send("PUT", "/api/v1/secrets/NoHosts", "{\"key\":\"x\"}");
  check(res.status == 422 && errorCode(res) == "validationFailed" && !rig.service.has("NoHosts", "key"),
        "a line without hosts= takes no key: " + res.body);
  for (const std::string& body : {std::string("not json"), std::string("[]"), std::string(""),
                                  std::string(16385, ' ')})
    check(rig.send("PUT", "/api/v1/secrets/Assistant", body).status == 400, "400 for a body that is no object");
  check(rig.service.has("Assistant", "key"), "refused changes keep the value");

  check(rig.send("POST", "/api/v1/secrets/Assistant", put).status == 405, "POST on an app");
  check(rig.send("DELETE", "/api/v1/secrets/Assistant").status == 405, "DELETE needs a key");
  check(rig.send("GET", "/api/v1/secrets/Assistant/key").status == 405, "a key is never read");
  check(rig.send("PUT", "/api/v1/secrets/Assistant/key", put).status == 405, "a key is not put on its own");
  check(rig.send("DELETE", "/api/v1/secrets/Assistant/nope").status == 422, "an undeclared key");
  check(rig.send("DELETE", "/api/v1/secrets/Assistant/key").status == 200 && !rig.service.has("Assistant", "key"),
        "DELETE erases a key");
  check(rig.send("DELETE", "/api/v1/secrets/Assistant/key").status == 200, "erasing an empty key is fine");
  check(rig.send("PUT", "/api/v1/secrets/Assistant", put).status == 200 &&
            rig.send("PUT", "/api/v1/secrets/Assistant", "{\"key\":\"\"}").status == 200 &&
            !rig.service.has("Assistant", "key"),
        "an empty string erases");

  check(rig.everyAnswerNoStore, "every answer is no-store");
  bool leaked = false;
  for (const std::string& body : rig.bodies) leaked = leaked || body.find(kValue) != std::string::npos;
  check(!leaked, "no answer holds a value");
  return awtrix::test::finish("linux-secret-api");
}
