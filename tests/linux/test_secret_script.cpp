#include "../support.h"

#include <unistd.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include "core/apps/AppRegistry.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/linux/secrets/SecretScripting.h"

using namespace awtrix;
using awtrix::test::check;
namespace fs = std::filesystem;

namespace {

const std::string kHeader = "# @secret key label=\"API key\"\n# @secret org\n";

struct Rig {
  fs::path root = fs::temp_directory_path() / ("secret-script-" + std::to_string(::getpid()));
  std::map<std::string, std::string> sources;
  posix::PrivateStore store{(root / "s").string(), secrets::Service::kMaxRecord};
  secrets::Service service{store, [this](const std::string& app) {
                             const auto it = sources.find(app);
                             return it == sources.end() ? std::string() : it->second;
                           }};
  secrets::SecretScripting ext{service};
  script::ScriptServices services;
  AppRegistry registry;
  std::unique_ptr<script::ScriptHost> host;
  std::unique_ptr<script::ExtensionHost> extensions;

  Rig() {
    services.monotonicMs = [] { return 0L; };
    host = std::make_unique<script::ScriptHost>(registry, services, nullptr, nullptr);
    extensions = std::make_unique<script::ExtensionHost>(*host, std::vector<script::ScriptExtension*>{&ext});
  }
  ~Rig() {
    extensions.reset();
    host.reset();
    fs::remove_all(root);
  }
  bool install(const std::string& name, const std::string& header, const std::string& body) {
    sources[name] = header + "import secret\nimport global\nclass App\ndef draw() end\n" + body + "\nend\nreturn App()";
    return host->set(name, sources[name]) && host->errorOf(name).empty();
  }
  std::string probe(const std::string& name) {
    auto* app = static_cast<script::ScriptApp*>(registry.find(name));
    std::string out;
    return app && app->callCheckForTest(out) ? out : "<no check>";
  }
};

}

int main() {
  Rig r;
  const std::string answers =
      "def check() return str(secret.has('key')) + ' ' + str(secret.has('org')) + ' ' + str(secret.has('nope')) + "
      "' ' + str(secret.has(7)) + ' ' + str(secret.has()) end";
  check(r.install("S", kHeader, answers), "an app with @secret lines installs");
  check(r.probe("S") == "false false false false false", "nothing set yet: " + r.probe("S"));

  std::string error;
  check(r.service.set("S", {{"key", "sk-VALUE"}}, error), "the owner sets a key");
  check(r.probe("S") == "true false false false false",
        "has() is true for the set key only, false for anything else: " + r.probe("S"));

  check(r.install("T", kHeader, answers), "a second app with the same keys installs");
  check(r.probe("T") == "false false false false false", "keys belong to their app: " + r.probe("T"));
  check(r.install("U", "", answers), "an app without @secret installs");
  check(r.probe("U") == "false false false false false", "an app without @secret has no key: " + r.probe("U"));

  check(r.install("S", kHeader,
                  "def setup() global.outside = def (a, b) global.seen = secret.has('key') end end\n"
                  "def check() return str(secret.has('key')) + ' ' + str(global.seen) end"),
        "an app that leaves a function to call outside any app installs");
  r.extensions->call("outside", "", "");
  check(r.probe("S") == "true false", "outside an app has() is false: " + r.probe("S"));
  return awtrix::test::finish("linux-secret-script");
}
