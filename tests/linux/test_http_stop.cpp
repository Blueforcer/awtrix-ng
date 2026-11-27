#include "../../test/EngineFakes.h"
#include "../Sockets.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

#include "core/CoreEngine.h"
#include "core/render/Canvas.h"
#include "persistence/DeviceConfig.h"
#include "platform/linux/host/HostHttpServer.h"
#include "platform/linux/host/vendor/httplib.h"

using namespace awtrix;

namespace {

using Display = awtrix::test::NullDisplay;
using System = awtrix::test::NullSystem;

constexpr int kRounds = 300;

int availablePort() { return awtrix::test::freePort(SOCK_STREAM); }

}

// A hang in either case ends in the test's timeout.
int main() {
  awtrix::os::startSockets();
  sound::AudioRouter audio;
  Display display;
  System system;
  CoreEngine engine(audio, display, system);
  Canvas canvas(52, 16);
  DeviceConfig config;

  // A stop straight after begin often comes before the listener thread has run.
  for (int round = 0; round < kRounds; ++round) {
    const int port = availablePort();
    HostHttpServer server;
    if (!port || !server.begin(static_cast<uint16_t>(port), engine, canvas, "fixture", config, "")) {
      std::printf("FAIL: round %d did not start\n", round);
      return 1;
    }
    server.stop();
  }

  // A listener that has already ended never runs again, and the stop must not wait for it to.
  httplib::Server* listener = nullptr;
  HostHttpOptions options;
  options.listenerFactory = [&listener] {
    auto made = std::make_unique<httplib::Server>();
    listener = made.get();
    return made;
  };
  const int port = availablePort();
  HostHttpServer server;
  if (!port || !server.begin(static_cast<uint16_t>(port), engine, canvas, "fixture", config, "", options)) {
    std::printf("FAIL: the ended listener case did not start\n");
    return 1;
  }
  listener->wait_until_ready();
  listener->stop();
  while (listener->is_running()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  server.stop();
  std::printf("http stop: %d rounds\n", kRounds);
  return 0;
}
