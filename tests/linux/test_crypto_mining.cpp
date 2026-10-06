#include "../support.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <string>
#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/linux/script/CryptoScripting.h"

namespace {
int& failures = awtrix::test::failures();
using awtrix::test::check;
long now = 0;
struct ScriptRig {
  awtrix::linux_script::CryptoScripting crypto{[] { return now; }};
  awtrix::script::ScriptServices services;
  awtrix::AppRegistry registry;
  std::unique_ptr<awtrix::script::ScriptHost> host;
  std::unique_ptr<awtrix::script::ExtensionHost> extensions;
  ScriptRig() {
    now = 0;
    services.monotonicMs = [] { return now; };
    services.log = [](const std::string& line) { std::printf("%s\n", line.c_str()); };
    host = std::make_unique<awtrix::script::ScriptHost>(registry, services, nullptr, nullptr);
    extensions = std::make_unique<awtrix::script::ExtensionHost>(*host,
        std::vector<awtrix::script::ScriptExtension*>{&crypto});
  }
  std::string probe(const char* name) {
    auto* app = static_cast<awtrix::script::ScriptApp*>(registry.find(name));
    std::string out;
    return app && app->callCheckForTest(out) ? out : "<no check>";
  }
  void tick(const char* current) { awtrix::RenderCtx ctx; host->tick(ctx, current); }
};
std::string mineApp(const std::string& body) { return "import crypto\nclass App\n" + body + "\ndef draw() end\nend\nreturn App()"; }

const char* kGenesisBerry =
    "bytes('0100000000000000000000000000000000000000000000000000000000000000000000003ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a29ab5f49ffff001d00000000')";

void scriptFindsAHit() {
  ScriptRig r;
  check(r.host->set("M", mineApp(std::string("var hits, done\n"
      "def init() self.hits = [] self.done = false end\n"
      "def setup() crypto.mine(") + kGenesisBerry + ", crypto.target(1.0 / 65536), def (n, h) "
      "if n == nil self.done = true else self.hits.push(n) end end, {'threads': 2}) end\n"
      "def check() return str(size(self.hits) > 0) + ',' + str(self.done) + ',' + str(crypto.mine_threads() > 0) end")),
        "a mining script installs");
  for (int i = 0; i < 400 && r.probe("M").rfind("true,", 0) != 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    r.tick("M");
  }
  check(r.probe("M").rfind("true,", 0) == 0, "hits arrive as nonce strings");
}

void secondAppIsBusy() {
  ScriptRig r;
  const std::string miner = mineApp(std::string("def setup() crypto.mine(") + kGenesisBerry +
                                   ", bytes('0000000000000000000000000000000000000000000000000000000000000000'), def (n, h) end) end\ndef check() return 'm' end");
  r.host->set("A", miner);
  r.host->set("B", mineApp(std::string("var ok\ndef setup() self.ok = crypto.mine(") + kGenesisBerry +
                          ", bytes('0000000000000000000000000000000000000000000000000000000000000000'), def (n, h) end) end\ndef check() return str(self.ok) end"));
  check(r.probe("B") == "false", "a second app cannot take the scanner");
  r.host->remove("A");
  check(!r.crypto.miner().scanner().running(), "removing the owner stops mining");
}

void leaseLapses() {
  ScriptRig r;
  r.host->set("A", mineApp(std::string("def setup() crypto.mine(") + kGenesisBerry +
                          ", bytes('0000000000000000000000000000000000000000000000000000000000000000'), def (n, h) end) end\ndef check() return 'm' end"));
  check(r.crypto.miner().scanner().running(), "mining runs");
  now += 6000;
  r.tick("A");
  check(!r.crypto.miner().scanner().running(), "six seconds without a mining call stop it");
}

void badSizesRaise() {
  ScriptRig r;
  r.host->set("A", mineApp("def check() try crypto.mine(bytes('00'), bytes('0000000000000000000000000000000000000000000000000000000000000000'), def (n, h) end) "
                          "return 'no' except 'value_error' return 'raised' end end"));
  check(r.probe("A") == "raised", "a short header raises value_error");
}

void stoppedOwnerReleases() {
  ScriptRig r;
  const std::string app = mineApp(std::string("var ok\ndef setup() self.ok = crypto.mine(") + kGenesisBerry +
      ", crypto.target(1.0), def(n,h) end, {'threads':1}) end\ndef check() return str(self.ok) end");
  check(r.host->set("A", app), "owner installs");
  check(r.host->set("B", mineApp("def check() crypto.mine_stop() return 'ok' end")), "observer installs");
  r.probe("B");
  check(r.crypto.miner().scanner().running(), "another app cannot stop the owner");
  check(r.host->set("A", mineApp("def setup() crypto.mine_stop() end")), "owner removal releases");
  check(r.host->set("B", app) && r.probe("B") == "true", "another app can acquire after release");
}

void allFunctionsRenewLease() {
  for (const char* function : {"crypto.mine_threads()", "crypto.target(1.0)", "crypto.difficulty(bytes('ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff'))"}) {
    ScriptRig r;
    r.host->set("A", mineApp(std::string("def setup() crypto.mine(") + kGenesisBerry +
      ", crypto.target(1.0), def(n,h) end, {'threads':1}) end\ndef check() " + function + " return 'ok' end"));
    now = 4000;
    r.probe("A");
    now = 5000;
    r.tick("A");
    check(r.crypto.miner().scanner().running(), "utility calls renew the owner's lease");
    now = 9000;
    r.tick("A");
    check(!r.crypto.miner().scanner().running(), "lease expires at exactly five seconds");
  }
}


void explicitStopReleases() {
  ScriptRig r;
  const std::string begin = std::string("crypto.mine(") + kGenesisBerry +
      ", crypto.target(1.0), def(n,h) end, {'threads':1})";
  check(r.host->set("A", mineApp("def setup() " + begin + " end\ndef check() crypto.mine_stop() return str(crypto.mine_rate()) end")),
        "explicit-stop owner installs");
  check(r.probe("A") == "0", "an explicitly stopped owner has zero rate");
  check(!r.crypto.miner().scanner().running(), "explicit stop joins workers");
  check(r.host->set("B", mineApp("var ok\ndef setup() self.ok = " + begin + " end\ndef check() return str(self.ok) end")),
        "next owner installs");
  check(r.probe("B") == "true", "explicit stop makes ownership available immediately");
}

void invalidInputsRaise() {
  for (const std::string& expression : {
      std::string("crypto.target(0)"), std::string("crypto.target(-1)"), std::string("crypto.target('1')"),
      std::string("crypto.difficulty(bytes('00'))"),
      std::string("crypto.mine(") + kGenesisBerry + ", bytes('00'), def(n,h) end)",
      std::string("crypto.mine(") + kGenesisBerry + ", crypto.target(1.0), 42)"}) {
    ScriptRig r;
    check(r.host->set("A", mineApp("def check() try " + expression +
          " return 'no' except 'value_error' return 'raised' end end")), "invalid-input probe installs");
    check(r.probe("A") == "raised", "invalid inputs raise value_error");
    check(!r.crypto.miner().scanner().running(), "invalid inputs never start workers");
  }
}

void observerCannotKeepLeaseAlive() {
  ScriptRig r;
  r.host->set("A", mineApp(std::string("def setup() crypto.mine(") + kGenesisBerry +
      ", crypto.target(1.0), def(n,h) end, {'threads':1}) end"));
  r.host->set("B", mineApp("def check() crypto.mine_rate() crypto.mine_threads() return str(crypto.mine_hashes()) end"));
  now = 4000;
  check(r.probe("B") == "0", "observer cannot read the owner's hash count");
  now = 5000;
  r.tick("B");
  check(!r.crypto.miner().scanner().running(), "observer calls cannot renew the owner's lease");
}

void callbackReplacementIsIsolated() {
  ScriptRig r;
  const std::string all = "bytes('ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff')";
  r.host->set("A", mineApp(std::string("var hits\ndef init() self.hits = 0 end\ndef setup() crypto.mine(") +
      kGenesisBerry + ", " + all + ", def(n,h) self.hits += 1 crypto.mine_stop() end, {'threads':1}) end\n"
      "def check() return str(self.hits) end"));
  r.host->set("B", mineApp(std::string("var hits, ok\ndef init() self.hits = 0 end\ndef setup() self.ok = crypto.mine(") +
      kGenesisBerry + ", " + all + ", def(n,h) self.hits += 1 end, {'threads':1}) end\n"
      "def check() return str(self.hits) + ',' + str(self.ok) end"));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  r.tick("A");
  check(r.probe("A") == "1", "a callback can stop and discard the remaining queued hits");
  check(r.probe("B") == "0,false", "a failed competing start cannot replace the owner's callback");
  check(!r.crypto.miner().scanner().running(), "a callback can synchronously join the scanner");
}

}
int main() {
  callbackReplacementIsIsolated();
  explicitStopReleases();
  invalidInputsRaise();
  observerCannotKeepLeaseAlive();
  scriptFindsAHit();
  secondAppIsBusy();
  leaseLapses();
  badSizesRaise();
  stoppedOwnerReleases();
  allFunctionsRenewLease();
  return failures == 0 ? 0 : 1;
}
