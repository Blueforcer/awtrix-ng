#include "../support.h"
#include "../LogLines.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "platform/linux/secrets/SecretService.h"
#include "system/Log.h"

using namespace awtrix;
using awtrix::net::RequestFill;
using awtrix::net::RequestHeaders;
using awtrix::test::check;
using awtrix::test::logAfter;
using awtrix::test::logMark;
namespace fs = std::filesystem;

namespace {

const std::string kValue = "sk-VALUE-123";
const std::string kOrg = "org-VALUE-9";
const std::string kAssistant =
    "# @secret key label=\"API key\" help=\"Your key\" hosts=api.test\n"
    "# @secret org\n"
    "class A end\n";

struct Rig {
  fs::path root = fs::temp_directory_path() / ("secrets-" + std::to_string(::getpid()));
  fs::path dir = root / "script-secrets";
  std::map<std::string, std::string> sources = {
      {"Assistant", kAssistant}, {"Plain", "class A end\n"}, {"Broken", "# @secret 1bad\n"}};
  posix::PrivateStore store{dir.string(), secrets::Service::kMaxRecord};
  secrets::Service service{store, [this](const std::string& app) {
                             const auto it = sources.find(app);
                             return it == sources.end() ? std::string() : it->second;
                           }};

  Rig() { fs::remove_all(root); }
  ~Rig() { fs::remove_all(root); }

  bool set(const std::string& app, const std::string& key, const std::string& value, std::string* error = nullptr) {
    std::string message;
    const bool ok = service.set(app, {{key, value}}, message);
    if (error) *error = message;
    return ok;
  }
  std::size_t files() const {
    std::size_t count = 0;
    std::error_code error;
    for (fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) ++count;
    return count;
  }
  RequestFill fill(std::string url, RequestHeaders& headers, const std::string& app = "Assistant") {
    return service.fill(app, url, headers);
  }
};

bool keyIs(const secrets::KeyStatus& k, const char* key, const char* label, const char* help, bool set) {
  return k.key == key && k.label == label && k.help == help && k.set == set;
}

void statusAndSet() {
  Rig rig;
  secrets::Status s = rig.service.status("Assistant");
  check(s.declared && s.invalid.empty() && s.keys.size() == 2 && keyIs(s.keys[0], "key", "API key", "Your key", false) &&
            keyIs(s.keys[1], "org", "", "", false),
        "the declared keys in header order, none set");
  check(!rig.service.status("Plain").declared && !rig.service.status("Missing").declared,
        "an app without @secret, and a missing one, declare nothing");
  s = rig.service.status("Broken");
  check(s.declared && s.invalid == "@secret: bad key" && s.keys.empty(), "a malformed line is declared and invalid");

  std::string error;
  check(rig.set("Assistant", "key", kValue, &error) && error.empty(), "a value is stored");
  s = rig.service.status("Assistant");
  check(s.keys.size() == 2 && s.keys[0].set && !s.keys[1].set, "status shows which key is set");
  check(rig.service.has("Assistant", "key") && !rig.service.has("Assistant", "org") &&
            !rig.service.has("Assistant", "nope") && !rig.service.has("Plain", "key") &&
            !rig.service.has("Missing", "key"),
        "has() is true only for a declared key with a value");
  check(rig.files() == 1, "one file for the app");

  check(rig.set("Assistant", "key", "", &error) && !rig.service.has("Assistant", "key"), "an empty value erases");
  check(rig.files() == 0, "an app without values keeps no file");
}

void refusedValues() {
  Rig rig;
  std::string error;
  check(rig.set("Assistant", "key", std::string(secrets::Service::kMaxValue, 'v')), "a value at the limit");
  const std::string tooLong = kValue + std::string(secrets::Service::kMaxValue, 'x');
  check(!rig.set("Assistant", "key", tooLong, &error) && error == "value too long", "a value too long: " + error);
  for (const std::string& bad : {kValue + "\r", kValue + "\n" + "X-Injected: 1", kValue + std::string(1, '\0')})
    check(!rig.set("Assistant", "key", bad, &error) && error == "invalid value", "CR, LF and NUL are refused");
  check(!rig.set("Assistant", "nope", kValue, &error) && error == "unknown key", "an undeclared key: " + error);
  check(!rig.set("Plain", "key", kValue, &error) && error == "no @secret line", "an app without @secret: " + error);
  check(!rig.set("Broken", "key", kValue, &error) && error == "@secret: bad key", "an invalid line: " + error);
  check(error.find(kValue) == std::string::npos, "no error names the value");

  check(!rig.service.set("Assistant", {{"org", kOrg}, {"key", kValue + "\n"}}, error) &&
            !rig.service.has("Assistant", "org"),
        "one bad value refuses the whole change");
  check(rig.service.set("Assistant", {{"org", kOrg}, {"key", kValue}}, error) && rig.service.has("Assistant", "org") &&
            rig.service.has("Assistant", "key"),
        "several values at once");
}

void bindingPerKey() {
  Rig rig;
  std::string error;
  check(rig.service.set("Assistant", {{"key", kValue}, {"org", kOrg}}, error), "both values");

  rig.sources["Assistant"] =
      "# @secret key label=\"Your key\" help=\"New words\" hosts=API.test\n"
      "# @secret org label=\"Organisation\" help=\"For teams\"\n";
  rig.service.sourceSaved("Assistant");
  check(rig.service.has("Assistant", "key") && rig.service.has("Assistant", "org"),
        "a new label or help keeps the value, and so does the host in another case");
  const secrets::Status renamed = rig.service.status("Assistant");
  check(renamed.keys.size() == 2 && renamed.keys[0].label == "Your key" && renamed.keys[1].help == "For teams",
        "the new words show");

  rig.sources["Assistant"] =
      "# @secret key hosts=api.test,other.test\n"
      "# @secret org\n";
  rig.service.sourceSaved("Assistant");
  check(!rig.service.has("Assistant", "key") && rig.service.has("Assistant", "org"),
        "changed hosts= erase only that key's value");
  check(rig.set("Assistant", "key", kValue), "set again");
  rig.sources["Assistant"] =
      "# @secret key hosts=other.test,api.test,api.test\n"
      "# @secret org\n";
  rig.service.sourceSaved("Assistant");
  check(rig.service.has("Assistant", "key"), "the same hosts in another order, with a repeat, are the same hosts");

  rig.sources["Assistant"] =
      "# @secret key\n"
      "# @secret org\n";
  rig.service.sourceSaved("Assistant");
  check(!rig.service.has("Assistant", "key") && rig.service.has("Assistant", "org"),
        "dropping hosts= widens where a key may go, so its value goes");
  rig.sources["Assistant"] = "# @secret key\n";
  rig.service.sourceSaved("Assistant");
  check(!rig.service.has("Assistant", "org") && rig.files() == 0, "a key that is gone loses its value");

  rig.sources["Assistant"] = kAssistant;
  rig.service.sourceSaved("Assistant");
  check(rig.set("Assistant", "key", kValue) && rig.files() == 1, "set again");
  rig.service.sourceRemoved("Assistant");
  check(!rig.service.has("Assistant", "key") && rig.files() == 0, "a removed app loses every value");

  check(rig.set("Assistant", "key", kValue) && rig.set("Assistant", "org", kOrg), "set again");
  rig.sources["Assistant"] = "# @secret key label=\"Key\" hosts=other.test\n# @secret org help=\"New\"\n";
  rig.sources.erase("Plain");
  std::ofstream(rig.dir / "corrupt.json") << "not json";
  rig.service.dropStale();
  check(!rig.service.has("Assistant", "key") && rig.service.has("Assistant", "org") && rig.files() == 1,
        "at start changed hosts drop a value, new words do not, and an unreadable file goes");
  rig.sources.erase("Assistant");
  rig.service.dropStale();
  check(rig.files() == 0, "at start the values of an app that is gone go");

  rig.sources["Assistant"] = kAssistant;
  rig.service.sourceSaved("Assistant");
  check(rig.set("Assistant", "key", kValue) && rig.service.eraseAll() && !fs::exists(rig.dir) &&
            !rig.service.has("Assistant", "key"),
        "eraseAll removes the store");
}

void fillsHeaders() {
  Rig rig;
  check(rig.set("Assistant", "key", kValue), "a value");
  const std::string api = "https://api.test/v1/chat";

  RequestHeaders headers{{"Accept", "application/json"}, {"Authorization", "Bearer {secret:key}"}};
  check(rig.fill(api, headers) == RequestFill::Filled && headers[0].second == "application/json" &&
            headers[1].second == "Bearer " + kValue,
        "a placeholder is replaced, other headers stay");

  RequestHeaders plain{{"Accept", "application/json"}, {"{secret:key}", "x"}};
  const RequestHeaders before = plain;
  check(rig.fill(api, plain) == RequestFill::Unchanged && plain == before,
        "without a placeholder in a value nothing changes, header names are never replaced");

  std::string error;
  check(rig.set("Assistant", "org", kOrg, &error), "a second value");
  RequestHeaders many{{"X-Both", "{secret:key}:{secret:org}:{secret:key}"}, {"X-Org", "{secret:org}"}};
  check(rig.fill(api, many) == RequestFill::Filled && many[0].second == kValue + ":" + kOrg + ":" + kValue &&
            many[1].second == kOrg,
        "several placeholders and several keys");
  RequestHeaders anyHost{{"X-Org", "{secret:org}"}};
  check(rig.fill("https://other.test/", anyHost) == RequestFill::Filled && anyHost[0].second == kOrg,
        "a key without hosts= goes to any https host");
}

void plainHttpOnlyToPrivateTargets() {
  Rig rig;
  rig.sources["Local"] = "# @secret open\n# @secret listed hosts=192.168.1.5,printer.local\n";
  std::string error;
  check(rig.service.set("Local", {{"open", kValue}, {"listed", kOrg}}, error), "two values");

  const auto fills = [&](const std::string& url, const std::string& key) {
    RequestHeaders headers{{"X-Key", "{secret:" + key + "}"}};
    return rig.fill(url, headers, "Local");
  };
  for (const char* url : {"http://192.168.1.5/x", "http://10.0.0.1:8080/x", "http://127.0.0.1/x", "http://172.16.0.1/",
                          "http://169.254.1.1/", "http://printer.local/", "http://[::1]/", "http://[fd00::1]:80/x",
                          "http://[fe80::1]/"})
    check(fills(url, "open") == RequestFill::Filled, std::string("plain http to a private target: ") + url);
  for (const char* url : {"http://8.8.8.8/x", "http://example.com/x", "http://172.32.0.1/", "http://172.15.0.1/",
                          "http://192.168.1.1.evil.com/", "http://0xC0A80001/", "http://3232235777/",
                          "http://010.0.0.1/", "http://printer.local.evil.com/", "http://[2001:db8::1]/",
                          "http://user@192.168.1.5/"})
    check(fills(url, "open") == RequestFill::Refused, std::string("plain http to anything else: ") + url);
  check(fills("https://8.8.8.8/x", "open") == RequestFill::Filled, "https may go anywhere without hosts=");

  check(fills("http://192.168.1.5/x", "listed") == RequestFill::Filled, "plain http to a listed private host");
  check(fills("http://PRINTER.local/x", "listed") == RequestFill::Filled, "the list ignores case");
  check(fills("https://printer.local/x", "listed") == RequestFill::Filled, "https to a listed host");
  check(fills("http://192.168.1.6/x", "listed") == RequestFill::Refused, "hosts= also holds for private targets");

  rig.sources["Public"] = "# @secret key hosts=api.test\n";
  check(rig.set("Public", "key", kValue), "a value for a public host");
  RequestHeaders headers{{"X-Key", "{secret:key}"}};
  check(rig.fill("http://api.test/x", headers, "Public") == RequestFill::Refused,
        "hosts= does not open plain http to a public host");
}

void fillsTheUrl() {
  Rig rig;
  const std::string tricky = "a&b=c/d e+f?#%";
  const std::string encoded = "a%26b%3Dc%2Fd%20e%2Bf%3F%23%25";
  std::string error;
  check(rig.service.set("Assistant", {{"org", tricky}, {"key", kValue}}, error), "two values");

  std::string url = "https://any.test/v1/{secret:org}/x?token={secret:org}&other=1";
  RequestHeaders none;
  check(rig.service.fill("Assistant", url, none) == RequestFill::Filled &&
            url == "https://any.test/v1/" + encoded + "/x?token=" + encoded + "&other=1",
        "path and query take the value percent-encoded: " + url);

  url = "https://api.test/q?key={secret:key}";
  RequestHeaders both{{"Authorization", "Bearer {secret:org}"}};
  check(rig.service.fill("Assistant", url, both) == RequestFill::Filled && url == "https://api.test/q?key=" + kValue &&
            both[0].second == "Bearer " + tricky,
        "URL and headers fill together, a header takes the value as it is");

  url = "https://any.test?token={secret:org}";
  check(rig.service.fill("Assistant", url, none) == RequestFill::Filled && url == "https://any.test?token=" + encoded,
        "a query without a path");

  url = "https://api.test/v1?plain=1";
  check(rig.service.fill("Assistant", url, none) == RequestFill::Unchanged && url == "https://api.test/v1?plain=1",
        "a URL without a placeholder stays");

  logbuf::setVerbose(true);
  const uint32_t mark = logMark();
  for (const std::string& refused :
       {std::string("https://{secret:key}/v1"), std::string("https://{secret:key}.api.test/v1"),
        std::string("https://api.{secret:key}/v1"), std::string("https://api.test:{secret:key}/v1"),
        std::string("https://{secret:key}@api.test/v1"), std::string("https://u:{secret:key}@api.test/v1"),
        std::string("{secret:key}://api.test/v1"), std::string("http{secret:key}://api.test/v1"),
        std::string("https://api.test/v1#{secret:key}"), std::string("http://api.test/v1?k={secret:key}"),
        std::string("https://evil.test/v1?k={secret:key}"), std::string("https://api.test/{secret:nope}"),
        std::string("https://api.test/{secret:key"), std::string("https://user@api.test/{secret:key}")}) {
    url = refused;
    RequestHeaders kept{{"Authorization", "Bearer {secret:key}"}};
    check(rig.service.fill("Assistant", url, kept) == RequestFill::Refused, "refused: " + refused);
    check(url == refused && kept[0].second == "Bearer {secret:key}", "a refusal leaves URL and headers as they were");
  }
  const std::string log = logAfter(mark);
  logbuf::setVerbose(false);
  check(log.find(kValue) == std::string::npos && log.find("/v1") == std::string::npos &&
            log.find("{secret") == std::string::npos,
        "a refusal names neither a value nor the URL nor a placeholder: " + log);
}

void refusalsAreLogged() {
  Rig rig;
  std::string error;
  check(rig.set("Assistant", "key", kValue) && rig.set("Assistant", "org", kOrg, &error), "two values");
  const std::string api = "https://api.test/v1/chat";
  logbuf::setVerbose(true);
  const uint32_t mark = logMark();
  const auto refusal = [&](const std::string& url, const std::string& value, const std::string& app, const char* why) {
    RequestHeaders refused{{"Accept", "text/plain"}, {"Authorization", value}};
    const RequestHeaders kept = refused;
    check(rig.fill(url, refused, app) == RequestFill::Refused && refused == kept, std::string("refused: ") + why);
  };
  refusal("http://api.test/v1", "Bearer {secret:key}", "Assistant", "plain http");
  refusal("https://user@api.test/v1", "Bearer {secret:key}", "Assistant", "user information");
  refusal("https://evil.test/v1", "Bearer {secret:key}", "Assistant", "a host hosts= does not list");
  refusal(api, "Bearer {secret:nope}", "Assistant", "an undeclared key");
  refusal(api, "{secret:org} {secret:key", "Assistant", "a placeholder without its brace");
  refusal(api, "Bearer {secret:key}", "Plain", "an app without @secret");
  check(rig.set("Assistant", "org", "", &error), "org erased");
  refusal(api, "{secret:key}{secret:org}", "Assistant", "a declared key without a value");
  const std::string log = logAfter(mark);
  logbuf::setVerbose(false);
  check(log.find("secret: Assistant refused (http) for api.test") != std::string::npos &&
            log.find("secret: Assistant refused (host) for evil.test") != std::string::npos &&
            log.find("secret: Assistant refused (unknown) for api.test") != std::string::npos &&
            log.find("secret: Assistant refused (empty) for api.test") != std::string::npos &&
            log.find("secret: Plain refused (unknown) for api.test") != std::string::npos,
        "each refusal leaves a debug line with app, reason and host: " + log);
  check(log.find(kValue) == std::string::npos && log.find(kOrg) == std::string::npos &&
            log.find("/v1") == std::string::npos,
        "no log line holds a value or the full URL");
}

}

int main() {
  statusAndSet();
  refusedValues();
  bindingPerKey();
  fillsHeaders();
  plainHttpOnlyToPrivateTargets();
  fillsTheUrl();
  refusalsAreLogged();
  return awtrix::test::finish("linux-secret-service");
}
