#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace awtrix {
namespace tc002d {
namespace ip {

namespace mdns {
constexpr uint16_t kPort = 5353;
constexpr uint32_t kGroup = 0xe00000fb;
constexpr uint16_t kTypeA = 1, kTypePtr = 12, kTypeTxt = 16, kTypeSrv = 33, kTypeAny = 255;
constexpr uint32_t kHostTtl = 120, kServiceTtl = 4500, kLegacyTtl = 10;

struct Service {
  std::string type;  // "_http._tcp"
  uint16_t port = 0;
  std::vector<std::string> txt;
};

// What <hostname>.local answers for; the instance name of every service is the hostname.
struct Identity {
  std::string hostname;
  uint32_t address = 0;
  std::vector<Service> services;
};

struct Question {
  std::string name;
  uint16_t type = 0;
  uint16_t klass = 0;
  bool unicastResponse = false;
};

struct Message {
  uint16_t id = 0;
  uint16_t flags = 0;
  std::vector<Question> questions;
  bool response() const { return flags & 0x8000; }
};

bool parseMessage(const uint8_t* data, std::size_t size, Message& out);
// Response to the matching questions, empty when none matches. A legacy query comes from a
// port other than 5353 and gets its id and questions echoed with short TTLs.
std::vector<uint8_t> answer(const Identity& identity, const Message& query, bool legacy);
// Every record unsolicited; goodbye sends them with TTL zero.
std::vector<uint8_t> announcement(const Identity& identity, bool goodbye);
}

// Answers mDNS queries for one IPv4 address on one interface.
class MdnsResponder {
 public:
  MdnsResponder() = default;
  ~MdnsResponder();
  MdnsResponder(const MdnsResponder&) = delete;
  MdnsResponder& operator=(const MdnsResponder&) = delete;

  bool start(const mdns::Identity& identity, int ifindex, uint8_t prefix, int64_t nowMs, std::string& error);
  void stop();
  bool running() const { return fd_ >= 0; }
  const mdns::Identity& identity() const { return identity_; }
  int fd() const { return fd_; }
  void onReadable(int64_t nowMs);
  int64_t nextDeadlineMs() const { return fd_ >= 0 ? announceAtMs_ : -1; }
  void onTime(int64_t nowMs);

 private:
  void send(const std::vector<uint8_t>& bytes, uint32_t address, uint16_t port);

  int fd_ = -1;
  mdns::Identity identity_;
  uint8_t prefix_ = 0;
  unsigned announcementsLeft_ = 0;
  int64_t announceAtMs_ = -1;
  std::vector<uint8_t> lastMulticast_;
  int64_t lastMulticastMs_ = -1;
};

}
}
}
