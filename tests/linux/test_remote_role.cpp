#include "../../test/EngineFakes.h"
#include "../support.h"

#include <arpa/inet.h>
#include <sys/socket.h>
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
#include "platform/posix/Files.h"
#include "transport/net/HmacSha256.h"

using namespace awtrix;
using awtrix::test::check;
using awtrix::test::require;

namespace fs = std::filesystem;

namespace {

constexpr const char* kUid = "a1b2c3";
const std::string kKey(64, 'a');
const char* const kBeep = "{\"rtttl\":\"beep:d=4,o=5,b=100:c\"}";

int freeUdpPort() {
  const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(address);
  const bool okay = fd >= 0 && ::bind(fd, reinterpret_cast<sockaddr*>(&address), size) == 0 &&
                    ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0;
  if (fd >= 0) ::close(fd);
  return okay ? ntohs(address.sin_port) : 0;
}

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
  int sender = -1;
  uint32_t counter = 1;
  int64_t nowMs = 1000;

  explicit Rig(const fs::path& root) {
    host::setDataDir(root.string());
    fs::create_directories(root / "ICONS");
    require(posix::replaceText((root / "key").string(), kKey + "\n"), "write the key file");
    rolePort = freeUdpPort();
    std::string error;
    require(rolePort && role.begin({kUid, (root / "key").string(), "http://127.0.0.1:" + std::to_string(fake.port)},
                                   static_cast<uint16_t>(rolePort), 32, 8, error),
            "the role starts: " + error);
    role.bindIcons(icons);
    sender = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    require(sender >= 0, "a socket for the clock's datagrams");
  }

  ~Rig() {
    if (sender >= 0) ::close(sender);
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
    ::sendto(sender, packet, length, 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
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
  return posix::readText((root / "ICONS" / (std::string(name) + ".gif")).string(), text) && text == "GIF89a" + std::string(name);
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

}

int main() {
  const fs::path base = fs::temp_directory_path() / ("awtrix-remote-role-" + std::to_string(::getpid()));
  fs::remove_all(base);
  scriptCallsReachTheClockAsItWouldTakeThem(base / "calls");
  anIconTheClockDidNotAnswerIsAskedAgain(base / "icons");
  fs::remove_all(base);
  return awtrix::test::finish("remote-role");
}
