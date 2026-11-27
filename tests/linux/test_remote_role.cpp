#include "../../test/EngineFakes.h"
#include "../support.h"
#include "../Sockets.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/ApplicationRuntime.h"
#include "core/remote/PairingKey.h"
#include "core/remote/RemoteWire.h"
#include "media/AwtrixFontAdapter.h"
#include "media/ScriptIcon.h"
#include "platform/linux/host/HostStore.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/linux/remote/RemoteClockApplication.h"
#include "platform/linux/remote/RemoteRole.h"
#include "platform/host/Files.h"
#include "transport/net/HmacSha256.h"

using namespace awtrix;
using awtrix::test::check;
using awtrix::test::require;

namespace fs = std::filesystem;

namespace {

constexpr const char* kUid = "a1b2c3";
const std::string kKey(64, 'a');
const char* const kBeep = "{\"rtttl\":\"beep:d=4,o=5,b=100:c\"}";

int freeUdpPort() { return awtrix::test::freePort(SOCK_DGRAM); }

struct FakeClock {
  httplib::Server server;
  std::thread listener;
  int port = 0;
  std::mutex mutex;
  std::vector<std::string> acts;
  std::map<std::string, int> asked;
  int flakyFailures = 1;

  FakeClock() {
    server.Post("/api/v1/scripthost/act", [this](const httplib::Request& req, httplib::Response& res) {
      std::lock_guard<std::mutex> lock(mutex);
      acts.push_back(req.body);
      res.set_content("{\"ok\":true}", "application/json");
    });
    server.Get("/api/v1/capabilities", [this](const httplib::Request& req, httplib::Response& res) {
      count(req.path);
      res.set_content("{\"audio\":{\"mp3\":true,\"rtttl\":false}}", "application/json");
    });
    server.Get("/api/v1/settings", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("{}", "application/json");
    });
    server.Get("/api/v1/system", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("{}", "application/json");
    });
    server.Get(R"(/ICONS/(\w+)\.(gif|jpg))", [this](const httplib::Request& req, httplib::Response& res) {
      const std::string name = req.matches[1];
      const int times = count(req.path);
      if (req.matches[2] == "jpg") {
        res.status = 404;
      } else if (name == "flaky" && times <= flakyFailures) {
        res.status = 503;
      } else {
        res.set_content("GIF89a" + name, "image/gif");
      }
    });
    port = server.bind_to_any_port("127.0.0.1");
    require(port > 0, "bind a loopback clock");
    listener = std::thread([this] { server.listen_after_bind(); });
  }

  ~FakeClock() {
    server.stop();
    listener.join();
  }

  int count(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    return ++asked[path];
  }

  int times(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex);
    return asked.count(path) ? asked[path] : 0;
  }

  std::vector<std::string> taken() {
    std::lock_guard<std::mutex> lock(mutex);
    return acts;
  }
};

struct Clock : IPageClock {
  void fill(RenderCtx& ctx, int64_t now) override { ctx.nowMs = now; }
};

struct Rig {
  sound::AudioRouter audio;
  awtrix::test::NullDisplay display;
  awtrix::test::NullSystem system;
  Clock clock;
  ApplicationServices services{audio, display, system, clock, awtrixFontCatalog(), nullptr, nullptr, [](uint8_t) {}};
  ApplicationRuntime runtime{32, 8, services, AppRegistry()};
  FakeClock fake;
  remote::RemoteRole role;
  remote::RemoteClockApplication app{runtime.engine(), role};
  ScriptIcon icons;
  int rolePort = 0;
  os::Socket sender = os::kNoSocket;
  uint32_t counter = 1;
  int64_t nowMs = 1000;

  explicit Rig(const fs::path& root) {
    host::setDataDir(root.string());
    fs::create_directories(root / "ICONS");
    require(os::replaceFile((root / "key").string(), kKey + "\n"), "write the key file");
    rolePort = freeUdpPort();
    std::string error;
    require(rolePort && role.begin({kUid, (root / "key").string(), "http://127.0.0.1:" + std::to_string(fake.port)},
                                   static_cast<uint16_t>(rolePort), 32, 8, error),
            "the role starts: " + error);
    role.bindIcons(icons);
    sender = ::socket(AF_INET, SOCK_DGRAM, 0);
    require(sender != os::kNoSocket, "a socket for the clock's datagrams");
  }

  ~Rig() {
    os::closeSocket(sender);
  }

  void hello() {
    remote::PairingKey key;
    require(remote::PairingKey::parse(kKey, key), "the key parses");
    remote::wire::Hello h;
    h.uid = kUid;
    h.width = 32;
    h.height = 8;
    h.counter = ++counter;
    h.state = remote::wire::state::kDisplayOn;
    uint8_t packet[remote::wire::kMaxDatagram];
    const std::size_t length = remote::wire::encodeHello(h, remote::signerFor(key, hmacSha256), packet, sizeof(packet));
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<uint16_t>(rolePort));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    os::sendDatagram(sender, packet, length, to);
  }

  template <class Done>
  bool tickUntil(Done done, int64_t stepMs = 10, int rounds = 500) {
    for (int i = 0; i < rounds; ++i) {
      hello();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      nowMs += stepMs;
      role.tick(nowMs, runtime, services, nullptr);
      if (done()) return true;
    }
    return done();
  }

  void ticks(int rounds, int64_t stepMs = 10) {
    tickUntil([] { return false; }, stepMs, rounds);
  }

  bool settled() {
    const std::size_t before = fake.taken().size();
    ticks(20);
    return fake.taken().size() == before;
  }
};

bool hasIcon(const fs::path& root, const char* name) {
  std::string text;
  return os::readFile((root / "ICONS" / (std::string(name) + ".gif")).string(), text) && text == "GIF89a" + std::string(name);
}

bool contains(const std::vector<std::string>& acts, const char* part) {
  for (const std::string& act : acts)
    if (act.find(part) != std::string::npos) return true;
  return false;
}

void scriptCallsReachTheClockAsItWouldTakeThem(const fs::path& root) {
  Rig r(root);
  check(!r.app.notify("{\"txt\":\"hi\"}", "S"), "a notification the clock would refuse is refused here");
  check(!r.app.notify("[{\"text\":\"a\"},{\"text\":\"b\"}]", "S"), "so are two at once");
  check(r.app.notify("{\"text\":\"hi\"}", "S"), "a valid one is taken");
  std::string error;
  check(r.app.sound(script::SoundAction::Play, kBeep, "S", error) == 1,
        "before the clock named its outputs, a sound goes there for the clock to decide");
  check(r.role.soundCaps() == nullptr, "the outputs are not known yet");
  check(r.tickUntil([&] { return r.fake.taken().size() >= 2; }), "both calls reach the clock");
  const std::vector<std::string> acts = r.fake.taken();
  check(acts.size() == 2 && contains(acts, "\"act\":\"notify\"") && contains(acts, "\"text\":\"hi\"") &&
            !contains(acts, "txt"), "only the valid notification goes out");
  check(contains(acts, "\"act\":\"sound\"") && contains(acts, "beep"), "and the sound");

  check(r.tickUntil([&] { return r.role.soundCaps() != nullptr; }), "the clock's outputs arrive");
  check(r.app.sound(script::SoundAction::Play, kBeep, "S", error) == 0, "now a sound it cannot play stays here");
  check(r.app.sound(script::SoundAction::Play, "{\"file\":\"ding\"}", "S", error) == 1, "one it can play goes");
  check(r.tickUntil([&] { return r.fake.taken().size() >= 3; }) && r.settled() && r.fake.taken().size() == 3,
        "one more act, not two");
}

void scriptNeedsAreTheClocks(const fs::path& root) {
  Rig r(root);
  std::string seen;
  r.role.setOnCapabilities([&](const std::string& body) { seen = body; });
  check(r.tickUntil([&] { return !seen.empty(); }) && seen.find("\"mp3\":true") != std::string::npos,
        "the clock's capabilities are handed on");

  const char* const tc001 =
      "{\"audio\":{\"mp3\":false,\"rtttl\":true,\"song\":false,\"speech\":false,\"track\":false,\"radio\":false,"
      "\"url\":false,\"effect\":false,\"clip\":false},\"microphone\":false,\"platform\":{\"id\":\"esp32\"},"
      "\"sensors\":{\"light\":true},\"display\":{\"width\":64,\"height\":8},\"oauth\":true,\"tcp\":true}";
  const DeviceCapabilities::Extra modules[] = {{"tcp", true}, {"ble", false}, {}};
  DeviceCapabilities host = DeviceCapabilities::from(sound::Caps{}, nullptr, modules);
  for (std::size_t i = 0; i < host.present.size(); ++i)
    host.present[i] = DeviceCapabilities::kNames[i] != "audio.rtttl" && DeviceCapabilities::kNames[i] != "sensors.light";
  host.panelWidth = 32;
  host.panelHeight = 8;
  const DeviceCapabilities caps = remote::clockCapabilities(tc001, host);
  check(caps.has("audio.rtttl") && caps.has("sensors.light"), "what the clock has is there");
  for (const char* name : {"audio.mp3", "audio.song", "audio.speech", "audio.track", "audio.radio", "audio.effect",
                           "microphone"})
    check(!caps.has(name), std::string("what it lacks is not: ") + name);
  check(caps.has("tcp") && !caps.has("ble") && !caps.has("oauth"), "the modules are the host's");
  check(caps.fits(32, 8) && !caps.fits(64, 8), "so is the panel");
  check(!remote::clockCapabilities("{}", host).has("audio.rtttl"), "an empty answer names nothing");
}

void anIconTheClockDidNotAnswerIsAskedAgain(const fs::path& root) {
  Rig r(root);
  r.icons.missing("flaky");
  r.icons.missing("steady");
  check(r.tickUntil([&] { return r.fake.times("/ICONS/flaky.gif") == 1 && hasIcon(root, "steady"); }),
        "a failed icon does not hold up the next one");
  check(!hasIcon(root, "flaky"), "the failed one is not there yet");
  r.ticks(300, 10);
  check(r.fake.times("/ICONS/flaky.gif") == 1, "and is not asked again at once");
  check(r.tickUntil([&] { return hasIcon(root, "flaky"); }, 100, 200), "it is asked again later and arrives");
  check(r.fake.times("/ICONS/flaky.gif") == 2 && r.fake.times("/ICONS/steady.gif") == 1, "each as often as needed");
}

void aClockNotHeardCountsAsSilent(const fs::path& root) {
  Rig r(root);
  const auto quiet = [&r](int64_t ms) {
    for (const int64_t until = r.nowMs + ms; r.nowMs < until;) {
      r.nowMs += 500;
      r.role.tick(r.nowMs, r.runtime, r.services, nullptr);
    }
  };
  const int64_t started = r.nowMs + 500;
  quiet(20000);
  check(r.role.silentMs(r.nowMs) == r.nowMs - started, "a clock never heard is silent since the start");
  check(r.tickUntil([&] { return r.role.silentMs(r.nowMs) == 0; }), "its Hello ends the silence");
  quiet(remote::RemoteServer::kClockSilenceMs);
  check(r.role.silentMs(r.nowMs) == 0, "a short gap is not silence yet");
  quiet(20000);
  check(r.role.silentMs(r.nowMs) > 20000 - remote::RemoteServer::kClockSilenceMs - 1000,
        "a clock that stopped talking is silent again");
}

}

int main() {
  require(os::startSockets(), "sockets");
  const fs::path base = fs::temp_directory_path() / ("awtrix-remote-role-" + std::to_string(::getpid()));
  fs::remove_all(base);
  scriptCallsReachTheClockAsItWouldTakeThem(base / "calls");
  scriptNeedsAreTheClocks(base / "needs");
  anIconTheClockDidNotAnswerIsAskedAgain(base / "icons");
  aClockNotHeardCountsAsSilent(base / "silent");
  fs::remove_all(base);
  return awtrix::test::finish("remote-role");
}
