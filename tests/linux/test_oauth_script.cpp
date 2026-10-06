#include "../support.h"
// The oauth script module against a real script host.
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/linux/oauth/OAuthScripting.h"
#include "OAuthTestSupport.h"

using namespace awtrix;
namespace fs = std::filesystem;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

const std::string kHeader = "# @oauth authorize=https://a.test/auth token=https://a.test/token api=api.test\n";

struct Rig {
  fs::path root = fs::temp_directory_path() / ("oauth-script-" + std::to_string(::getpid()));
  oauth::Vault vault{(root / "p").string()};
  std::string source;
  oauth::Service service{vault,
                         [](const oauth::HttpCall& c) {
                           return oauth::HttpReply{200, "{\"auth\":\"" + oauth_test::headerOf(c, "Authorization") + "\"}"};
                         },
                         [this](const std::string&) { return source; }, [] { return 0LL; }};
  oauth::OAuthScripting ext{service};
  script::ScriptServices services;
  AppRegistry registry;
  std::unique_ptr<script::ScriptHost> host;
  std::unique_ptr<script::ExtensionHost> extensions;
  Rig() {
    services.monotonicMs = [] { return 0L; };
    host = std::make_unique<script::ScriptHost>(registry, services, nullptr, nullptr);
    extensions = std::make_unique<script::ExtensionHost>(*host,
        std::vector<script::ScriptExtension*>{&ext});
    service.begin();
  }
  ~Rig() {
    extensions.reset();
    host.reset();
    service.stop();
    fs::remove_all(root);
  }
  bool install(const std::string& body) {
    source = kHeader + "import oauth\nclass App\nvar got\ndef draw() end\n" + body + "\nend\nreturn App()";
    return host->set("S", source);
  }
  std::string probe() {
    auto* app = static_cast<script::ScriptApp*>(registry.find("S"));
    std::string out;
    return app && app->callCheckForTest(out) ? out : "<no check>";
  }
  void tick() {
    RenderCtx ctx;
    host->tick(ctx, "S");
  }
};

}

int main() {
  Rig r;
  check(r.install("def check() return str(oauth.ready()) end"), "installs");
  check(r.probe() == "false", "not ready before sign-in");

  const auto spec = oauth::parseSpec(kHeader);
  check(spec.has_value(), "header parses");
  if (!spec) return 1;
  oauth::Record rec;
  rec.binding = oauth::sha256Hex("S\n" + spec->line);
  rec.clientId = "cid";
  rec.accessToken = "LONG";
  check(r.vault.save("S", rec), "record stored");
  r.service.forget("S");
  check(r.probe() == "true", "ready with a stored token");

  check(r.install("def setup() oauth.get('https://api.test/me', def (b, s) self.got = str(s) + ' ' + b end) end\n"
                  "def check() return str(self.got) end"),
        "request app installs");
  for (int i = 0; i < 200 && r.probe() == "nil"; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    r.tick();
  }
  check(r.probe() == "200 {\"auth\":\"Bearer LONG\"}", "callback gets status and body");

  check(r.install("def check() return str(oauth.get('https://evil.test/', def (b, s) end)) end"), "installs");
  check(r.probe() == "false", "an unlisted host is refused at once");

  check(r.install("def check() try oauth.get('https://api.test/', nil) return 'no' except 'value_error' return "
                  "'raised' end end"),
        "installs");
  check(r.probe() == "raised", "a callback is required");

  check(r.install("def check() try oauth.get('https://api.test/', def (b, s) end, {'headers': {'X': 1}}) "
                  "return 'no' except 'value_error' return 'raised' end end"),
        "installs");
  check(r.probe() == "raised", "header values must be strings");

  check(r.install("def check() import global return str(global.contains('_oauth_token')) end"), "installs");
  check(r.probe() == "false", "no token getter exists");

  check(r.install("def setup() import global global._native_app = def () return 'Other' end\n"
                  "oauth.get('https://api.test/me', def (b, s) self.got = str(s) end) end\n"
                  "def check() return str(self.got) end"),
        "an app that replaces _native_app installs");
  for (int i = 0; i < 200 && r.probe() == "nil"; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    r.tick();
  }
  check(r.probe() == "200", "the module still files and finds its callbacks under the calling app");
  if (failures == 0) std::puts("ok");
  return failures == 0 ? 0 : 1;
}
