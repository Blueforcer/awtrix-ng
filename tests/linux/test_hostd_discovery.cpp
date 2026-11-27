#include "../support.h"
#include "../Sockets.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#include "core/remote/RemoteWire.h"
#include "platform/linux/hostd/ClockStore.h"
#include "platform/linux/hostd/Discovery.h"

using awtrix::hostd::ClockStore;
using awtrix::hostd::Discovery;
using awtrix::test::check;
using awtrix::test::require;
namespace fs = std::filesystem;
namespace os = awtrix::os;
namespace wire = awtrix::remote::wire;

namespace {

sockaddr_in loopback(uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  return address;
}

std::string receive(os::Socket socket, sockaddr_in& from) {
  char buffer[256];
  const long count = os::receiveDatagram(socket, buffer, sizeof buffer, from);
  return count > 0 ? std::string(buffer, static_cast<std::size_t>(count)) : std::string();
}

}

int main() {
  require(os::startSockets(), "sockets start");
  const uint16_t clockPort = static_cast<uint16_t>(awtrix::test::freePort(SOCK_DGRAM));
  const uint16_t replyPort = static_cast<uint16_t>(awtrix::test::freePort(SOCK_DGRAM));
  require(clockPort && replyPort && clockPort != replyPort, "two free UDP ports");

  os::UniqueSocket clock(::socket(AF_INET, SOCK_DGRAM, 0));
  sockaddr_in clockAddress = loopback(clockPort);
  require(clock.valid() &&
              ::bind(clock.get(), reinterpret_cast<const sockaddr*>(&clockAddress), sizeof clockAddress) == 0,
          "a clock listens on loopback");
  awtrix::test::receiveTimeout(clock.get(), 300);

  const ClockStore store(fs::temp_directory_path() / "awtrix-hostd-discovery");
  os::UniqueSocket holder(::socket(AF_INET, SOCK_DGRAM, 0));
  sockaddr_in anyAddress = loopback(replyPort);
  anyAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  require(holder.valid() &&
              ::bind(holder.get(), reinterpret_cast<const sockaddr*>(&anyAddress), sizeof anyAddress) == 0,
          "another program holds the answer port");

  Discovery discovery(clockPort, replyPort);
  check(!discovery.begin("desk", 8300) && !discovery.listening(), "a taken answer port is reported");
  discovery.tick(0, store);
  check(!discovery.listening(), "it stays taken while the other program holds it");
  holder.reset();
  discovery.tick(Discovery::kEveryMs - 1, store);
  check(!discovery.listening(), "nothing is tried before the next round");
  discovery.tick(Discovery::kEveryMs, store);
  check(discovery.listening(), "the next round takes the freed port");

  sockaddr_in from{};
  std::string asked;
  for (int i = 0; i < 5 && asked != "FIND_AWTRIXNG"; ++i) asked = receive(clock.get(), from);
  check(asked == "FIND_AWTRIXNG" && ntohs(from.sin_port) == replyPort, "and asks the clocks from it");

  const sockaddr_in host = loopback(replyPort);
  os::sendDatagram(clock.get(), "desk-clock", 10, host);
  wire::Packet packet;
  bool offered = false;
  std::string offer;
  for (int i = 0; i < 10 && !offered; ++i) {
    discovery.tick(Discovery::kEveryMs + 1, store);
    offer = receive(clock.get(), from);
    offered = offer.size() >= 4 && std::memcmp(offer.data(), "AWRH", 4) == 0 &&
              wire::decode(reinterpret_cast<const uint8_t*>(offer.data()), offer.size(), packet);
  }
  check(offered && packet.type == wire::Type::Offer && packet.offer.name == "desk" && packet.offer.port == 8300,
        "a clock that answers is offered this host");
  return awtrix::test::finish("hostd discovery");
}
