#include "../support.h"
// BLE and gamepad scripts and HTTP routes with fake Bluetooth devices, and phones standing in
// for gamepads.
#include <arpa/inet.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/apps/IApp.h"
#include "core/apps/AppRegistry.h"
#include "core/render/Canvas.h"
#include "core/script/ScriptApp.h"
#include "core/script/ScriptHost.h"
#include "platform/linux/script/ExtensionHost.h"
#include "platform/linux/ble/BleScripting.h"
#include "platform/linux/ble/GamepadApi.h"
#include "platform/linux/ble/GamepadScripting.h"
#include "platform/linux/ble/RemoteGamepad.h"
#include "transport/net/UdpSocket.h"

using namespace awtrix;
using namespace awtrix::ble;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

struct FakeBackend : BleBackend {
  struct Call {
    std::string script, op, args;
    uint32_t id;
  };
  std::vector<Call> calls;
  std::vector<std::string> forgotten;
  std::string call(const std::string& script, const std::string& op, const std::string& args, uint32_t id) override {
    calls.push_back({script, op, args, id});
    if (op == "state") return R"({"state":"on","addr":"CC:C4:B2:77:98:C0"})";
    return R"({"ok":true})";
  }
  void forget(const std::string& script) override { forgotten.push_back(script); }
  const Call* last(const char* op) const {
    for (auto it = calls.rbegin(); it != calls.rend(); ++it)
      if (it->op == op) return &*it;
    return nullptr;
  }
};

// Slot n is Player n while Ready.
struct FakeReader : GamepadRegistry {
  GamepadDevices slots{{{GamepadStatus::Ready, "8BitDo", "E4:17:D8:BC:EC:1D", 1}, {}}};
  std::array<HidControls, kGamepadPlayers> controls;
  PlayerInput input(int player) const override { return {slots[player - 1].state, controls[player - 1]}; }
  std::string name(int player) const override { return slots[player - 1].name; }
  GamepadDevices devices() const override { return slots; }
};

long now = 0;

int64_t testClock() { return now; }

struct Rig {
  FakeBackend backend;
  FakeReader reader;
  BleScripting ble{backend};
  GamepadScripting pad{reader};
  script::ScriptServices services;
  AppRegistry registry;
  std::unique_ptr<script::ScriptHost> host;
  std::unique_ptr<script::ExtensionHost> extensions;
  Rig() {
    now = 0;
    services.monotonicMs = [] { return now; };
    host = std::make_unique<script::ScriptHost>(registry, services, nullptr, nullptr);
    extensions = std::make_unique<script::ExtensionHost>(*host,
        std::vector<script::ScriptExtension*>{&ble, &pad});
  }
  std::string probe(const char* name) {
    auto* app = static_cast<script::ScriptApp*>(registry.find(name));
    std::string out;
    if (!app || !app->callCheckForTest(out)) return "<no check>";
    return out;
  }
  void tick(const char* current) {
    RenderCtx ctx;
    host->tick(ctx, current);
  }
};

std::string bleApp(const std::string& body) { return "import ble\nclass App\n" + body + "\nend\nreturn App()"; }
std::string padApp(const std::string& body) { return "import gamepad\nclass App\n" + body + "\nend\nreturn App()"; }

void scanHandsDecodedAdverts() {
  Rig r;
  check(r.host->set("S", bleApp("var n, h, rssi, mfg\n"
                                "def init() self.n = 0 end\n"
                                "def setup() self.h = ble.scan(def (d, e) self.n += 1 self.rssi = d['rssi'] "
                                "self.mfg = d['mfg'][0]['data'].get(0, 2) end, {'uuid': '180d'}) end\n"
                                "def draw() end\n"
                                "def check() return str(self.h) + ',' + str(self.n) + ',' + str(self.rssi) + ',' + "
                                "str(self.mfg) end")),
        "a scanning script installs");
  const FakeBackend::Call* scan = r.backend.last("scan");
  check(scan && scan->script == "S" && scan->args.find("180d") != std::string::npos, "its scan reaches the backend");
  if (!scan) return;
  const std::string id = std::to_string(scan->id);
  r.ble.push({"S", scan->id, false, R"({"addr":"AA:BB:CC:DD:EE:01","rssi":-50,"mfg":[{"id":76,"data":"0102"}]})"});
  r.tick("S");
  check(r.probe("S") == id + ",1,-50,513", "an advert arrives decoded");
  r.ble.push({"S", scan->id, true, R"({"end":true})"});
  r.ble.push({"S", scan->id, false, R"({"addr":"AA:BB:CC:DD:EE:01","rssi":-40})"});
  r.tick("S");
  check(r.probe("S") == id + ",1,-50,513", "nothing arrives after the end");
}

void removalForgetsAndDropsOldCallbacks() {
  Rig r;
  const std::string src = bleApp("var n\ndef init() self.n = 0 end\n"
                                 "def setup() ble.scan(def (d, e) self.n += 1 end) end\n"
                                 "def draw() end\ndef check() return str(self.n) end");
  r.host->set("S", src);
  const uint32_t first = r.backend.last("scan")->id;
  r.host->set("S", src);
  check(r.backend.forgotten == std::vector<std::string>{"S"}, "reinstalling a script lets go of its resources");
  r.ble.push({"S", first, false, R"({"addr":"AA:BB:CC:DD:EE:01","rssi":-40})"});
  r.tick("S");
  check(r.probe("S") == "0", "and an event for the old instance is dropped");
  r.host->remove("S");
  check(r.backend.forgotten.size() == 2, "removing it lets go again");
}

void terminalCleanupWithoutUserCallbacks() {
  for (bool broken : {false, true}) {
    Rig r;
    r.host->set("S", bleApp("var calls\ndef init() self.calls = 0 end\n"
                            "def setup() ble.connect('AA:BB:CC:DD:EE:01', def (id, err)\n"
                            " self.calls += 1 ble.on_disconnect(id, def (why) self.calls += 1 end) end) end\n"
                            "def draw() raise 'broken' end\n"
                            "def check() return str(self.calls) end"));
    const uint32_t id = r.backend.last("connect")->id;
    r.ble.push({"S", id, false, R"({"connected":true})"});
    r.tick("S");
    check(r.probe("S") == "1", "the connection callback ran once");
    if (broken) {
      Canvas canvas(32, 8);
      RenderCtx ctx;
      r.registry.find("S")->render(canvas, ctx);
      check(r.backend.forgotten == std::vector<std::string>{"S"},
            "a failing draw releases its Bluetooth resources immediately");
    } else {
      r.host->setRunningScripts({"Time"});
    }
    r.ble.push({"S", id, true, R"({"disconnected":true})"});
    r.tick("Time");
    check(r.probe("S") == "1", "terminal cleanup does not invoke user callbacks");
    r.host->setRunningScripts({"S"});
    r.ble.push({"S", id, false, R"({"connected":true})"});
    r.tick("S");
    check(r.probe("S") == "1", "late events stay discarded when the script is selected again");
  }
}

void callbackFailureReleasesResources() {
  Rig r;
  check(r.host->set("S", bleApp("def setup() ble.scan(def (d, e) raise 'broken' end) end\n"
                                "def draw() end")), "a failing callback script installs");
  const uint32_t id = r.backend.last("scan")->id;
  r.ble.push({"S", id, false, R"({"addr":"AA:BB:CC:DD:EE:01","rssi":-40})"});
  r.ble.push({"S", id, false, R"({"addr":"AA:BB:CC:DD:EE:02","rssi":-50})"});
  r.tick("S");
  check(!static_cast<script::ScriptApp*>(r.registry.find("S"))->ok(), "the callback failure retires the app");
  check(r.backend.forgotten == std::vector<std::string>{"S"},
        "a failed callback releases resources and discards queued callbacks");
}

void cleanupSurvivesQueueOverflow() {
  Rig r;
  const std::string source = bleApp("var calls\ndef init() self.calls = 0 end\n"
                                    "def setup() ble.scan(def (data, err) self.calls += 1 end) end\n"
                                    "def draw() end\n"
                                    "def check() return str(size(_ble_cbs[_native_app()])) + ',' + str(self.calls) end");
  r.host->set("A", source);
  const uint32_t first = r.backend.last("scan")->id;
  r.host->set("B", source);
  const uint32_t second = r.backend.last("scan")->id;
  r.ble.push({"A", first, true, R"({"end":true})"});
  for (int i = 0; i < 64; ++i) r.ble.push({"B", second, false, "{}"});
  r.tick("B");
  check(r.probe("A") == "0,0", "an end pushed out of a full queue still drops the callback");
  check(r.probe("B") == "1,64", "while the other script gets all its events");
}

void connectReportsTheLinkAndItsEnd() {
  Rig r;
  r.host->set("C", bleApp("var log\ndef init() self.log = '' end\n"
                          "def setup() ble.connect('C4:DE:E2:1F:D1:B6', def (c, e)\n"
                          "  if c == nil self.log += 'fail ' + str(e) return end\n"
                          "  self.log += 'up '\n"
                          "  ble.on_disconnect(c, def (why) self.log += 'down ' + str(why) end)\n"
                          "end, {'random': true}) end\n"
                          "def draw() end\ndef check() return self.log end"));
  const FakeBackend::Call* connect = r.backend.last("connect");
  check(connect && connect->args.find("\"random\":true") != std::string::npos, "connect passes the address type");
  if (!connect) return;
  r.ble.push({"C", connect->id, false, R"({"connected":true,"mtu":247})"});
  r.tick("C");
  r.ble.push({"C", connect->id, true, R"({"disconnected":true,"reason":"timeout"})"});
  r.tick("C");
  check(r.probe("C") == "up down timeout", "the script hears the link and its end");
}

void bleNamesAreTaken() {
  Rig r;
  check(!r.host->set("ble", "# @module\nreturn 1\n"), "no script module may be called ble");
  check(!r.host->set("x", "# @module gamepad\nreturn 1\n"), "nor gamepad");
}

void gamepadReadsTheControls() {
  Rig r;
  HidControls& c = r.reader.controls[0];
  c.buttons = 0x801;
  c.hat = 2;
  c.axes[0] = 0;
  c.axes[1] = 255;
  c.triggers[1] = 255;
  r.host->set("S", padApp("def draw() end\ndef check() return str(gamepad.ready()) + ',' + gamepad.state() + ',' + "
                          "gamepad.name() + ',' + str(gamepad.down('A')) + ',' + str(gamepad.down('B')) + ',' + "
                          "str(gamepad.down('START')) + ',' + str(gamepad.pressed(1, 'A')) + ',' + "
                          "str(gamepad.pressed(0, 'A')) + ',' + str(gamepad.down(11)) + ',' + "
                          "str(gamepad.axis('lx')) + ',' + str(gamepad.axis('ly')) + ',' + str(gamepad.trigger('rt')) + "
                          "',' + str(gamepad.trigger('lt')) + ',' + str(gamepad.dir()) end"));
  check(r.probe("S") == "true,ready,8BitDo,true,false,true,false,true,true,-100,100,100,0,[1, 0]",
        "a script reads buttons, D-pad, sticks and triggers");
  c.hat = -1;
  const std::string out = r.probe("S");
  check(out.substr(out.rfind('[')) == "[-1, 1]", "without the D-pad the left stick gives the direction");
  r.reader.slots[0].state = GamepadStatus::Waiting;
  c = HidControls{};
  check(r.probe("S").rfind("false,waiting,", 0) == 0, "a gamepad that is away says so");
}

void gamepadRefusesUnknownNames() {
  Rig r;
  r.host->set("S", padApp("def draw() end\ndef check()\n"
                          "  var out = ''\n"
                          "  for f : [/ -> gamepad.down('Z'), / -> gamepad.axis('lz'), / -> gamepad.trigger('mt')]\n"
                          "    try f() out += 'no ' except 'value_error' out += 'yes ' end\n"
                          "  end\n"
                          "  return out\nend"));
  check(r.probe("S") == "yes yes yes ", "an unknown button, axis or trigger name raises value_error");
}

void gamepadIsClaimedByOneGameAtATime() {
  Rig r;
  r.host->set("A", padApp("def draw() end\ndef check() gamepad.claim(self) return str(gamepad.mine(self)) end"));
  r.host->set("B", padApp("def draw() end\ndef check() return str(gamepad.mine(self)) end"));
  check(r.probe("A") == "true", "the game that claims it has it");
  check(r.probe("B") == "false", "another game does not");
  now += 1001;
  check(r.probe("B") == "true", "until the claim lapses");
}

void gamepadRoutes() {
  FakeReader reader;
  reader.slots[1] = {GamepadStatus::Waiting, "Second", "AA:BB:CC:DD:EE:02", 0};
  int pairs = 0, forgotten = 0;
  GamepadPairResult result{GamepadPairStatus::Started, 2};
  RemoteGamepad phones(testClock);
  GamepadApi api(&reader, &phones, [&] { ++pairs; return result; }, [&](int id) { forgotten = id; });
  std::string body;
  check(api.handle("GET", "/api/v1/gamepad", "", body) == 200 &&
            body == R"({"devices":[{"id":1,"state":"ready","name":"8BitDo","address":"E4:17:D8:BC:EC:1D","player":1},)"
                    R"({"id":2,"state":"waiting","name":"Second","address":"AA:BB:CC:DD:EE:02","player":null}],)"
                    R"("remotes":[]})",
        "GET lists both device slots with their players");
  check(api.handle("POST", "/api/v1/gamepad/pair", "", body) == 200 && body == R"({"ok":true,"id":2})" && pairs == 1,
        "POST pair answers the slot it searches for");
  result = {GamepadPairStatus::Full, 0};
  check(api.handle("POST", "/api/v1/gamepad/pair", "", body) == 409 &&
            body == R"({"error":{"code":"gamepadsFull","message":"no free slot"}})",
        "with both slots taken there is nothing to pair into");
  result = {};
  check(api.handle("POST", "/api/v1/gamepad/pair", "", body) == 503 && body.find("unavailable") != std::string::npos,
        "without a running Bluetooth service pairing is unavailable");
  check(api.handle("DELETE", "/api/v1/gamepad/2", "", body) == 200 && body == R"({"ok":true})" && forgotten == 2,
        "DELETE forgets the slot it names");
  for (const char* path : {"/api/v1/gamepad/3", "/api/v1/gamepad/0", "/api/v1/gamepad/02", "/api/v1/gamepad/2/x"})
    check(api.handle("DELETE", path, "", body) == 404 && body.find("no such gamepad") != std::string::npos &&
              forgotten == 2,
          "any other id names no gamepad");
  check(api.handle("DELETE", "/api/v1/gamepad", "", body) == 405 && body.find("allowed: GET") != std::string::npos,
        "the list is read only");
  check(api.handle("GET", "/api/v1/gamepad/pair", "", body) == 405 && pairs == 3, "pair takes POST only");
  check(api.handle("GET", "/api/v1/gamepad/1", "", body) == 405 && body.find("allowed: DELETE") != std::string::npos,
        "a slot takes DELETE only");
  check(api.handle("GET", "/api/v1/gamepads", "", body) == 0, "other paths are not the gamepad's");
}

void gamepadRoutesRequireScripting() {
  FakeReader reader;
  GamepadApi api(&reader, nullptr, [] { return GamepadPairResult{}; }, [](int) {});
  std::string body;
  check(api.handle("GET", "/api/v1/gamepad", "", body) == 0, "without scripting there is no listing");
  check(api.handle("POST", "/api/v1/gamepad/pair", "", body) == 0, "nothing to pair");
  check(api.handle("DELETE", "/api/v1/gamepad/1", "", body) == 0, "nothing to forget");
  check(api.handle("POST", "/api/v1/gamepad/remote", "", body) == 0, "and no phone");

  int forgets = 0;
  RemoteGamepad phones(testClock);
  GamepadApi noBluetooth(nullptr, &phones, [] { return GamepadPairResult{}; }, [&](int) { ++forgets; });
  check(noBluetooth.handle("GET", "/api/v1/gamepad", "", body) == 200 &&
            body == R"({"devices":[{"id":1,"state":"unpaired","name":"","address":"","player":null},)"
                    R"({"id":2,"state":"unpaired","name":"","address":"","player":null}],"remotes":[]})",
        "without Bluetooth GET answers, with both slots empty");
  check(noBluetooth.handle("POST", "/api/v1/gamepad/pair", "", body) == 0, "but there is nothing to pair");
  check(noBluetooth.handle("DELETE", "/api/v1/gamepad/1", "", body) == 0 && forgets == 0, "or to forget");
  check(noBluetooth.handle("POST", "/api/v1/gamepad/remote", "", body) == 200, "a phone plays without Bluetooth");
  phones.endAll();
}

void twoPlayersInBerry() {
  Rig r;
  r.reader.controls[0].buttons = 1;
  r.reader.controls[0].hat = 0;
  r.reader.slots[1] = {GamepadStatus::Ready, "Second", "AA:BB:CC:DD:EE:02", 2};
  r.reader.controls[1].buttons = 2;
  r.reader.controls[1].hat = 4;
  r.reader.controls[1].axes[2] = 255;
  r.reader.controls[1].triggers[0] = 255;
  check(r.host->set("S", padApp("def draw() end\ndef check() return gamepad.name(2) + ',' + "
      "str(gamepad.ready(2)) + ',' + str(gamepad.buttons(1)) + ',' + str(gamepad.buttons(2)) + ',' + "
      "str(gamepad.dir()) + ',' + str(gamepad.dir(2)) + ',' + str(gamepad.down('B', 2)) + ',' + "
      "str(gamepad.pressed(0, 'B', 2)) + ',' + str(gamepad.axis('rx', 2)) + ',' + "
      "str(gamepad.trigger('lt', 2)) + ',' + str(gamepad.hat(2)) end")), "a two-player script installs");
  check(r.probe("S") == "Second,true,1,2,[0, -1],[0, 1],true,true,100,100,4",
        "real Berry calls read separate player states and controls");
  r.host->set("A", padApp("def draw() end\ndef check() gamepad.claim(self, 1) return str(gamepad.mine(self, 2)) end"));
  r.host->set("B", padApp("def draw() end\ndef check() gamepad.claim(self, 2) return "
      "str(gamepad.mine(self, 1)) + ',' + str(gamepad.mine(self, 2)) end"));
  check(r.probe("A") == "true" && r.probe("B") == "false,true" && r.probe("A") == "false",
        "claims belong to one player, shared between scripts");
  r.host->set("R", padApp("def draw() end\ndef check() gamepad.release(_apps['B'], 2) "
      "return str(gamepad.mine(self, 1)) + ',' + str(gamepad.mine(self, 2)) end"));
  check(r.probe("R") == "false,true", "release gives back only the selected player");
  now += 1001;
  check(r.probe("R") == "true,true", "claims keep their existing expiry");
}

void gamepadRejectsUnknownPlayers() {
  Rig r;
  r.host->set("S", padApp("def draw() end\ndef check()\n"
      " var n = 0\n"
      " for player : [0, 3, -1, '2', true, 1.5]\n"
      "  for f : [/ -> gamepad.state(player), / -> gamepad.ready(player), / -> gamepad.name(player), "
      "/ -> gamepad.buttons(player), / -> gamepad.hat(player), / -> gamepad.dir(player), "
      "/ -> gamepad.down('A', player), / -> gamepad.pressed(0, 'A', player), "
      "/ -> gamepad.axis('lx', player), / -> gamepad.trigger('lt', player), "
      "/ -> gamepad.claim(self, player), / -> gamepad.release(self, player), / -> gamepad.mine(self, player)]\n"
      "   try f() except 'value_error' n += 1 end\n"
      "  end\n end\n return str(n) end"));
  check(r.probe("S") == "78", "every player-aware call validates the player before using it");
}

using Packet = std::array<uint8_t, RemoteGamepad::kPacketBytes>;

RemoteGamepad::Token tokenOf(uint8_t seed) {
  RemoteGamepad::Token t;
  for (std::size_t i = 0; i < t.size(); ++i) t[i] = static_cast<uint8_t>(seed + i * 7);
  return t;
}

Packet packet(const RemoteGamepad::Token& token, uint32_t seq, uint32_t buttons, int8_t hat = -1,
              std::array<uint8_t, 4> axes = {128, 128, 128, 128}, std::array<uint8_t, 2> triggers = {0, 0}) {
  Packet p{};
  p[0] = 1;
  for (std::size_t i = 0; i < token.size(); ++i) p[1 + i] = token[i];
  for (int i = 0; i < 4; ++i) {
    p[17 + i] = static_cast<uint8_t>(seq >> (8 * i));
    p[21 + i] = static_cast<uint8_t>(buttons >> (8 * i));
  }
  p[25] = static_cast<uint8_t>(hat);
  for (int i = 0; i < 4; ++i) p[26 + i] = axes[i];
  p[30] = triggers[0];
  p[31] = triggers[1];
  return p;
}

bool send(RemoteGamepad& pad, const Packet& p) { return pad.receive(p.data(), p.size()); }

HidControls phoneControls(const RemoteGamepad& pad, int player = 1) {
  HidControls controls;
  pad.controls(player, controls);
  return controls;
}

RemoteGamepad::Token hexToken(const std::string& body) {
  RemoteGamepad::Token t{};
  const std::size_t at = body.find(R"("token":")");
  if (at == std::string::npos || body.size() < at + 9 + 32) return t;
  for (std::size_t i = 0; i < t.size(); ++i)
    t[i] = static_cast<uint8_t>(std::stoi(body.substr(at + 9 + 2 * i, 2), nullptr, 16));
  return t;
}

long long numberAfter(const std::string& body, const char* key) {
  const std::string k = std::string("\"") + key + "\":";
  const std::size_t at = body.find(k);
  return at == std::string::npos ? -1 : std::stoll(body.substr(at + k.size()));
}

void remotePacketsAreChecked() {
  now = 0;
  RemoteGamepad pad(testClock);
  const auto token = tokenOf(3);
  check(!send(pad, packet(token, 1, 1)), "no packet is taken before a session starts");
  const uint32_t id = pad.begin("Pixel 8", 1, token);
  check(id && pad.sessions().size() == 1 && pad.sessions()[0].name == "Pixel 8" && pad.sessions()[0].id == id,
        "a session starts");
  check(phoneControls(pad).buttons == 0 && phoneControls(pad).hat == -1, "with every control released");
  check(send(pad, packet(token, 5, 0x801, 2, {0, 255, 10, 20}, {30, 40})), "a valid packet is taken");
  const HidControls c = phoneControls(pad);
  check(c.buttons == 0x801 && c.hat == 2 && c.axes[0] == 0 && c.axes[1] == 255 && c.axes[2] == 10 &&
            c.axes[3] == 20 && c.triggers[0] == 30 && c.triggers[1] == 40,
        "its buttons, hat, axes and triggers are read little-endian at their offsets");
  check(!send(pad, packet(token, 5, 1)) && !send(pad, packet(token, 4, 1)),
        "a sequence number not above the last is dropped");
  check(!send(pad, packet(tokenOf(4), 9, 1)), "a wrong token is dropped");
  Packet wrongVersion = packet(token, 9, 1);
  wrongVersion[0] = 2;
  check(!send(pad, wrongVersion), "a wrong version is dropped");
  const Packet p = packet(token, 9, 1);
  check(!pad.receive(p.data(), p.size() - 1), "a short datagram is dropped");
  uint8_t longer[33] = {};
  std::copy(p.begin(), p.end(), longer);
  check(!pad.receive(longer, sizeof(longer)), "a long one too");
  check(phoneControls(pad).buttons == 0x801, "and none of them changed the controls");
  check(send(pad, packet(token, 0xFFFFFFF0u, 2, 9)), "a later packet is taken");
  check(phoneControls(pad).buttons == 2 && phoneControls(pad).hat == -1, "a hat outside 0 to 7 reads as released");
}

void remoteSessionsTimeOutOnTheirOwn() {
  now = 0;
  RemoteGamepad pad(testClock);
  const auto one = tokenOf(1), two = tokenOf(2);
  pad.begin("A", 1, one);
  now = 500;
  pad.begin("B", 2, two);
  now = 999;
  pad.tick();
  check(pad.sessions().size() == 2, "a session waits a second for its first packet");
  send(pad, packet(one, 1, 0x40));
  now = 1000;
  pad.tick();
  check(pad.sessions().size() == 2 && phoneControls(pad, 1).buttons == 0x40, "each packet keeps it for another second");
  now = 1500;
  pad.tick();
  HidControls left;
  check(pad.sessions().size() == 1 && pad.sessions()[0].player == 1 && !pad.controls(2, left),
        "a quiet second ends only that phone's session");
  check(!send(pad, packet(two, 2, 1)), "its token no longer works");
  now = 1999;
  pad.tick();
  check(pad.sessions().empty(), "the other ends when it goes quiet too");
}

void remoteSessionsArePerPlayer() {
  now = 0;
  RemoteGamepad pad(testClock);
  const auto one = tokenOf(1), two = tokenOf(2), three = tokenOf(3);
  const uint32_t a = pad.begin("A", 1, one), b = pad.begin("B", 2, two);
  check(a && b && a != b, "two phones get their own session numbers");
  send(pad, packet(one, 1, 1));
  send(pad, packet(two, 1, 2));
  check(phoneControls(pad, 1).buttons == 1 && phoneControls(pad, 2).buttons == 2,
        "each token drives its own player, with its own sequence");
  const uint32_t c = pad.begin("C", 2, three);
  check(c != b && pad.name(2) == "C" && !send(pad, packet(two, 2, 4)), "a phone that takes a player ends the old session");
  check(!pad.end(b) && pad.sessions().size() == 2, "the replaced session cannot be ended any more");
  check(pad.end(a) && pad.sessions().size() == 1 && pad.sessions()[0].id == c, "ending one session leaves the other");
  check(!pad.end(a) && !pad.end(0), "a session ends once");
}

void muxPrefersThePhone() {
  now = 0;
  FakeReader bluetooth;
  bluetooth.controls[0].buttons = 1;
  bluetooth.slots[1] = {GamepadStatus::Ready, "Second", "AA:BB:CC:DD:EE:02", 2};
  bluetooth.controls[1].buttons = 2;
  RemoteGamepad phones(testClock);
  GamepadMux mux(phones, &bluetooth);
  check(mux.input(1).controls.buttons == 1 && mux.name(1) == "8BitDo", "without a phone the Bluetooth pad is read");
  const auto token = tokenOf(9);
  const uint32_t id = phones.begin("Pixel 8", 2, token);
  send(phones, packet(token, 1, 8));
  check(mux.input(1).controls.buttons == 1 && mux.name(1) == "8BitDo" && mux.input(2).controls.buttons == 8 &&
            mux.name(2) == "Pixel 8" && mux.input(2).state == GamepadStatus::Ready,
        "the phone takes over only its player");
  phones.end(id);
  check(mux.name(2) == "Second" && mux.input(2).controls.buttons == 2, "and gives it back when it ends");

  GamepadMux alone(phones, nullptr);
  check(alone.input(1).state == GamepadStatus::Unpaired && alone.name(1).empty(), "no Bluetooth and no phone: no gamepad");
  phones.begin("Phone", 1, token);
  send(phones, packet(token, 1, 2, 4));
  check(alone.input(1).state == GamepadStatus::Ready && alone.input(1).controls.hat == 4, "a phone works without Bluetooth");
  phones.endAll();
}

void scriptsReadThePhones() {
  FakeReader bluetooth;
  bluetooth.slots[0].state = GamepadStatus::Waiting;
  RemoteGamepad phones(testClock);
  GamepadMux mux(phones, &bluetooth);
  GamepadScripting pad{mux};
  script::ScriptServices services;
  AppRegistry registry;
  now = 0;
  services.monotonicMs = [] { return now; };
  script::ScriptHost host(registry, services, nullptr, nullptr);
  script::ExtensionHost extensions(host, std::vector<script::ScriptExtension*>{&pad});
  host.set("S", padApp("def draw() end\ndef check() return gamepad.state() + ',' + gamepad.name() + ',' + "
                       "str(gamepad.down('A')) + ',' + str(gamepad.down('START')) + ',' + str(gamepad.dir()) + "
                       "',' + str(gamepad.axis('lx')) + ',' + str(gamepad.trigger('rt')) + ',' + "
                       "str(gamepad.buttons(2)) end"));
  const auto probe = [&] {
    auto* app = static_cast<script::ScriptApp*>(registry.find("S"));
    std::string out;
    return app && app->callCheckForTest(out) ? out : std::string("<no check>");
  };
  check(probe() == "waiting,8BitDo,false,false,nil,0,0,0", "a script sees the Bluetooth pad without a phone");
  const auto one = tokenOf(5), two = tokenOf(6);
  phones.begin("Pixel 8", 1, one);
  phones.begin("Galaxy", 2, two);
  send(phones, packet(one, 1, 0x801, 6, {0, 128, 128, 128}, {0, 255}));
  send(phones, packet(two, 1, 0x40));
  check(probe() == "ready,Pixel 8,true,true,[-1, 0],-100,100,64", "and both phones' controls while they are connected");
  now = 1000;
  phones.tick();
  check(probe() == "waiting,8BitDo,false,false,nil,0,0,0", "and the Bluetooth pad again once the phones are gone");
}

void remoteRoutes() {
  now = 0;
  FakeReader bluetooth;
  RemoteGamepad phones(testClock);
  GamepadApi api(&bluetooth, &phones, [] { return GamepadPairResult{}; }, [](int) {});
  std::string body;
  check(api.handle("POST", "/api/v1/gamepad/remote", "", body) == 200, "POST without a body starts a session");
  const std::size_t hex = body.find(R"("token":")");
  check(body.rfind(R"({"port":4214,"token":")", 0) == 0 && hex != std::string::npos &&
            body.find_first_not_of("0123456789abcdef", hex + 9) == hex + 9 + 32,
        "and answers the port and 32 lowercase hex digits");
  check(numberAfter(body, "player") == 2, "a phone without a player takes the one no gamepad plays");
  const long long first = numberAfter(body, "session");
  const auto firstToken = hexToken(body);
  check(first > 0 && phones.sessions().size() == 1 && phones.sessions()[0].name == "Phone", "the default name is Phone");
  check(send(phones, packet(firstToken, 1, 1)), "the answered token opens the session");
  api.handle("GET", "/api/v1/gamepad", "", body);
  check(body.find(R"("remotes":[{"session":)" + std::to_string(first) + R"(,"name":"Phone","player":2}])") !=
            std::string::npos && body.find(R"("name":"8BitDo")") != std::string::npos,
        "GET lists the phone next to the Bluetooth pad");

  check(api.handle("POST", "/api/v1/gamepad/remote", R"({"name":"Pixel 8"})", body) == 200 &&
            numberAfter(body, "player") == 1,
        "the next phone takes the player that has no phone");
  const long long second = numberAfter(body, "session");
  check(second != first && phones.sessions().size() == 2, "both phones play");
  check(api.handle("POST", "/api/v1/gamepad/remote", R"({"name":"Galaxy","player":2})", body) == 200 &&
            !send(phones, packet(firstToken, 2, 1)) && phones.name(2) == "Galaxy",
        "a phone that asks for a taken player takes it over");
  const long long third = numberAfter(body, "session");

  for (const char* invalid : {"0", "3", "-1", "true", "null", "\"2\"", "1.5"})
    check(api.handle("POST", "/api/v1/gamepad/remote", "{\"player\":" + std::string(invalid) + "}", body) == 400 &&
              body.find(R"("code":"invalidPlayer")") != std::string::npos && phones.name(2) == "Galaxy",
          "an invalid player is refused and leaves the sessions alone");
  check(api.handle("POST", "/api/v1/gamepad/remote", R"({"name":")" + std::string(33, 'x') + R"("})", body) == 400 &&
            body.find("invalidName") != std::string::npos,
        "a name over 32 characters is refused");
  const std::string umlauts = "\xC3\x84\xC3\x96\xC3\x9C\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F\xC3\x84";
  check(api.handle("POST", "/api/v1/gamepad/remote", R"({"name":")" + umlauts + umlauts + umlauts + umlauts +
                       R"(","player":1})", body) == 200,
        "32 characters are counted, not bytes");
  const long long fourth = numberAfter(body, "session");
  check(api.handle("POST", "/api/v1/gamepad/remote", R"({"name":7})", body) == 400, "a name must be text");
  check(api.handle("POST", "/api/v1/gamepad/remote", "{", body) == 400 && body.find("invalidJson") != std::string::npos,
        "broken JSON is refused");
  check(api.handle("POST", "/api/v1/gamepad/remote", "[]", body) == 400, "so is JSON that is not an object");

  check(api.handle("DELETE", "/api/v1/gamepad/remote/" + std::to_string(third), "", body) == 200 &&
            body == R"({"ok":true})" && phones.sessions().size() == 1,
        "DELETE ends the session it names");
  check(api.handle("DELETE", "/api/v1/gamepad/remote/" + std::to_string(third), "", body) == 404 &&
            body.find("no such session") != std::string::npos,
        "and answers 404 once it is gone");
  check(api.handle("DELETE", "/api/v1/gamepad/remote/" + std::to_string(second), "", body) == 404,
        "a session another phone replaced is gone too");
  for (const char* path : {"/api/v1/gamepad/remote/0", "/api/v1/gamepad/remote/x", "/api/v1/gamepad/remote/",
                           "/api/v1/gamepad/remote/01", "/api/v1/gamepad/remote/99999999999"})
    check(api.handle("DELETE", path, "", body) == 404, "a path that names no session finds none");
  check(api.handle("GET", "/api/v1/gamepad/remote/" + std::to_string(fourth), "", body) == 405 &&
            body.find("allowed: DELETE") != std::string::npos,
        "a session takes DELETE only");
  check(api.handle("GET", "/api/v1/gamepad/remote", "", body) == 405 && body.find("allowed: POST") != std::string::npos,
        "starting takes POST only");
  check(api.handle("DELETE", "/api/v1/gamepad/remote", "", body) == 405, "and no DELETE without a session");
  phones.endAll();
}

void remoteOverUdp() {
  now = 0;
  RemoteGamepad phones(testClock);
  GamepadApi api(nullptr, &phones, [] { return GamepadPairResult{}; }, [](int) {});
  std::string body;
  if (api.handle("POST", "/api/v1/gamepad/remote", R"({"name":"Loop","player":2})", body) != 200) {
    check(false, "a session opens the UDP port");
    return;
  }
  const auto token = hexToken(body);
  UdpSocket phone;
  check(phone.open(0), "the phone side opens a socket");
  const net::Endpoint to{ntohl(inet_addr("127.0.0.1")), RemoteGamepad::kPort};
  const Packet stranger = packet(tokenOf(0), 1, 0xFFF);
  const Packet valid = packet(token, 7, 0x10, 0, {128, 0, 128, 128}, {0, 0});
  phone.sendTo(to, stranger.data(), stranger.size());
  phone.sendTo(to, valid.data(), valid.size());
  for (int i = 0; i < 100 && phoneControls(phones, 2).buttons != 0x10; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    phones.poll();
  }
  const HidControls heard = phoneControls(phones, 2);
  check(heard.buttons == 0x10 && heard.hat == 0 && heard.axes[1] == 0, "a datagram to port 4214 reaches the controls");
  now = 1000;
  phones.poll();
  check(phones.sessions().empty(), "the session times out on the loop");
  check(!send(phones, valid), "and takes nothing more");
}

}

int main() {
  twoPlayersInBerry();
  gamepadRejectsUnknownPlayers();
  scanHandsDecodedAdverts();
  removalForgetsAndDropsOldCallbacks();
  terminalCleanupWithoutUserCallbacks();
  callbackFailureReleasesResources();
  cleanupSurvivesQueueOverflow();
  connectReportsTheLinkAndItsEnd();
  bleNamesAreTaken();
  gamepadReadsTheControls();
  gamepadRefusesUnknownNames();
  gamepadIsClaimedByOneGameAtATime();
  gamepadRoutes();
  gamepadRoutesRequireScripting();
  remotePacketsAreChecked();
  remoteSessionsTimeOutOnTheirOwn();
  remoteSessionsArePerPlayer();
  muxPrefersThePhone();
  scriptsReadThePhones();
  remoteRoutes();
  remoteOverUdp();
  if (failures) std::printf("%d failure(s)\n", failures);
  else std::printf("ble-script: all checks passed\n");
  return failures ? 1 : 0;
}
