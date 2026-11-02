#include "../support.h"
#include "../LogLines.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/linux/secrets/SecretService.h"
#include "platform/posix/Files.h"
#include "system/Log.h"

using namespace awtrix;
using awtrix::net::RequestBody;
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
    "# @secret org hosts=api.test,any.test\n"
    "class A end\n";

struct Rig {
  fs::path root = fs::temp_directory_path() / ("secrets-" + std::to_string(::getpid()));
  fs::path dir = root / "script-secrets";
  std::map<std::string, std::string> sources = {
      {"Assistant", kAssistant}, {"Plain", "class A end\n"}, {"Broken", "# @secret 1bad\n"},
      {"NoHosts", "# @secret key label=\"API key\"\n"}};
  bool sourcesReadable = true;
  secrets::Service service{dir.string(), [this](const std::string& app) -> std::optional<std::string> {
                             if (!sourcesReadable) return std::nullopt;
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
    RequestBody none;
    return service.fill(app, url, headers, none);
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
  check(s.keys[0].hosts == std::vector<std::string>{"api.test"} &&
            s.keys[1].hosts == std::vector<std::string>{"any.test", "api.test"},
        "and where they may go");
  check(!rig.service.status("Plain").declared && !rig.service.status("Missing").declared,
        "an app without @secret, and a missing one, declare nothing");
  s = rig.service.status("Broken");
  check(s.declared && s.invalid == "@secret: bad key" && s.keys.empty(), "a malformed line is declared and invalid");
  s = rig.service.status("NoHosts");
  check(s.declared && s.invalid == "@secret: hosts needs 1-8 hosts" && s.keys.empty(), "a line without hosts= is invalid");

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
  for (const std::string& bad : {kValue + "\r", kValue + "\n" + "X-Injected: 1", kValue + std::string(1, '\0'),
                                 kValue + "\t", kValue + "\x01", kValue + "\x1f", kValue + "\x7f"})
    check(!rig.set("Assistant", "key", bad, &error) && error == "invalid value", "control characters are refused");
  check(rig.set("Assistant", "key", "k\xC3\xA4y with space~"), "spaces, tildes and UTF-8 are fine");
  check(!rig.set("Assistant", "nope", kValue, &error) && error == "unknown key", "an undeclared key: " + error);
  check(!rig.set("Plain", "key", kValue, &error) && error == "no @secret line", "an app without @secret: " + error);
  check(!rig.set("Broken", "key", kValue, &error) && error == "@secret: bad key", "an invalid line: " + error);
  check(!rig.set("NoHosts", "key", kValue, &error) && error == "@secret: hosts needs 1-8 hosts",
        "a line without hosts= takes no key: " + error);
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
      "# @secret org label=\"Organisation\" help=\"For teams\" hosts=api.test,any.test\n";
  rig.service.sourceSaved("Assistant");
  check(rig.service.has("Assistant", "key") && rig.service.has("Assistant", "org"),
        "a new label or help keeps the value, and so does the host in another case");
  const secrets::Status renamed = rig.service.status("Assistant");
  check(renamed.keys.size() == 2 && renamed.keys[0].label == "Your key" && renamed.keys[1].help == "For teams",
        "the new words show");

  rig.sources["Assistant"] =
      "# @secret key hosts=api.test,other.test\n"
      "# @secret org hosts=api.test,any.test\n";
  rig.service.sourceSaved("Assistant");
  check(!rig.service.has("Assistant", "key") && rig.service.has("Assistant", "org"),
        "changed hosts= erase only that key's value");
  check(rig.set("Assistant", "key", kValue), "set again");
  rig.sources["Assistant"] =
      "# @secret key hosts=other.test,api.test,api.test\n"
      "# @secret org hosts=api.test,any.test\n";
  rig.service.sourceSaved("Assistant");
  check(rig.service.has("Assistant", "key"), "the same hosts in another order, with a repeat, are the same hosts");

  rig.sources["Assistant"] = "# @secret key hosts=other.test,api.test\n";
  rig.service.sourceSaved("Assistant");
  check(rig.service.has("Assistant", "key") && !rig.service.has("Assistant", "org"), "a key that is gone loses its value");
  rig.sources["Assistant"] = "# @secret key\n";
  rig.service.sourceSaved("Assistant");
  check(!rig.service.has("Assistant", "key") && rig.files() == 0, "a line without hosts= is invalid, so its value goes");

  rig.sources["Assistant"] = kAssistant;
  rig.service.sourceSaved("Assistant");
  check(rig.set("Assistant", "key", kValue) && rig.files() == 1, "set again");
  rig.service.sourceRemoved("Assistant");
  check(!rig.service.has("Assistant", "key") && rig.files() == 0, "a removed app loses every value");

  check(rig.set("Assistant", "key", kValue) && rig.set("Assistant", "org", kOrg), "set again");
  rig.sources["Assistant"] =
      "# @secret key label=\"Key\" hosts=other.test\n"
      "# @secret org help=\"New\" hosts=api.test,any.test\n";
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

void unreadableSourceKeepsValues() {
  Rig rig;
  check(rig.set("Assistant", "key", kValue), "a value");
  RequestHeaders headers = {{"X-Key", "{secret:key}"}};
  rig.sourcesReadable = false;
  rig.service.dropStale();
  check(rig.files() == 1, "a source that cannot be read at start keeps the values");
  rig.service.sourceSaved("Assistant");
  check(rig.files() == 1, "a source that cannot be read after a save keeps the values");
  check(rig.fill("https://api.test/x", headers) == RequestFill::Refused, "and nothing is sent while it is unknown");
  rig.sourcesReadable = true;
  check(rig.service.has("Assistant", "key"), "the next read finds the spec again, nothing was cached");

  rig.sources.erase("Assistant");
  rig.service.dropStale();
  check(rig.files() == 0, "a source that is confirmed gone drops the values");
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
  RequestHeaders second{{"X-Org", "{secret:org}"}};
  check(rig.fill("https://any.test/", second) == RequestFill::Filled && second[0].second == kOrg,
        "a key goes to every host of its line");
  RequestHeaders both{{"X-Org", "{secret:org}"}, {"X-Key", "{secret:key}"}};
  const RequestHeaders kept = both;
  check(rig.fill("https://any.test/", both) == RequestFill::Refused && both == kept,
        "one key that may not go there refuses the request");
}

void plainHttpOnlyToPrivateTargets() {
  Rig rig;
  rig.sources["Local"] =
      "# @secret near hosts=192.168.1.5,10.0.0.1,127.0.0.1,172.16.0.1,169.254.1.1,printer.local,[::1],[fd00::1]\n"
      "# @secret link hosts=[fe80::1]\n"
      "# @secret far hosts=8.8.8.8,example.com,172.32.0.1,172.15.0.1,192.168.1.1.evil.com,0xc0a80001,3232235777,010.0.0.1\n"
      "# @secret farther hosts=printer.local.evil.com,[2001:db8::1]\n";
  std::string error;
  check(rig.service.set("Local", {{"near", kValue}, {"link", kValue}, {"far", kValue}, {"farther", kValue}}, error),
        "four values");

  const auto fills = [&](const std::string& url, const std::string& key) {
    RequestHeaders headers{{"X-Key", "{secret:" + key + "}"}};
    return rig.fill(url, headers, "Local");
  };
  for (const char* url : {"http://192.168.1.5/x", "http://10.0.0.1:8080/x", "http://127.0.0.1/x", "http://172.16.0.1/",
                          "http://169.254.1.1/", "http://printer.local/", "http://[::1]/", "http://[fd00::1]:80/x"})
    check(fills(url, "near") == RequestFill::Filled, std::string("plain http to a private target: ") + url);
  check(fills("http://[fe80::1]/", "link") == RequestFill::Filled, "plain http to a link-local address");
  for (const char* url : {"http://8.8.8.8/x", "http://example.com/x", "http://172.32.0.1/", "http://172.15.0.1/",
                          "http://192.168.1.1.evil.com/", "http://0xC0A80001/", "http://3232235777/",
                          "http://010.0.0.1/"})
    check(fills(url, "far") == RequestFill::Refused, std::string("a listed host opens no plain http: ") + url);
  for (const char* url : {"http://printer.local.evil.com/", "http://[2001:db8::1]/"})
    check(fills(url, "farther") == RequestFill::Refused, std::string("a listed host opens no plain http: ") + url);
  check(fills("http://user@192.168.1.5/", "near") == RequestFill::Refused, "no user information");
  check(fills("https://8.8.8.8/x", "far") == RequestFill::Filled, "https to a listed public host");

  check(fills("http://PRINTER.local/x", "near") == RequestFill::Filled, "the list ignores case");
  check(fills("https://printer.local/x", "near") == RequestFill::Filled, "https to a listed private host");
  check(fills("http://192.168.1.6/x", "near") == RequestFill::Refused, "hosts= also holds for private targets");
  check(fills("https://other.test/x", "near") == RequestFill::Refused, "and for any other host");

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
  RequestBody noBody;
  check(rig.service.fill("Assistant", url, none, noBody) == RequestFill::Filled &&
            url == "https://any.test/v1/" + encoded + "/x?token=" + encoded + "&other=1",
        "path and query take the value percent-encoded: " + url);

  url = "https://api.test/q?key={secret:key}";
  RequestHeaders both{{"Authorization", "Bearer {secret:org}"}};
  check(rig.service.fill("Assistant", url, both, noBody) == RequestFill::Filled && url == "https://api.test/q?key=" + kValue &&
            both[0].second == "Bearer " + tricky,
        "URL and headers fill together, a header takes the value as it is");

  url = "https://any.test?token={secret:org}";
  check(rig.service.fill("Assistant", url, none, noBody) == RequestFill::Filled && url == "https://any.test?token=" + encoded,
        "a query without a path");

  url = "https://api.test/v1?plain=1";
  check(rig.service.fill("Assistant", url, none, noBody) == RequestFill::Unchanged && url == "https://api.test/v1?plain=1",
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
    check(rig.service.fill("Assistant", url, kept, noBody) == RequestFill::Refused, "refused: " + refused);
    check(url == refused && kept[0].second == "Bearer {secret:key}", "a refusal leaves URL and headers as they were");
  }
  const std::string log = logAfter(mark);
  logbuf::setVerbose(false);
  check(log.find(kValue) == std::string::npos && log.find("/v1") == std::string::npos &&
            log.find("{secret") == std::string::npos,
        "a refusal names neither a value nor the URL nor a placeholder: " + log);
}

void fillsTextBodies() {
  Rig rig;
  const std::string tricky = "a&b=c/d e+f\"";
  std::string error;
  check(rig.service.set("Assistant", {{"org", tricky}, {"key", kValue}}, error), "two values");
  const std::string api = "https://api.test/v1/chat";

  std::string url = api;
  RequestHeaders none;
  RequestBody json(std::string("{\"k\":\"{secret:key}\",\"o\":\"{secret:org}\",\"again\":\"{secret:key}\"}"));
  check(rig.service.fill("Assistant", url, none, json) == RequestFill::Filled && url == api &&
            json.head == "{\"k\":\"" + kValue + "\",\"o\":\"" + tricky + "\",\"again\":\"" + kValue + "\"}",
        "a text body takes the values as they are: " + json.head);

  url = "https://api.test/q?k={secret:key}";
  RequestHeaders header{{"X-Key", "{secret:key}"}};
  RequestBody all(std::string("{secret:key}"));
  check(rig.service.fill("Assistant", url, header, all) == RequestFill::Filled &&
            url == "https://api.test/q?k=" + kValue && header[0].second == kValue && all.head == kValue,
        "URL, header and body fill together");

  url = "https://evil.test/v1";
  const std::string plainText = "{ no placeholder {secret";
  RequestBody plain(plainText);
  check(rig.service.fill("Assistant", url, none, plain) == RequestFill::Unchanged && plain.head == plainText,
        "a body without a placeholder stays, wherever it goes");

  const fs::path clip = rig.root / "clip";
  std::ofstream(clip) << "audio";
  const auto mapped = std::make_shared<const posix::MappedFile>(clip.string(), 100);
  RequestBody form("{secret:key}", mapped, "{secret:key}");
  check(!form.fillable() && rig.service.fill("Assistant", url, none, form) == RequestFill::Unchanged &&
            form.head == "{secret:key}" && form.tail == "{secret:key}",
        "a clip stays as it is, form fields included");
  check(RequestBody(std::string("x{secret:key}")).fillable() && !RequestBody(plainText).fillable(),
        "only a text body with a placeholder can be filled");

  logbuf::setVerbose(true);
  const uint32_t mark = logMark();
  const auto refused = [&](const std::string& target, const std::string& text, const std::string& value) {
    std::string kept = target;
    RequestHeaders headers{{"X-Key", value}};
    RequestBody body(text);
    check(rig.service.fill("Assistant", kept, headers, body) == RequestFill::Refused && kept == target &&
              headers[0].second == value && body.head == text,
          "a refusal leaves URL, headers and body as they were: " + target + " " + text + " " + value);
  };
  refused(api, "{secret:nope}", "plain");
  refused(api, "{secret:key", "plain");
  refused("https://evil.test/v1", "{secret:key}", "plain");
  refused("http://api.test/v1", "{secret:key}", "plain");
  refused(api, "{secret:key}", "{secret:nope}");
  refused(api, "{secret:nope}", "{secret:key}");
  refused("https://api.test/{secret:nope}", "{secret:key}", "{secret:key}");
  refused("https://api.test/{secret:key}", "{secret:key}{secret:nope}", "{secret:key}");
  check(rig.set("Assistant", "org", "", &error), "org erased");
  refused(api, "{secret:key}{secret:org}", "plain");
  const std::string log = logAfter(mark);
  logbuf::setVerbose(false);
  check(log.find(kValue) == std::string::npos && log.find("{secret") == std::string::npos,
        "no log line holds a value or the body: " + log);
}

void aFilledTextGrowsOnlySoFar() {
  Rig rig;
  const std::string value(secrets::Service::kMaxValue, 'v');
  const std::string marked = "{secret:key}";
  check(rig.set("Assistant", "key", value), "a long value");
  const std::size_t most = net::kMaxFillGrowth / (value.size() - marked.size());
  std::string text;
  for (std::size_t i = 0; i < most; ++i) text += marked;
  const std::string api = "https://api.test/v1/chat";

  std::string url = api;
  RequestHeaders none;
  RequestBody fits(text);
  check(rig.service.fill("Assistant", url, none, fits) == RequestFill::Filled && fits.head.size() == most * value.size(),
        "a body may grow up to the limit");
  logbuf::setVerbose(true);
  const uint32_t mark = logMark();
  RequestBody over(text + marked);
  check(rig.service.fill("Assistant", url, none, over) == RequestFill::Refused && over.head == text + marked,
        "a placeholder more is refused and leaves the body");
  RequestHeaders header{{"X-Key", text + marked}};
  RequestBody noBody;
  check(rig.service.fill("Assistant", url, header, noBody) == RequestFill::Refused && header[0].second == text + marked,
        "the same limit holds for a header");
  const std::string log = logAfter(mark);
  logbuf::setVerbose(false);
  check(log.find("secret: Assistant refused (size) for api.test") != std::string::npos, "and is logged: " + log);
}

void allowsSharesTheRuleOfTheFill() {
  Rig rig;
  rig.sources["Local"] = "# @secret near hosts=192.168.1.5,api.test\n";
  std::string error;
  check(rig.set("Assistant", "key", kValue) && rig.service.set("Local", {{"near", kOrg}}, error), "two values");
  const auto allowed = [&](const std::string& key, const std::string& url, const std::string& app = "Assistant") {
    return rig.service.allows(app, key, url);
  };
  check(allowed("key", "https://api.test/v1"), "a set key to its host");
  check(allowed("key", "https://API.test:443/v1?a=b"), "the host in another case, with a port and a query");
  check(allowed("key", "https://api.test/v1?k={secret:key}"), "a placeholder in the query");
  check(!allowed("org", "https://api.test/v1"), "a declared key without a value");
  check(!allowed("nope", "https://api.test/v1"), "an undeclared key");
  check(!allowed("key", "https://evil.test/v1"), "another host");
  check(!allowed("key", "https://api.test.evil.test/v1"), "a suffix trick");
  check(!allowed("key", "https://user@api.test/v1"), "user information");
  check(!allowed("key", "http://api.test/v1"), "plain http to a public host");
  check(!allowed("key", "ftp://api.test/v1"), "another scheme");
  check(!allowed("key", "not a url"), "no URL");
  check(!allowed("key", ""), "an empty URL");
  check(!allowed("key", "https://{secret:key}/v1"), "a placeholder in the host");
  check(!allowed("key", "https://api.test/v1#{secret:key}"), "a placeholder after the #");
  check(!allowed("", "https://api.test/v1"), "an empty key");
  check(allowed("near", "http://192.168.1.5/x", "Local"), "plain http to a listed private host");
  check(!allowed("near", "http://192.168.1.6/x", "Local"), "plain http to another private host");
  check(!allowed("near", "http://api.test/x", "Local"), "plain http to a listed public host");
  check(allowed("near", "https://api.test/x", "Local"), "https to the same host");
  check(!allowed("key", "https://api.test/v1", "Local"), "a key of another app");
  check(!allowed("key", "https://api.test/v1", "Plain"), "an app without @secret");
  check(!allowed("key", "https://api.test/v1", "Broken"), "an app with an invalid line");
  check(!allowed("key", "https://api.test/v1", "Missing"), "an app that is not there");

  for (const std::string& url : {std::string("https://api.test/v1"), std::string("https://evil.test/v1"),
                                 std::string("http://api.test/v1"), std::string("https://api.test/v1#{secret:key}")}) {
    RequestHeaders headers{{"X-Key", "{secret:key}"}};
    std::string filledUrl = url;
    RequestBody none;
    check(allowed("key", url) == (rig.service.fill("Assistant", filledUrl, headers, none) == RequestFill::Filled),
          "allows answers what the fill does: " + url);
  }
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

void aFailedEraseLeavesNoStoreBehind() {
  struct Store final : script::PerScriptStore {
    explicit Store(bool works) : works(works) {}
    void dropStale() override {}
    void sourceSaved(const std::string&) override {}
    void sourceRemoved(const std::string&) override {}
    bool eraseAll() override {
      ++erases;
      return works;
    }
    bool works;
    int erases = 0;
  };
  Store failing(false), next(true);
  script::PerScriptStores stores;
  stores.add(failing);
  stores.add(next);
  check(!stores.eraseAll() && next.erases == 1, "a store that cannot erase is reported, and the next one still erases");
}

}

int main() {
  aFailedEraseLeavesNoStoreBehind();
  statusAndSet();
  unreadableSourceKeepsValues();
  refusedValues();
  bindingPerKey();
  fillsHeaders();
  plainHttpOnlyToPrivateTargets();
  fillsTheUrl();
  fillsTextBodies();
  aFilledTextGrowsOnlySoFar();
  allowsSharesTheRuleOfTheFill();
  refusalsAreLogged();
  return awtrix::test::finish("linux-secret-service");
}
