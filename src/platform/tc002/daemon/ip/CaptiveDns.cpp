#include "platform/tc002/daemon/ip/CaptiveDns.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iterator>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

namespace awtrix {
namespace tc002d {
namespace ip {
namespace {

uint16_t word(const uint8_t* p) { return static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1]); }

}

std::vector<uint8_t> captiveDnsReply(const uint8_t* packet, std::size_t size) {
  if (!packet || size < 17 || size > kMaxCaptiveDnsPacket || (word(packet + 2) & 0xfa00) ||
      word(packet + 4) != 1 || word(packet + 6) || word(packet + 8) || word(packet + 10) > 1)
    return {};
  std::size_t at = 12;
  for (;;) {
    if (at >= size || at - 12 >= 255) return {};
    const uint8_t count = packet[at++];
    if (!count) break;
    if (count > 63 || count > size - at || at - 12 + count >= 255) return {};
    at += count;
  }
  if (at == 13 || size - at < 4 || word(packet + at + 2) != 1) return {};
  const bool answer = word(packet + at) == 1;
  const std::size_t questionEnd = at + 4;
  at = questionEnd;
  if (word(packet + 10)) {
    if (size - at < 11 || packet[at] != 0 || word(packet + at + 1) != 41) return {};
    const uint16_t length = word(packet + at + 9);
    at += 11;
    if (length != size - at) return {};
    const std::size_t end = at + length;
    while (at < end) {
      if (end - at < 4) return {};
      const uint16_t optionLength = word(packet + at + 2);
      at += 4;
      if (optionLength > end - at) return {};
      at += optionLength;
    }
  }
  if (at != size) return {};
  std::vector<uint8_t> out(packet, packet + questionEnd);
  out[2] = static_cast<uint8_t>(0x84 | (packet[2] & 1));
  out[3] = 0;
  out[7] = answer ? 1 : 0;
  out[10] = out[11] = 0;
  if (answer) {
    const uint8_t record[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4,
        static_cast<uint8_t>(kAccessPointAddress >> 24), static_cast<uint8_t>(kAccessPointAddress >> 16),
        static_cast<uint8_t>(kAccessPointAddress >> 8), static_cast<uint8_t>(kAccessPointAddress)};
    out.insert(out.end(), std::begin(record), std::end(record));
  }
  return out;
}

CaptiveDns::~CaptiveDns() { stop(); }

bool CaptiveDns::start(const std::string& interfaceName, std::string& error) {
  stop();
  if (interfaceName.empty() || interfaceName.size() >= IFNAMSIZ) {
    error = "invalid captive DNS interface";
    return false;
  }
  fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(53);
  address.sin_addr.s_addr = htonl(kAccessPointAddress);
  if (fd_ < 0 || ::setsockopt(fd_, SOL_SOCKET, SO_BINDTODEVICE, interfaceName.c_str(),
                             static_cast<socklen_t>(interfaceName.size() + 1)) != 0 ||
      ::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    error = std::string("captive DNS socket: ") + std::strerror(errno);
    stop();
    return false;
  }
  return true;
}

void CaptiveDns::stop() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

void CaptiveDns::onReadable() {
  if (fd_ < 0) return;
  uint8_t buffer[kMaxCaptiveDnsPacket + 1];
  for (unsigned round = 0; round < 8; ++round) {
    sockaddr_in peer{};
    socklen_t length = sizeof(peer);
    const ssize_t n = ::recvfrom(fd_, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&peer), &length);
    if (n < 0) {
      if (errno == EINTR) continue;
      return;
    }
    const uint32_t source = ntohl(peer.sin_addr.s_addr);
    if (length != sizeof(peer) || peer.sin_family != AF_INET || !peer.sin_port ||
        (source & 0xffffff00) != (kAccessPointAddress & 0xffffff00) ||
        (source & 0xff) < 2 || (source & 0xff) == 255)
      continue;
    const auto response = captiveDnsReply(buffer, static_cast<std::size_t>(n));
    if (!response.empty())
      ::sendto(fd_, response.data(), response.size(), MSG_NOSIGNAL,
               reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
  }
}

}
}
}
