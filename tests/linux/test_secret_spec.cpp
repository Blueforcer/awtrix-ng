#include "../support.h"

#include <string>

#include "platform/linux/secrets/SecretSpec.h"

using namespace awtrix;
using awtrix::test::check;

namespace {

std::string refusedFor(const secrets::Secret& secret, const std::string& url) {
  const char* reason = "unset";
  if (secrets::allows(secret, url, &reason)) return reason ? "allowed, reason set" : "allowed";
  return reason ? reason : "refused without a reason";
}

bool refused(const std::string& line, const std::string& expected) {
  std::string error;
  return !secrets::parseSpec("# @secret " + line + "\n", &error) && error == expected;
}

}

int main() {
  std::string error = "left over";
  const auto spec = secrets::parseSpec(
      "# @name Assistant\n"
      "# @secret key label=\"API key\" help=\"Your OpenAI API key\" hosts=api.openai.com,API.Groq.com\n"
      "# @secret org hosts=org.test\n"
      "class App end\n",
      &error);
  check(spec.has_value() && error.empty(), "the header parses: " + error);
  check(spec && spec->secrets.size() == 2, "one secret per line");
  const secrets::Secret* key = spec ? spec->find("key") : nullptr;
  check(key && key->label == "API key" && key->help == "Your OpenAI API key", "quoted label and help");
  check(key && key->hosts == std::vector<std::string>{"api.groq.com", "api.openai.com"},
        "hosts in lower case and sorted");
  const secrets::Secret* org = spec ? spec->find("org") : nullptr;
  check(org && org->label.empty() && org->help.empty() && org->hosts == std::vector<std::string>{"org.test"},
        "a key and its hosts are a whole line");
  const auto repeated = secrets::parseSpec("# @secret k hosts=b.test,A.test,a.test\n");
  check(repeated && repeated->secrets[0].hosts == std::vector<std::string>{"a.test", "b.test"}, "a repeat counts once");
  check(spec && !spec->find("Key") && !spec->find("missing"), "keys are found as written");

  const auto upper = secrets::parseSpec("# @SECRET Token LABEL=Name Help=\"two words\" HOSTS=a.test\n");
  check(upper && upper->secrets.size() == 1 && upper->secrets[0].key == "Token" &&
            upper->secrets[0].label == "Name" && upper->secrets[0].help == "two words" &&
            upper->secrets[0].hosts == std::vector<std::string>{"a.test"},
        "tag and attribute names ignore case, the key keeps its case");

  error = "left over";
  check(!secrets::parseSpec("# @name x\n# @config city text \"City\"\nclass A end\n", &error) && error.empty(),
        "without an @secret line: nothing and no error");
  check(!secrets::parseSpec("class A end\n# @secret key\n", &error) && error.empty(),
        "a line after the code is no header line");

  const std::string four = "# @secret a hosts=t\n# @secret b hosts=t\n# @secret c hosts=t\n# @secret d hosts=t\n";
  check(secrets::parseSpec(four).has_value(), "four lines");
  check(!secrets::parseSpec(four + "# @secret e hosts=t\n", &error) && error == "@secret: at most 4 lines",
        "a fifth line: " + error);
  check(!secrets::parseSpec("# @secret a hosts=t\n# @secret a label=x hosts=t\n", &error) &&
            error == "@secret: key declared twice",
        "a key twice: " + error);

  for (const char* good : {"_key", "a", "Key2", "abcdefghijklmnopqrstuvwx", "_0"})
    check(secrets::parseSpec(std::string("# @secret ") + good + " hosts=t\n").has_value(), std::string("key ") + good);
  for (const char* bad : {"", "1key", "abcdefghijklmnopqrstuvwxy", "my-key", "key.x", "k\xc3\xa9y", "label=x",
                          "\"quoted key\""})
    check(refused(bad, "@secret: bad key"), std::string("bad key ") + bad);

  check(refused("key colour=red", "@secret: unknown attribute"), "an unknown attribute");
  check(refused("key label", "@secret: unknown attribute"), "an attribute without a value");
  check(refused("key other", "@secret: unknown attribute"), "a second bare word");
  check(refused("key label=\"open", "@secret: unterminated quote"), "an open quote");
  check(secrets::parseSpec("# @secret key label=\"" + std::string(48, 'l') + "\" help=\"" + std::string(96, 'h') +
                           "\" hosts=t\n")
            .has_value(),
        "label and help at their limits");
  check(refused("key label=\"" + std::string(49, 'l') + "\"", "@secret: label too long"), "a label too long");
  check(refused("key help=\"" + std::string(97, 'h') + "\"", "@secret: help too long"), "a help too long");

  for (const char* host : {"a.test/x", "a.test:443", "user@a.test", "\"a.test b.test\"", "a.test?x", "a.test#x",
                           "b\xc3\xa4" "d.test", "[fd00::1", "[zz::1]", "[]", "fd00::1"})
    check(refused(std::string("key hosts=") + host, "@secret: bad host"), std::string("bad host ") + host);
  for (const char* line : {"key", "key label=x help=y", "key hosts=", "key hosts=,"})
    check(refused(line, "@secret: hosts needs 1-8 hosts"), std::string("no host: ") + line);
  check(secrets::parseSpec("# @secret key hosts=[fd00::1],[::1]\n").has_value(), "an IPv6 address in brackets");
  check(secrets::parseSpec("# @secret key hosts=1.t,2.t,3.t,4.t,5.t,6.t,7.t,8.t\n").has_value(), "eight hosts");
  check(refused("key hosts=1.t,2.t,3.t,4.t,5.t,6.t,7.t,8.t,9.t", "@secret: hosts needs 1-8 hosts"), "nine hosts");

  const auto local = secrets::parseSpec("# @secret local hosts=192.168.1.5,printer.local,[::1]\n");
  if (key && local) {
    const secrets::Secret& near = local->secrets[0];
    check(refusedFor(*key, "https://api.openai.com/v1/chat/completions") == "allowed", "a listed host");
    check(refusedFor(*key, "https://API.OpenAI.com/v1") == "allowed", "the host match ignores case");
    check(refusedFor(*key, "https://api.groq.com:443/v1") == "allowed", "the second host, with a port");
    check(refusedFor(*key, "http://api.openai.com/v1") == "http", "plain http");
    check(refusedFor(*key, "ftp://api.openai.com/") == "http", "another scheme");
    check(refusedFor(*key, "https://evil.example/?u=https://api.openai.com") == "host", "another host");
    check(refusedFor(*key, "https://api.openai.com.evil.example/") == "host", "a suffix trick");
    check(refusedFor(*key, "https://user:pass@api.openai.com/") == "host", "user information");
    check(refusedFor(near, "http://192.168.1.5:8080/x") == "allowed", "plain http to a listed private address");
    check(refusedFor(near, "http://printer.local/x") == "allowed", "plain http to a listed .local name");
    check(refusedFor(near, "http://[::1]/x") == "allowed", "plain http to a listed IPv6 loopback");
    check(refusedFor(near, "https://printer.local/x") == "allowed", "https to a listed private host");
    check(refusedFor(near, "http://user@192.168.1.5/x") == "host", "plain http with user information");
    check(refusedFor(near, "http://10.0.0.1/x") == "host", "hosts= holds for a private address too");
    check(refusedFor(*key, "http://192.168.1.5/x") == "host", "a private address that is not listed");
    check(refusedFor(secrets::Secret{}, "https://any.example/x") == "host", "without a host nothing is allowed");
  }
  return awtrix::test::finish("linux-secret-spec");
}
