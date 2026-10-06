#include "platform/tc002/daemon/ip/Mdns.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "platform/tc002/daemon/ip/Ipv4.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace mdns {
namespace {

constexpr uint16_t kClassIn = 1;
constexpr uint16_t kClassAny = 255;
constexpr uint16_t kCacheFlush = 0x8000;
constexpr std::size_t kMaxQuestions = 32;

using Labels = std::vector<std::string>;

struct Record {
  Labels name;
  uint16_t type = 0;
  bool unique = false;
  uint32_t ttl = 0;
  uint32_t address = 0;
  Labels target;
  uint16_t port = 0;
  std::vector<std::string> txt;
};

std::string lower(std::string text) {
  for (char& c : text)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return text;
}

Labels split(const std::string& dotted) {
  Labels out;
  std::size_t begin = 0;
  while (begin <= dotted.size()) {
    const std::size_t dot = dotted.find('.', begin);
    const std::size_t end = dot == std::string::npos ? dotted.size() : dot;
    if (end > begin) out.push_back(dotted.substr(begin, end - begin));
    if (dot == std::string::npos) break;
    begin = dot + 1;
  }
  return out;
}

Labels join(Labels head, const Labels& tail) {
  head.insert(head.end(), tail.begin(), tail.end());
  return head;
}

std::string dotted(const Labels& labels) {
  std::string out;
  for (const std::string& label : labels) {
    if (!out.empty()) out += '.';
    out += label;
  }
  return lower(out);
}

class Writer {
 public:
  std::vector<uint8_t> bytes;
  void u8(uint8_t value) { bytes.push_back(value); }
  void u16(uint16_t value) {
    u8(static_cast<uint8_t>(value >> 8));
    u8(static_cast<uint8_t>(value));
  }
  void u32(uint32_t value) {
    u16(static_cast<uint16_t>(value >> 16));
    u16(static_cast<uint16_t>(value));
  }
  void name(const Labels& labels) {
    for (const std::string& label : labels) {
      const std::size_t size = label.size() > 63 ? 63 : label.size();
      u8(static_cast<uint8_t>(size));
      bytes.insert(bytes.end(), label.begin(), label.begin() + static_cast<std::ptrdiff_t>(size));
    }
    u8(0);
  }
  void patch16(std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value >> 8);
    bytes[at + 1] = static_cast<uint8_t>(value);
  }
};

std::vector<Record> records(const Identity& identity) {
  std::vector<Record> out;
  const Labels host{identity.hostname, "local"};
  Record a;
  a.name = host;
  a.type = kTypeA;
  a.unique = true;
  a.ttl = kHostTtl;
  a.address = identity.address;
  out.push_back(a);
  for (const Service& service : identity.services) {
    const Labels type = join(split(service.type), {"local"});
    const Labels instance = join({identity.hostname}, type);
    Record ptr;
    ptr.name = type;
    ptr.type = kTypePtr;
    ptr.ttl = kServiceTtl;
    ptr.target = instance;
    out.push_back(ptr);
    Record srv;
    srv.name = instance;
    srv.type = kTypeSrv;
    srv.unique = true;
    srv.ttl = kHostTtl;
    srv.port = service.port;
    srv.target = host;
    out.push_back(srv);
    Record txt;
    txt.name = instance;
    txt.type = kTypeTxt;
    txt.unique = true;
    txt.ttl = kServiceTtl;
    txt.txt = service.txt;
    out.push_back(txt);
    Record meta;
    meta.name = {"_services", "_dns-sd", "_udp", "local"};
    meta.type = kTypePtr;
    meta.ttl = kServiceTtl;
    meta.target = type;
    out.push_back(meta);
  }
  return out;
}

void writeRecord(Writer& out, const Record& record, bool legacy, bool goodbye) {
  out.name(record.name);
  out.u16(record.type);
  out.u16(static_cast<uint16_t>(kClassIn | (record.unique && !legacy ? kCacheFlush : 0)));
  out.u32(goodbye ? 0 : legacy && record.ttl > kLegacyTtl ? kLegacyTtl : record.ttl);
  const std::size_t lengthAt = out.bytes.size();
  out.u16(0);
  const std::size_t start = out.bytes.size();
  if (record.type == kTypeA) {
    out.u32(record.address);
  } else if (record.type == kTypePtr) {
    out.name(record.target);
  } else if (record.type == kTypeSrv) {
    out.u16(0);
    out.u16(0);
    out.u16(record.port);
    out.name(record.target);
  } else if (record.type == kTypeTxt) {
    if (record.txt.empty()) out.u8(0);
    for (const std::string& entry : record.txt) {
      const std::size_t size = entry.size() > 255 ? 255 : entry.size();
      out.u8(static_cast<uint8_t>(size));
      out.bytes.insert(out.bytes.end(), entry.begin(), entry.begin() + static_cast<std::ptrdiff_t>(size));
    }
  }
  out.patch16(lengthAt, static_cast<uint16_t>(out.bytes.size() - start));
}

bool contains(const std::vector<const Record*>& list, const Record* record) {
  for (const Record* known : list)
    if (known == record) return true;
  return false;
}

bool readName(const uint8_t* data, std::size_t size, std::size_t& at, std::string& out) {
  out.clear();
  std::size_t position = at;
  bool jumped = false;
  for (int jumps = 0;;) {
    if (position >= size) return false;
    const uint8_t length = data[position];
    if ((length & 0xc0) == 0xc0) {
      if (position + 1 >= size || ++jumps > 16) return false;
      if (!jumped) at = position + 2;
      jumped = true;
      position = (static_cast<std::size_t>(length & 0x3f) << 8) | data[position + 1];
      continue;
    }
    if (length & 0xc0) return false;
    if (length == 0) {
      if (!jumped) at = position + 1;
      out = lower(out);
      return true;
    }
    if (position + 1 + length > size || out.size() + length + 1 > 255) return false;
    if (!out.empty()) out += '.';
    out.append(reinterpret_cast<const char*>(data + position + 1), length);
    position += 1 + length;
  }
}

uint16_t read16(const uint8_t* data) { return static_cast<uint16_t>((data[0] << 8) | data[1]); }

}

bool parseMessage(const uint8_t* data, std::size_t size, Message& out) {
  out = Message{};
  if (size < 12) return false;
  out.id = read16(data);
  out.flags = read16(data + 2);
  if ((out.flags >> 11) & 0xf) return false;
  const uint16_t count = read16(data + 4);
  if (count > kMaxQuestions) return false;
  std::size_t at = 12;
  for (uint16_t i = 0; i < count; ++i) {
    Question question;
    if (!readName(data, size, at, question.name) || at + 4 > size) return false;
    question.type = read16(data + at);
    const uint16_t klass = read16(data + at + 2);
    question.klass = klass & 0x7fff;
    question.unicastResponse = klass & 0x8000;
    at += 4;
    out.questions.push_back(question);
  }
  return true;
}

std::vector<uint8_t> answer(const Identity& identity, const Message& query, bool legacy) {
  if (query.response() || identity.hostname.empty() || !identity.address) return {};
  const std::vector<Record> all = records(identity);
  std::vector<const Record*> answers, additional;
  for (const Question& question : query.questions) {
    if (question.klass != kClassIn && question.klass != kClassAny) continue;
    for (const Record& record : all) {
      if ((question.type == record.type || question.type == kTypeAny) && dotted(record.name) == question.name &&
          !contains(answers, &record))
        answers.push_back(&record);
    }
  }
  if (answers.empty()) return {};
  for (const Record* given : answers) {
    const std::string target = given->type == kTypePtr ? dotted(given->target) : std::string();
    bool pointsAtInstance = false;
    for (const Record& record : all) pointsAtInstance = pointsAtInstance || (record.type == kTypeSrv && dotted(record.name) == target);
    for (const Record& record : all) {
      const bool instanceDetail =
          pointsAtInstance && (record.type == kTypeSrv || record.type == kTypeTxt) && dotted(record.name) == target;
      const bool hostAddress = record.type == kTypeA && (given->type == kTypeSrv || pointsAtInstance);
      if ((instanceDetail || hostAddress) && !contains(answers, &record) && !contains(additional, &record))
        additional.push_back(&record);
    }
  }
  Writer out;
  out.u16(legacy ? query.id : 0);
  out.u16(0x8400);
  out.u16(legacy ? static_cast<uint16_t>(query.questions.size()) : 0);
  out.u16(static_cast<uint16_t>(answers.size()));
  out.u16(0);
  out.u16(static_cast<uint16_t>(additional.size()));
  if (legacy) {
    for (const Question& question : query.questions) {
      out.name(split(question.name));
      out.u16(question.type);
      out.u16(question.klass);
    }
  }
  for (const Record* record : answers) writeRecord(out, *record, legacy, false);
  for (const Record* record : additional) writeRecord(out, *record, legacy, false);
  return out.bytes;
}

std::vector<uint8_t> announcement(const Identity& identity, bool goodbye) {
  if (identity.hostname.empty() || !identity.address) return {};
  const std::vector<Record> all = records(identity);
  Writer out;
  out.u16(0);
  out.u16(0x8400);
  out.u16(0);
  out.u16(static_cast<uint16_t>(all.size()));
  out.u16(0);
  out.u16(0);
  for (const Record& record : all) writeRecord(out, record, false, goodbye);
  return out.bytes;
}

}

MdnsResponder::~MdnsResponder() { stop(); }

bool MdnsResponder::start(const mdns::Identity& identity, int ifindex, uint8_t prefix, int64_t nowMs,
                          std::string& error) {
  stop();
  const auto failed = [&](const char* what) {
    error = std::string(what) + ": " + std::strerror(errno);
    stop();
    return false;
  };
  fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd_ < 0) return failed("mdns socket");
  const int yes = 1, ttl = 255, no = 0;
  ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(mdns::kPort);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) return failed("mdns bind");
  ip_mreqn membership{};
  membership.imr_multiaddr.s_addr = htonl(mdns::kGroup);
  membership.imr_address.s_addr = htonl(identity.address);
  membership.imr_ifindex = ifindex;
  if (::setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0)
    return failed("mdns group");
  ip_mreqn outgoing{};
  outgoing.imr_address.s_addr = htonl(identity.address);
  outgoing.imr_ifindex = ifindex;
  if (::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &outgoing, sizeof(outgoing)) != 0)
    return failed("mdns interface");
  ::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  ::setsockopt(fd_, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
  ::setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &no, sizeof(no));
  identity_ = identity;
  prefix_ = prefix;
  announcementsLeft_ = 2;
  announceAtMs_ = nowMs;
  lastMulticast_.clear();
  lastMulticastMs_ = -1;
  return true;
}

void MdnsResponder::stop() {
  if (fd_ < 0) return;
  send(mdns::announcement(identity_, true), mdns::kGroup, mdns::kPort);
  ::close(fd_);
  fd_ = -1;
  announcementsLeft_ = 0;
  announceAtMs_ = -1;
}

void MdnsResponder::send(const std::vector<uint8_t>& bytes, uint32_t address, uint16_t port) {
  if (fd_ < 0 || bytes.empty()) return;
  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(port);
  peer.sin_addr.s_addr = htonl(address);
  const ssize_t sent = ::sendto(fd_, bytes.data(), bytes.size(), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&peer),
                                sizeof(peer));
  (void)sent;
}

void MdnsResponder::onTime(int64_t nowMs) {
  if (fd_ < 0 || announceAtMs_ < 0 || nowMs < announceAtMs_) return;
  const std::vector<uint8_t> bytes = mdns::announcement(identity_, false);
  send(bytes, mdns::kGroup, mdns::kPort);
  lastMulticast_ = bytes;
  lastMulticastMs_ = nowMs;
  announceAtMs_ = --announcementsLeft_ ? nowMs + 1000 : -1;
}

void MdnsResponder::onReadable(int64_t nowMs) {
  for (int round = 0; round < 8 && fd_ >= 0; ++round) {
    uint8_t buffer[1500];
    sockaddr_in source{};
    socklen_t sourceSize = sizeof(source);
    const ssize_t n = ::recvfrom(fd_, buffer, sizeof(buffer), MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&source),
                                 &sourceSize);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return;
    mdns::Message query;
    if (!mdns::parseMessage(buffer, static_cast<std::size_t>(n), query) || query.response()) continue;
    const uint32_t from = ntohl(source.sin_addr.s_addr);
    const uint16_t port = ntohs(source.sin_port);
    if (prefix_ >= 1 && prefix_ <= 30 && !sameSubnet(from, identity_.address, prefix_)) continue;
    const bool legacy = port != mdns::kPort;
    bool unicast = legacy;
    for (const mdns::Question& question : query.questions) unicast = unicast || question.unicastResponse;
    const std::vector<uint8_t> bytes = mdns::answer(identity_, query, legacy);
    if (bytes.empty()) continue;
    if (unicast) {
      send(bytes, from, port);
      continue;
    }
    if (bytes == lastMulticast_ && lastMulticastMs_ >= 0 && nowMs - lastMulticastMs_ < 1000) continue;
    send(bytes, mdns::kGroup, mdns::kPort);
    lastMulticast_ = bytes;
    lastMulticastMs_ = nowMs;
  }
}

}
}
}
