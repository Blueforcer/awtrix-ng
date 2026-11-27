#include "../support.h"
#include "../Sockets.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "core/remote/PairingKey.h"
#include "core/remote/RemoteWire.h"
#include "platform/linux/hostd/Children.h"
#include "platform/linux/hostd/ClockStore.h"
#include "platform/linux/remote/RunnerOptions.h"
#include "transport/net/HmacSha256.h"

using awtrix::hostd::Children;
using awtrix::hostd::ClockRecord;
using awtrix::hostd::ClockStore;
using awtrix::test::check;
using awtrix::test::require;
namespace fs = std::filesystem;
namespace os = awtrix::os;
namespace remote = awtrix::remote;

namespace {

const std::string kKey(64, 'c');
const std::string kOtherKey(64, 'd');
constexpr const char* kUid = "c0ffee";

int fakeRunner(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) != "--data") continue;
    std::ofstream(fs::path(argv[i + 1]).parent_path() / "starts", std::ios::app) << "start\n";
  }
  return remote::kClockSilentExit;
}

int starts(const ClockStore& store) {
  std::ifstream in(store.clockDir(kUid) / "starts");
  int lines = 0;
  for (std::string line; std::getline(in, line);) ++lines;
  return lines;
}

struct Rig {
  fs::path root;
  ClockStore store;
  std::string binary;
  int64_t nowMs = 0;
  int port = 0;
  os::UniqueSocket sender{::socket(AF_INET, SOCK_DGRAM, 0)};

  Rig(const char* name, const char* self)
      : root(fs::temp_directory_path() / name), store(root), binary(fs::absolute(self).string()) {
    fs::remove_all(root);
    std::string error;
    require(store.load(error), "the store loads: " + error);
    ClockRecord record;
    record.uid = kUid;
    record.clockUrl = "http://127.0.0.1:9";
    ClockRecord stored;
    require(store.pair(record, kKey, [](int p) { return os::localPortFree(static_cast<uint16_t>(p)); }, stored,
                       error),
            "the clock pairs: " + error);
    port = stored.port;
    require(sender.valid(), "a socket for the clock");
  }

  ~Rig() { fs::remove_all(root); }

  bool tickUntil(Children& children, const std::function<bool()>& done, int rounds = 300) {
    for (int i = 0; i < rounds; ++i) {
      nowMs += 20;
      children.tick(store, nowMs);
      if (done()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
  }

  void send(const std::string& uid, const std::string& keyHex) {
    remote::PairingKey key;
    require(remote::PairingKey::parse(keyHex, key), "the key parses");
    remote::wire::Hello hello;
    hello.uid = uid;
    hello.width = 32;
    hello.height = 8;
    hello.counter = 7;
    uint8_t packet[remote::wire::kMaxDatagram];
    sendRaw(packet, remote::wire::encodeHello(hello, remote::signerFor(key, awtrix::hmacSha256), packet, sizeof packet));
  }

  void sendRaw(const void* data, std::size_t length) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<uint16_t>(port));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    os::sendDatagram(sender.get(), data, length, to);
  }
};

void aSilentClockWaitsForItsHello(const char* self) {
  Rig rig("awtrix-hostd-children-silent", self);
  Children children(rig.binary, "", "", 1);
  children.sync(rig.store, rig.nowMs);
  check(rig.tickUntil(children, [&] { return children.waiting(kUid); }), "a process that ends for silence waits");
  check(starts(rig.store) == 1 && !children.running(kUid), "after one start");
  rig.nowMs += 10 * Children::kMaxBackoffMs;
  rig.tickUntil(children, [] { return false; }, 20);
  check(starts(rig.store) == 1 && children.waiting(kUid), "and is not started again by the clock alone");

  rig.sendRaw("FIND_AWTRIXNG", 13);
  rig.send(kUid, kOtherKey);
  rig.send("bada55", kKey);
  rig.tickUntil(children, [] { return false; }, 20);
  check(starts(rig.store) == 1 && children.waiting(kUid), "noise, a foreign key or another clock do not wake it");

  rig.send(kUid, kKey);
  check(rig.tickUntil(children, [&] { return starts(rig.store) == 2; }), "the clock's own Hello starts it again");
  check(rig.tickUntil(children, [&] { return children.waiting(kUid); }), "and it waits again when it ends so");

  children.stop(kUid);
  check(os::localPortFree(static_cast<uint16_t>(rig.port)) && !children.waiting(kUid),
        "a clock that is forgotten frees its port");
}

void aStartingHostWaitsForEachClock(const char* self) {
  Rig rig("awtrix-hostd-children-start", self);
  Children children(rig.binary, "", "", 1);
  children.awaitAll(rig.store);
  children.sync(rig.store, rig.nowMs);
  rig.tickUntil(children, [] { return false; }, 20);
  check(starts(rig.store) == 0 && children.waiting(kUid), "a starting host runs no clock before it is heard");
  check(!os::localPortFree(static_cast<uint16_t>(rig.port)), "it listens on the clock's port");
  rig.send(kUid, kKey);
  check(rig.tickUntil(children, [&] { return starts(rig.store) == 1; }), "a Hello starts the clock's process");

  check(rig.tickUntil(children, [&] { return children.waiting(kUid); }), "which waits once it ends so");
  children.restart(kUid);
  children.sync(rig.store, rig.nowMs);
  check(rig.tickUntil(children, [&] { return starts(rig.store) == 2; }), "pairing again starts it at once");
  children.stopAll();
}

}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--data") return fakeRunner(argc, argv);
  require(os::startSockets(), "sockets start");
  aSilentClockWaitsForItsHello(argv[0]);
  aStartingHostWaitsForEachClock(argv[0]);
  return awtrix::test::finish("hostd children");
}
