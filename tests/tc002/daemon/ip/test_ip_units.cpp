#include <arpa/inet.h>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <linux/if_addr.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "platform/tc002/daemon/ip/ClientOutput.h"
#include "platform/tc002/daemon/ip/CaptiveDns.h"
#include "platform/tc002/daemon/ip/DhcpLease.h"
#include "platform/tc002/daemon/ip/DhcpRecordFormat.h"
#include "platform/tc002/daemon/ip/Hostname.h"
#include "platform/tc002/daemon/ip/LeaseApplier.h"
#include "platform/tc002/daemon/ip/Mdns.h"
#include "platform/tc002/daemon/ip/ResolvConf.h"
#include "platform/tc002/daemon/ip/Sntp.h"
#include "platform/tc002/daemon/ip/SystemPlatform.h"
#include "support.h"

using namespace awtrix::tc002d::ip;
using ip_test::check;
using ip_test::ipv4;

namespace {

void testCaptiveDns() {
  const std::vector<uint8_t> query = {0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                      7, 'c', 'a', 'p', 't', 'i', 'v', 'e', 4, 't', 'e', 's', 't', 0, 0, 1, 0, 1};
  const auto reply = captiveDnsReply(query.data(), query.size());
  check(reply.size() == query.size() + 16 && reply[0] == 0x12 && reply[1] == 0x34 &&
            reply[2] == 0x85 && reply[7] == 1 &&
            std::vector<uint8_t>(reply.end() - 4, reply.end()) == std::vector<uint8_t>({192, 168, 4, 1}),
        "captive A reply preserves ID/question and returns only the AP address");
  for (std::size_t size = 0; size < query.size(); ++size)
    check(captiveDnsReply(query.data(), size).empty(), "truncated DNS query rejected");
  auto aaaa = query;
  aaaa[aaaa.size() - 3] = 28;
  const auto negative = captiveDnsReply(aaaa.data(), aaaa.size());
  check(negative.size() == aaaa.size() && negative[7] == 0, "AAAA receives an empty local answer without forwarding");
  auto edns = query;
  edns[11] = 1;
  const uint8_t opt[] = {0, 0, 41, 4, 208, 0, 0, 0x80, 0, 0, 8, 0, 12, 0, 4, 0, 0, 0, 0};
  edns.insert(edns.end(), std::begin(opt), std::end(opt));
  check(captiveDnsReply(edns.data(), edns.size()) == reply, "bounded EDNS padding is accepted without copying options");
  for (unsigned mutation = 0; mutation < 9; ++mutation) {
    auto bad = query;
    if (mutation == 0) bad[2] = 0x81;
    if (mutation == 1) bad[2] = 0x09;
    if (mutation == 2) bad[5] = 2;
    if (mutation == 3) bad[12] = 0xc0;
    if (mutation == 4) bad.push_back(0);
    if (mutation == 5) bad.at(query.size() - 1) = 3;
    if (mutation == 6) bad.resize(kMaxCaptiveDnsPacket + 1);
    if (mutation == 7) { bad = edns; bad.at(bad.size() - 1) = 1; bad.at(bad.size() - 6) = 0xff; }
    if (mutation == 8) bad[7] = 1;
    check(captiveDnsReply(bad.data(), bad.size()).empty(), "malformed or unsupported DNS structure rejected");
  }
  check(captiveDnsReply(nullptr, 50).empty(), "null DNS buffer rejected");
}

void testAccessPointFiles() {
  char temporary[] = "/tmp/awtrix-ap-files-XXXXXX";
  const char* created = ::mkdtemp(temporary);
  check(created != nullptr, "private AP fixture directory");
  if (!created) return;
  const std::string directory = created, ap = directory + "/access-point";
  SystemPlatform platform(directory);
  std::string error;
  check(platform.prepareAccessPoint(directory, "interface wlan0\n", error), "private AP files prepared: " + error);
  struct stat info{};
  check(::stat(ap.c_str(), &info) == 0 && (info.st_mode & 0777) == 0700, "AP directory is owner-only");
  for (const char* name : {"leases", "udhcpd.conf"}) {
    check(::stat((ap + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
              (info.st_mode & 0777) == 0600, "AP files are private regular files");
  }
  const std::string victim = directory + "/victim";
  int fd = ::open(victim.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  check(fd >= 0 && ::write(fd, "safe", 4) == 4, "private file target fixture");
  if (fd >= 0) ::close(fd);
  ::unlink((ap + "/leases").c_str());
  check(::symlink(victim.c_str(), (ap + "/leases").c_str()) == 0 &&
            !platform.prepareAccessPoint(directory, "replacement", error), "AP lease symlink is refused");
  ::unlink((ap + "/leases").c_str());
  check(::link(victim.c_str(), (ap + "/leases").c_str()) == 0 &&
            !platform.prepareAccessPoint(directory, "replacement", error), "AP lease hardlink is refused before truncation");
  check(::stat(victim.c_str(), &info) == 0 && info.st_size == 4, "refused AP files preserve the target");
  platform.removeAccessPoint(directory);
  check(::access((ap + "/leases").c_str(), F_OK) != 0 &&
            ::access((ap + "/udhcpd.conf").c_str(), F_OK) != 0, "AP cleanup removes only managed files");
  ::chmod(ap.c_str(), 0755);
  check(!platform.prepareAccessPoint(directory, "x", error), "nonprivate AP directory is refused");
  check(!platform.prepareAccessPoint(directory + "/../escape", "x", error), "AP config path traversal is refused");
  ::unlink(victim.c_str());
  ::rmdir(ap.c_str());
  ::rmdir(directory.c_str());
}

void testIpv4() {
  uint32_t address = 0;
  check(parseIpv4("192.0.2.57", address) && address == 0xc0000239, "dotted quad parses");
  for (const char* bad : {"01.2.3.4", "256.1.1.1", "1.2.3", "1.2.3.4.5", "1.2.3.4 ", "", "1..2.3", "a.b.c.d", "1234.1.1.1"})
    check(!parseIpv4(bad, address), std::string("rejects ") + bad);
  uint8_t prefix = 0;
  check(maskPrefix(0xffffff00, prefix) && prefix == 24, "mask /24");
  check(maskPrefix(0xffffffff, prefix) && prefix == 32, "mask /32");
  check(!maskPrefix(0xff00ff00, prefix), "non-contiguous mask");
  check(prefixMask(0) == 0 && prefixMask(32) == UINT32_MAX && prefixMask(8) == 0xff000000, "prefix masks");
  check(isUnicast(ipv4("10.0.0.1")) && !isUnicast(ipv4("127.0.0.1")) && !isUnicast(ipv4("224.0.0.251")) &&
            !isUnicast(0),
        "unicast classification");
}

std::size_t format(const std::map<std::string, std::string>& environment, const char* event, char* out,
                   std::size_t capacity, int64_t mono = 1234) {
  return formatDhcpRecord(
      event, mono,
      [&](const char* key) -> const char* {
        const auto found = environment.find(key);
        return found == environment.end() ? nullptr : found->second.c_str();
      },
      out, capacity);
}

const std::map<std::string, std::string> kBound = {
    {"interface", "wlan0"},       {"ip", "192.0.2.57"},          {"subnet", "255.255.255.0"},
    {"router", "192.0.2.1"},  {"dns", "192.0.2.1 8.8.8.8"}, {"lease", "864000"},
    {"ntpsrv", "192.0.2.1"},  {"hostname", "ignored"},          {"broadcast", "192.0.2.255"}};

void testRecordFormat() {
  char out[kMaxDhcpRecord];
  const std::size_t size = format(kBound, "bound", out, sizeof(out));
  check(std::string(out, size) ==
            "v=1\tevent=bound\tmono=1234\tinterface=wlan0\tip=192.0.2.57\tsubnet=255.255.255.0\t"
            "router=192.0.2.1\tdns=192.0.2.1 8.8.8.8\tbroadcast=192.0.2.255\tlease=864000\t"
            "ntpsrv=192.0.2.1\n",
        "bound record text");
  auto hostile = kBound;
  hostile["dns"] = "1.1.1.1\tevent=nak";
  hostile["router"] = "10.0.0.1\n";
  const std::size_t hostileSize = format(hostile, "bound", out, sizeof(out));
  const std::string text(out, hostileSize);
  check(text.find("dns=") == std::string::npos && text.find("router=") == std::string::npos &&
            text.find("event=nak") == std::string::npos,
        "control characters drop the field");
  check(format(kBound, "bound\n", out, sizeof(out)) == 0, "control characters in the event refuse the record");
  check(format(kBound, "bound", out, 64) == 0, "record larger than the buffer is refused");
  auto longValue = kBound;
  longValue["dns"] = std::string(300, '1');
  check(std::string(out, format(longValue, "renew", out, sizeof(out))).find("dns=") == std::string::npos,
        "overlong value dropped");
  check(std::string(out, format({{"interface", "wlan0"}}, "deconfig", out, sizeof(out), -1)) ==
            "v=1\tevent=deconfig\tinterface=wlan0\n",
        "deconfig without capture time");
}

bool parse(const std::string& line, DhcpRecord& record, std::string& error) {
  return parseDhcpRecord(line, "wlan0", record, error);
}

std::string line(std::map<std::string, std::string> environment, const char* event = "bound") {
  char out[kMaxDhcpRecord];
  const std::size_t size = format(environment, event, out, sizeof(out));
  return std::string(out, size ? size - 1 : 0);
}

void testLeaseParse() {
  DhcpRecord record;
  std::string error;
  check(parse(line(kBound), record, error), "bound parses: " + error);
  check(record.event == DhcpEvent::Bound && record.capturedMs == 1234, "event and capture time");
  const DhcpLease& lease = record.lease;
  check(lease.address == ipv4("192.0.2.57") && lease.prefix == 24 && lease.router == ipv4("192.0.2.1") &&
            lease.broadcast == ipv4("192.0.2.255") && lease.leaseSeconds == 864000,
        "lease addressing");
  check(lease.dns.size() == 2 && lease.dns[1] == ipv4("8.8.8.8") && lease.ntp.size() == 1, "dns and ntp lists");

  auto variant = kBound;
  variant["router"] = "192.0.2.57 192.0.2.254 192.0.2.1";
  variant["dns"] = "1.1.1.1 bogus 8.8.8.8 9.9.9.9 8.8.4.4 1.1.1.1";
  variant.erase("lease");
  check(parse(line(variant), record, error), "variant parses");
  check(record.lease.router == ipv4("192.0.2.254"), "router equal to own address skipped");
  check(record.lease.dns.size() == 3 && record.lease.dns[2] == ipv4("9.9.9.9"), "dns capped at three, bogus dropped");
  check(record.lease.leaseSeconds == kDefaultLeaseSeconds && !record.notes.empty(), "missing lease time defaults");

  variant = kBound;
  variant["lease"] = "4294967295";
  check(parse(line(variant), record, error) && record.lease.leaseSeconds == kInfiniteLease, "infinite lease");
  variant["lease"] = "4294967296";
  check(parse(line(variant), record, error) && record.lease.leaseSeconds == kDefaultLeaseSeconds, "overflowing lease");

  variant = kBound;
  variant["subnet"] = "255.255.255.255";
  variant["router"] = "10.0.0.1";
  check(parse(line(variant), record, error) && record.lease.prefix == 32 && record.lease.broadcast == 0 &&
            record.lease.router == ipv4("10.0.0.1"),
        "/32 lease with a router outside the subnet");

  for (const auto& broken : std::vector<std::pair<const char*, const char*>>{
           {"subnet", "255.0.255.0"}, {"ip", "192.0.2.0"}, {"ip", "192.0.2.255"}, {"ip", "127.0.0.1"},
           {"subnet", "0.0.0.0"}, {"ip", "x"}}) {
    variant = kBound;
    variant[broken.first] = broken.second;
    check(!parse(line(variant), record, error), std::string("rejects ") + broken.first + "=" + broken.second);
  }
  variant = kBound;
  variant.erase("subnet");
  check(!parse(line(variant), record, error), "bound without subnet");
  variant = kBound;
  variant["interface"] = "eth0";
  check(!parse(line(variant), record, error) && error == "foreign interface", "foreign interface");
  check(!parse(line(kBound, "expire"), record, error), "unknown event");
  check(!parse("v=1\tevent=bound\tevent=nak\tinterface=wlan0", record, error), "duplicate key");
  check(!parse("v=2\tevent=bound\tinterface=wlan0", record, error), "unsupported version");
  check(!parse("v=1\tevent=bound\tgarbage\tinterface=wlan0", record, error), "field without separator");
  check(parse("v=1\tevent=deconfig\tinterface=wlan0", record, error) && record.event == DhcpEvent::Deconfig &&
            record.capturedMs == -1,
        "deconfig needs no lease fields");
  check(parse("v=1\tevent=leasefail\tinterface=wlan0\tfuture=1", record, error), "unknown fields are ignored");
}

void testStaticLease() {
  awtrix::tc002::StaticAddress wanted{true, "192.0.2.50", "255.255.255.0", "192.0.2.1", "", "1.1.1.1"};
  DhcpLease lease;
  std::string error;
  check(staticLease(wanted, lease, error) && lease.address == ipv4("192.0.2.50") && lease.prefix == 24 &&
            lease.broadcast == ipv4("192.0.2.255") && lease.router == ipv4("192.0.2.1") &&
            lease.dns == std::vector<uint32_t>{ipv4("192.0.2.1"), ipv4("1.1.1.1")} &&
            lease.leaseSeconds == kInfiniteLease,
        "static lease, empty dns1 falls back to the gateway");
  DhcpRecord record;
  check(parse(formatLease(lease, "wlan0"), record, error) && record.lease == lease && record.notes.empty(),
        "a formatted lease reads back unchanged: " + error);

  wanted = {true, "10.0.0.7", "255.255.255.255", "0.0.0.0", "0.0.0.0", ""};
  check(staticLease(wanted, lease, error) && lease.prefix == 32 && !lease.router && lease.dns.empty(),
        "0.0.0.0 means no gateway and no DNS server");
  check(parse(formatLease(lease, "wlan0"), record, error) && record.lease == lease, "/32 without router reads back");

  for (const auto& broken : std::vector<std::pair<awtrix::tc002::StaticAddress, const char*>>{
           {{true, "192.0.2.255", "255.255.255.0", "", "", ""}, "address is not a host in its subnet"},
           {{true, "192.0.2.50", "255.0.255.0", "", "", ""}, "invalid subnet"},
           {{true, "192.000.2.50", "255.255.255.0", "", "", ""}, "invalid ip"},
           {{true, "192.0.2.50", "255.255.255.0", "192.0.2.50", "", ""}, "invalid gateway"},
           {{true, "192.0.2.50", "255.255.255.0", "", "239.0.0.1", ""}, "invalid dns"},
           {{true, "192.0.2.50 ", "255.255.255.0", "", "", ""}, "invalid ip"}}) {
    error.clear();
    check(!staticLease(broken.first, lease, error) && error == broken.second,
          "rejects " + broken.first.ip + ": " + error);
  }
}

void testResolv() {
  check(resolvContent({ipv4("192.0.2.1"), ipv4("8.8.8.8")}, ipv4("192.0.2.1")) ==
            "nameserver 192.0.2.1\nnameserver 8.8.8.8\n",
        "resolv.conf from lease DNS");
  check(resolvContent({}, ipv4("10.0.0.1")) == "nameserver 10.0.0.1\n", "resolv.conf falls back to the gateway");
  check(resolvContent({}, 0).empty(), "nothing to write without DNS or gateway");
  const std::string mounts =
      "22 1 179:2 / / ro,relatime - squashfs /dev/root ro\n"
      "40 22 0:18 /awtrix/resolv.conf /etc/resolv.conf rw - tmpfs tmpfs rw\n"
      "41 40 0:18 /awtrix/resolv.conf /etc/resolv.conf rw - tmpfs tmpfs rw\n"
      "42 22 0:18 /x /etc/resolv.conf.bak rw - tmpfs tmpfs rw\n"
      "43 22 0:18 /x /etc/my\\040dir rw - tmpfs tmpfs rw";
  check(countMountsAt(mounts, "/etc/resolv.conf") == 2, "stacked binds are counted");
  check(countMountsAt(mounts, "/etc/my dir") == 1, "escaped mount points");
  check(countMountsAt(mounts, "/etc/hosts") == 0, "no mount");
}

void testHostname() {
  check(validHostname("awtrix-000007") && validHostname("A1") && validHostname(std::string(63, 'a')),
        "valid hostnames");
  for (const std::string& bad : {std::string(), std::string("-a"), std::string("a-"), std::string("a.b"),
                                 std::string("a_b"), std::string(64, 'a'), std::string("a b")})
    check(!validHostname(bad), "rejects hostname '" + bad + "'");
  check(hostnameForMac("02:00:00:00:00:07") == "awtrixng-000007", "default hostname as on ESP32");
  check(hostnameForMac("02:00:00:0A:BC:DE\n") == "awtrixng-0abcde", "sysfs MAC with newline, upper-case hex");
  check(hostnameForMac("02:00:00:0a:bc:de") == awtrix::net::defaultHostname("02:00:00:0A:BC:DE"),
        "same name as the ESP32 helper for WiFi.macAddress()");
  check(hostnameForMac("02:00:00:00:00").empty() && hostnameForMac("zz:00:00:00:00:07").empty(), "invalid MAC");
  check(macId("02:00:00:00:00:07") == "020000000007", "mdns id from MAC");
}

sntp::Reply serverReply(uint64_t cookie, int64_t receiveNs, int64_t transmitNs, uint8_t stratum = 2) {
  sntp::Reply reply;
  reply.leap = 0;
  reply.version = 4;
  reply.mode = 4;
  reply.stratum = stratum;
  reply.originate = cookie;
  reply.receive = sntp::unixNsToNtp(receiveNs);
  reply.transmit = sntp::unixNsToNtp(transmitNs);
  return reply;
}

void encodeReply(const sntp::Reply& reply, uint8_t (&out)[sntp::kPacketSize]) {
  std::memset(out, 0, sizeof(out));
  out[0] = static_cast<uint8_t>((reply.leap << 6) | (reply.version << 3) | reply.mode);
  out[1] = reply.stratum;
  const uint64_t stamps[3] = {reply.originate, reply.receive, reply.transmit};
  for (int s = 0; s < 3; ++s)
    for (int i = 0; i < 8; ++i) out[24 + s * 8 + i] = static_cast<uint8_t>(stamps[s] >> (56 - 8 * i));
}

void testSntpPackets() {
  uint8_t request[sntp::kPacketSize];
  sntp::encodeRequest(0x0102030405060708ull, request);
  check(request[0] == 0x23, "client request header");
  bool rest = true;
  for (int i = 1; i < 40; ++i) rest = rest && request[i] == 0;
  check(rest && request[40] == 1 && request[47] == 8, "cookie in transmit timestamp");

  const int64_t second = 1000000000;
  const int64_t now = INT64_C(1790000000) * second;
  uint8_t bytes[sntp::kPacketSize];
  encodeReply(serverReply(42, now, now + 2000000), bytes);
  sntp::Reply reply;
  const char* error = nullptr;
  check(sntp::decodeReply(bytes, sizeof(bytes), 42, reply, error), "server reply decodes");
  check(!sntp::decodeReply(bytes, sizeof(bytes), 43, reply, error) && std::string(error) == "reply to another request",
        "foreign cookie rejected");
  check(!sntp::decodeReply(bytes, 47, 42, reply, error), "short reply rejected");
  encodeReply(serverReply(42, now, now, 0), bytes);
  check(!sntp::decodeReply(bytes, sizeof(bytes), 42, reply, error) && std::string(error) == "kiss-o'-death",
        "kiss-o'-death rejected");
  sntp::Reply unsynchronized = serverReply(42, now, now);
  unsynchronized.leap = 3;
  encodeReply(unsynchronized, bytes);
  check(!sntp::decodeReply(bytes, sizeof(bytes), 42, reply, error), "unsynchronized server rejected");
  sntp::Reply client = serverReply(42, now, now);
  client.mode = 3;
  encodeReply(client, bytes);
  check(!sntp::decodeReply(bytes, sizeof(bytes), 42, reply, error), "client mode rejected");

  check(sntp::ntpToUnixNs(uint64_t(2208988800u) << 32) == 0, "NTP epoch offset");
  for (int64_t unixSeconds : {INT64_C(0), INT64_C(1790000000), INT64_C(2085978495), INT64_C(2085978496),
                              INT64_C(2200000000), INT64_C(4000000000)}) {
    const int64_t value = unixSeconds * second + 123456789;
    const int64_t back = sntp::ntpToUnixNs(sntp::unixNsToNtp(value));
    check(back - value < 2 && value - back < 2, "NTP era round trip at " + std::to_string(unixSeconds));
  }

  sntp::Sample result;
  const sntp::Reply measured = serverReply(42, now, now + 2 * 1000000);
  check(sntp::sample(measured, 1000 * 1000000, 1040 * 1000000, 5 * second, result), "sample");
  check(result.delayNs > 38 * 1000000 - 5 && result.delayNs < 38 * 1000000 + 5, "round trip minus server time");
  const int64_t expectedOffset = now + 2 * 1000000 + 19 * 1000000 - 5 * second;
  check(result.offsetNs > expectedOffset - 5 && result.offsetNs < expectedOffset + 5, "offset from a 1970 clock");
  check(!sntp::sample(measured, 2000, 1000, 0, result), "negative round trip rejected");
  check(!sntp::sample(measured, 0, 11 * second, 0, result), "overlong round trip rejected");
}

class FakeClock : public SystemClock {
 public:
  int64_t realtime = 3 * 1000000000LL;
  std::vector<int64_t> steps, slews;
  int64_t monotonicNs() override {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return int64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
  }
  int64_t realtimeNs() override { return realtime; }
  bool step(int64_t offset) override {
    steps.push_back(offset);
    realtime += offset;
    return true;
  }
  bool slew(int64_t offset) override {
    slews.push_back(offset);
    return true;
  }
};

int64_t nowMs() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return int64_t(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

// Runs the client's descriptors and timers until done() or the budget ends; server answers.
template <typename Done, typename Serve>
void drive(SntpClient& client, int server, Done done, Serve serve, int64_t budgetMs = 3000) {
  const int64_t end = nowMs() + budgetMs;
  while (!done() && nowMs() < end) {
    std::vector<awtrix::tc002d::PollInterest> interest;
    client.pollInterest(interest);
    std::vector<pollfd> fds;
    for (const auto& entry : interest) fds.push_back({entry.fd, entry.events, 0});
    fds.push_back({server, POLLIN, 0});
    ::poll(fds.data(), fds.size(), 20);
    for (std::size_t i = 0; i + 1 < fds.size(); ++i)
      if (fds[i].revents) client.onReady(fds[i].fd, fds[i].revents, nowMs());
    if (fds.back().revents & POLLIN) serve();
    int status = 0;
    for (pid_t child; (child = ::waitpid(-1, &status, WNOHANG)) > 0;) client.onChildExit(child);
    const int64_t deadline = client.nextDeadlineMs();
    if (deadline >= 0 && deadline <= nowMs()) client.onTime(nowMs());
  }
}

void testSntpClient() {
  const int server = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof(local);
  check(::bind(server, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0 &&
            ::getsockname(server, reinterpret_cast<sockaddr*>(&local), &size) == 0,
        "loopback SNTP server");
  SntpTiming timing;
  timing.port = ntohs(local.sin_port);
  timing.replyTimeoutMs = 300;
  const int64_t second = 1000000000;
  const int64_t serverTime = INT64_C(1790000000) * second;
  int answered = 0;
  uint8_t stratum = 2;
  const auto serve = [&] {
    uint8_t request[128];
    sockaddr_in peer{};
    socklen_t peerSize = sizeof(peer);
    const ssize_t n = ::recvfrom(server, request, sizeof(request), 0, reinterpret_cast<sockaddr*>(&peer), &peerSize);
    if (n != 48) return;
    uint64_t cookie = 0;
    for (int i = 0; i < 8; ++i) cookie = (cookie << 8) | request[40 + i];
    uint8_t reply[sntp::kPacketSize];
    encodeReply(serverReply(cookie, serverTime, serverTime, stratum), reply);
    ::sendto(server, reply, sizeof(reply), 0, reinterpret_cast<sockaddr*>(&peer), peerSize);
    ++answered;
  };

  FakeClock clock;
  SntpClient client(clock, timing);
  std::vector<std::string> lines;
  client.log = [&](const std::string& text) { lines.push_back(text); };
  int changes = 0;
  client.synchronizedChanged = [&] { ++changes; };
  client.setServer("127.0.0.1", nowMs());
  check(client.nextDeadlineMs() < 0, "offline client waits");
  client.setOnline(true, nowMs());
  drive(client, server, [&] { return client.synchronized(); }, serve);
  check(client.synchronized() && changes == 1 && answered == 1, "first sync over loopback");
  check(clock.steps.size() == 1 && clock.steps[0] > serverTime - 4 * second && clock.steps[0] < serverTime,
        "first sync steps the 1970 clock");
  check(client.nextDeadlineMs() >= nowMs() + timing.pollMs - 1000, "next poll in an hour");

  clock.realtime = serverTime - 200 * 1000000;
  client.setServer("localhost", nowMs());
  drive(client, server, [&] { return answered == 2 && client.nextDeadlineMs() > nowMs() + 60000; }, serve);
  check(answered == 2 && clock.slews.size() == 1 && clock.steps.size() == 1, "resolver child path, small offset slews");
  check(!client.childRunning(), "resolver child reaped");

  stratum = 0;
  client.setServer("127.0.0.1", nowMs());
  const int64_t before = nowMs();
  drive(client, server, [&] { return client.nextDeadlineMs() > before + 5000; }, serve);
  check(client.nextDeadlineMs() >= before + timing.firstRetryMs && client.nextDeadlineMs() < before + 2 * timing.firstRetryMs,
        "failure retries after the first backoff");
  check(!lines.empty() && lines.back().find("retry in 10 s") != std::string::npos, "failure is logged");
  client.stop();
  ::close(server);
}

struct Record {
  std::string name;
  uint16_t type = 0, klass = 0;
  uint32_t ttl = 0;
  std::vector<uint8_t> data;
  size_t dataAt = 0;
};

struct Response {
  uint16_t id = 0, flags = 0;
  std::vector<std::string> questions;
  std::vector<Record> answers, additional;
};

bool readName(const std::vector<uint8_t>& bytes, size_t& at, std::string& out) {
  out.clear();
  size_t position = at;
  bool jumped = false;
  for (int guard = 0; guard < 64; ++guard) {
    if (position >= bytes.size()) return false;
    const uint8_t length = bytes[position];
    if ((length & 0xc0) == 0xc0) {
      if (!jumped) at = position + 2;
      jumped = true;
      position = ((length & 0x3f) << 8) | bytes[position + 1];
      continue;
    }
    if (!length) {
      if (!jumped) at = position + 1;
      return true;
    }
    if (!out.empty()) out += '.';
    out.append(reinterpret_cast<const char*>(&bytes[position + 1]), length);
    position += 1 + length;
  }
  return false;
}

bool decode(const std::vector<uint8_t>& bytes, Response& out) {
  if (bytes.size() < 12) return false;
  const auto u16 = [&](size_t at) { return static_cast<uint16_t>((bytes[at] << 8) | bytes[at + 1]); };
  out.id = u16(0);
  out.flags = u16(2);
  const uint16_t questions = u16(4), answers = u16(6), authority = u16(8), additional = u16(10);
  size_t at = 12;
  for (int i = 0; i < questions; ++i) {
    std::string name;
    if (!readName(bytes, at, name)) return false;
    out.questions.push_back(name);
    at += 4;
  }
  for (int i = 0; i < answers + authority + additional; ++i) {
    Record record;
    if (!readName(bytes, at, record.name) || at + 10 > bytes.size()) return false;
    record.type = u16(at);
    record.klass = u16(at + 2);
    record.ttl = (uint32_t(u16(at + 4)) << 16) | u16(at + 6);
    const uint16_t length = u16(at + 8);
    at += 10;
    if (at + length > bytes.size()) return false;
    record.dataAt = at;
    record.data.assign(bytes.begin() + static_cast<long>(at), bytes.begin() + static_cast<long>(at + length));
    at += length;
    (i < answers ? out.answers : out.additional).push_back(record);
  }
  return at == bytes.size();
}

std::vector<uint8_t> query(const std::vector<std::pair<std::string, uint16_t>>& questions, uint16_t id = 0,
                           bool unicast = false) {
  std::vector<uint8_t> out = {static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id), 0, 0, 0,
                              static_cast<uint8_t>(questions.size()), 0, 0, 0, 0, 0, 0};
  for (const auto& question : questions) {
    size_t begin = 0;
    const std::string& name = question.first;
    while (begin < name.size()) {
      size_t dot = name.find('.', begin);
      if (dot == std::string::npos) dot = name.size();
      out.push_back(static_cast<uint8_t>(dot - begin));
      out.insert(out.end(), name.begin() + static_cast<long>(begin), name.begin() + static_cast<long>(dot));
      begin = dot + 1;
    }
    out.push_back(0);
    out.push_back(static_cast<uint8_t>(question.second >> 8));
    out.push_back(static_cast<uint8_t>(question.second));
    out.push_back(unicast ? 0x80 : 0);
    out.push_back(1);
  }
  return out;
}

std::vector<uint8_t> respond(const mdns::Identity& identity, const std::vector<uint8_t>& bytes, bool legacy = false) {
  mdns::Message message;
  if (!mdns::parseMessage(bytes.data(), bytes.size(), message)) return {};
  return mdns::answer(identity, message, legacy);
}

const Record* find(const std::vector<Record>& records, uint16_t type, const std::string& name) {
  for (const Record& record : records)
    if (record.type == type && record.name == name) return &record;
  return nullptr;
}

void testClientOutput() {
  check(routineClientMessage("udhcpc: lease of 10.0.0.5 obtained from 10.0.0.1, lease time 3600") &&
            !routineClientMessage("lease of 10.0.0.5") && !routineClientMessage("udhcpc: lease lost, entering init state"),
        "routine udhcpc progress recognised");
  std::vector<std::string> lines;
  ClientOutput output([&](const std::string& line) { lines.push_back(line); }, 1000, 3);
  const std::string chatter =
      "udhcpc: started, v1.37.0\nudhcpc: broadcasting discover\n"
      "udhcpc: broadcasting select for 10.0.0.5, server 10.0.0.1\n"
      "udhcpc: lease of 10.0.0.5 obtained from 10.0.0.1, lease time 3600\n"
      "udhcpc: sending renew to server 10.0.0.1\nudhcpc: broadcasting renew\nudhcpc: received SIGTERM\n";
  output.feed(chatter.data(), chatter.size(), 0);
  check(lines.empty(), "routine udhcpc output dropped");
  const std::string errors =
      "udhcpc: read error: Network is down, reopening socket\r\nudhcpc: received DHCP NAK\n"
      "dhcp callback: event pipe write failed\n";
  output.feed(errors.data(), 20, 10);
  output.feed(errors.data() + 20, errors.size() - 20, 10);
  check(lines == std::vector<std::string>{"read error: Network is down, reopening socket", "received DHCP NAK",
                                          "dhcp callback: event pipe write failed"},
        "errors pass without the udhcpc prefix, split reads joined");
  const std::string burst = "udhcpc: x\nudhcpc: y\n";
  output.feed(burst.data(), burst.size(), 500);
  check(lines.size() == 3, "at most budget lines per window");
  const std::string later = "udhcpc: z\n";
  output.feed(later.data(), later.size(), 1100);
  check(lines.size() == 5 && lines[3] == "2 more lines suppressed" && lines[4] == "z",
        "held-back lines are counted when the window ends");
  const std::string partial = "udhcpc: last words";
  output.feed(partial.data(), partial.size(), 1200);
  output.finish(1300);
  check(lines.back() == "last words", "unterminated line flushed when the client exits");
  const std::string longLine = std::string(2000, 'e') + "\n";
  output.feed(longLine.data(), longLine.size(), 1400);
  check(lines.back().size() == ClientOutput::kMaxLine, "long lines are cut");
}

void testMdns() {
  mdns::Identity identity;
  identity.hostname = "awtrixng-000007";
  identity.address = ipv4("192.0.2.57");
  identity.services = {{"_http._tcp", 80, {}},
                       {"_awtrixng._tcp", 80, {"id=020000000007", "name=awtrixng-000007", "type=awtrixng"}}};

  Response response;
  check(decode(respond(identity, query({{"AWTRIXNG-000007.local", mdns::kTypeA}})), response), "A answer decodes");
  check(response.id == 0 && response.flags == 0x8400 && response.questions.empty() && response.answers.size() == 1 &&
            response.additional.empty(),
        "A answer header");
  const Record a = response.answers.empty() ? Record() : response.answers[0];
  check(a.type == mdns::kTypeA && a.klass == 0x8001 && a.ttl == 120 && a.data == std::vector<uint8_t>{192, 0, 2, 57},
        "A record with cache flush");

  const std::vector<uint8_t> ptrBytes = respond(identity, query({{"_awtrixng._tcp.local", mdns::kTypePtr}}));
  response = Response();
  check(decode(ptrBytes, response), "PTR answer decodes");
  check(response.answers.size() == 1 && response.answers[0].type == mdns::kTypePtr &&
            response.answers[0].klass == 1 && response.answers[0].ttl == 4500,
        "shared PTR record");
  size_t at = response.answers[0].dataAt;
  std::string target;
  check(readName(ptrBytes, at, target) && target == "awtrixng-000007._awtrixng._tcp.local", "PTR names the instance");
  const Record* srv = find(response.additional, mdns::kTypeSrv, "awtrixng-000007._awtrixng._tcp.local");
  const Record* txt = find(response.additional, mdns::kTypeTxt, "awtrixng-000007._awtrixng._tcp.local");
  check(srv && txt && find(response.additional, mdns::kTypeA, "awtrixng-000007.local") && response.additional.size() == 3,
        "PTR carries SRV, TXT and A");
  if (srv) {
    at = srv->dataAt + 6;
    check(srv->data[4] == 0 && srv->data[5] == 80 && readName(ptrBytes, at, target) && target == "awtrixng-000007.local",
          "SRV port and target");
  }
  if (txt) {
    const std::string text(txt->data.begin(), txt->data.end());
    check(text == std::string("\x0f") + "id=020000000007" + "\x14" + "name=awtrixng-000007" + "\x0d" + "type=awtrixng",
          "TXT entries");
  }

  response = Response();
  check(decode(respond(identity, query({{"_services._dns-sd._udp.local", mdns::kTypePtr}})), response) &&
            response.answers.size() == 2 && response.additional.empty(),
        "service enumeration");
  response = Response();
  check(decode(respond(identity, query({{"awtrixng-000007._http._tcp.local", mdns::kTypeAny}})), response) &&
            response.answers.size() == 2 && find(response.additional, mdns::kTypeA, "awtrixng-000007.local"),
        "ANY for an instance returns SRV and TXT plus the address");
  const Record* emptyTxt = find(response.answers, mdns::kTypeTxt, "awtrixng-000007._http._tcp.local");
  check(emptyTxt && emptyTxt->data == std::vector<uint8_t>{0}, "empty TXT is one zero byte");

  response = Response();
  check(decode(respond(identity, query({{"awtrixng-000007.local", mdns::kTypeA}}, 0x1234), true), response) &&
            response.id == 0x1234 && response.questions.size() == 1 && response.answers.size() == 1 &&
            response.answers[0].ttl == 10 && response.answers[0].klass == 1,
        "legacy unicast answer");

  check(respond(identity, query({{"other.local", mdns::kTypeA}})).empty(), "no answer for other names");
  check(respond(identity, query({{"awtrixng-000007.local", 28}})).empty(), "no AAAA");
  std::vector<uint8_t> answerAsQuery = query({{"awtrixng-000007.local", mdns::kTypeA}});
  answerAsQuery[2] = 0x84;
  check(respond(identity, answerAsQuery).empty(), "responses are not answered");

  std::vector<uint8_t> compressed = query({{"local", mdns::kTypePtr}, {"x", mdns::kTypeA}});
  compressed.resize(12 + 7 + 4);
  compressed[5] = 2;
  const uint8_t second[] = {15, 'a', 'w', 't', 'r', 'i', 'x', 'n', 'g', '-', '0', '0', '0', '0', '0', '7', 0xc0, 12, 0, 1, 0, 1};
  compressed.insert(compressed.end(), std::begin(second), std::end(second));
  response = Response();
  check(decode(respond(identity, compressed), response) && response.answers.size() == 1, "compressed question name");
  std::vector<uint8_t> loop = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xc0, 12, 0, 1, 0, 1};
  mdns::Message message;
  check(!mdns::parseMessage(loop.data(), loop.size(), message), "pointer loop rejected");
  std::vector<uint8_t> truncated = query({{"awtrixng-000007.local", mdns::kTypeA}});
  truncated.resize(truncated.size() - 3);
  check(!mdns::parseMessage(truncated.data(), truncated.size(), message), "truncated question rejected");

  response = Response();
  check(decode(mdns::announcement(identity, false), response) && response.answers.size() == 9, "announcement");
  response = Response();
  bool zero = decode(mdns::announcement(identity, true), response) && response.answers.size() == 9;
  for (const Record& record : response.answers) zero = zero && record.ttl == 0;
  check(zero, "goodbye has TTL zero");
  identity.address = 0;
  check(mdns::announcement(identity, false).empty(), "nothing to announce without an address");
}

void testNetlinkCodec() {
  KernelAddress address;
  address.ifindex = 3;
  address.address = ipv4("192.0.2.57");
  address.prefix = 24;
  address.broadcast = ipv4("192.0.2.255");
  netlink::Request request;
  netlink::addressRequest(request, RTM_NEWADDR, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE, 7, address);
  nlmsghdr header{};
  std::memcpy(&header, request.bytes, sizeof(header));
  ifaddrmsg body{};
  std::memcpy(&body, request.bytes + NLMSG_HDRLEN, sizeof(body));
  check(header.nlmsg_len == request.size && header.nlmsg_type == RTM_NEWADDR && header.nlmsg_seq == 7 &&
            (header.nlmsg_flags & NLM_F_REPLACE) && body.ifa_family == AF_INET && body.ifa_prefixlen == 24 &&
            body.ifa_index == 3,
        "address request header");
  KernelAddress decoded;
  check(netlink::decodeAddress(request.bytes, request.size, decoded) && decoded.address == address.address &&
            decoded.broadcast == address.broadcast && decoded.prefix == 24 && decoded.ifindex == 3,
        "address attributes round trip");
  netlink::addressRequest(request, RTM_DELADDR, NLM_F_REQUEST | NLM_F_ACK, 8, address);
  check(netlink::decodeAddress(request.bytes, request.size, decoded) && decoded.broadcast == 0,
        "delete carries no broadcast");

  netlink::defaultRouteRequest(request, RTM_NEWROUTE, NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE, 9, 3,
                               ipv4("192.0.2.1"), true);
  rtmsg route{};
  std::memcpy(&route, request.bytes + NLMSG_HDRLEN, sizeof(route));
  KernelRoute parsed;
  check(route.rtm_table == RT_TABLE_MAIN && route.rtm_protocol == RTPROT_DHCP && route.rtm_dst_len == 0 &&
            route.rtm_type == RTN_UNICAST && (route.rtm_flags & RTNH_F_ONLINK),
        "default route request");
  check(netlink::decodeRoute(request.bytes, request.size, parsed) && parsed.gateway == ipv4("192.0.2.1") &&
            parsed.ifindex == 3 && parsed.prefix == 0 && parsed.table == RT_TABLE_MAIN,
        "default route attributes");

  KernelRoute stale;
  stale.ifindex = 3;
  stale.destination = ipv4("10.1.0.0");
  stale.prefix = 16;
  stale.gateway = ipv4("192.0.2.2");
  stale.table = RT_TABLE_MAIN;
  stale.metric = 50;
  stale.hasMetric = true;
  netlink::routeRequest(request, RTM_DELROUTE, NLM_F_REQUEST | NLM_F_ACK, 10, stale);
  check(netlink::decodeRoute(request.bytes, request.size, parsed) && parsed.destination == stale.destination &&
            parsed.prefix == 16 && parsed.gateway == stale.gateway && parsed.hasMetric && parsed.metric == 50,
        "route delete request round trip");
  std::memcpy(&route, request.bytes + NLMSG_HDRLEN, sizeof(route));
  check(route.rtm_scope == RT_SCOPE_NOWHERE && route.rtm_protocol == 0, "delete matches any scope and protocol");

  netlink::dumpRequest(request, RTM_GETROUTE, 11);
  std::memcpy(&header, request.bytes, sizeof(header));
  check(header.nlmsg_flags == (NLM_F_REQUEST | NLM_F_DUMP) && header.nlmsg_len == NLMSG_LENGTH(sizeof(rtmsg)),
        "dump request");
  const unsigned char brokenAttribute[] = {28, 0, 0, 0, RTM_NEWADDR, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                           AF_INET, 24, 0, 0, 3, 0, 0, 0, 40, 0, 1, 0};
  check(!netlink::decodeAddress(brokenAttribute, sizeof(brokenAttribute), decoded), "attribute overrun rejected");
}

DhcpLease lease(const char* address, const char* router, uint8_t prefix = 24) {
  DhcpLease out;
  out.address = ipv4(address);
  out.prefix = prefix;
  out.broadcast = out.address | ~prefixMask(prefix);
  out.router = router ? ipv4(router) : 0;
  out.leaseSeconds = 3600;
  return out;
}

void testLeaseApplier() {
  ip_test::FakeKernel kernel;
  KernelAddress stock;
  stock.ifindex = 3;
  stock.address = ipv4("192.0.2.110");
  stock.prefix = 24;
  kernel.addAddress(stock);
  KernelRoute stockDefault;
  stockDefault.ifindex = 3;
  stockDefault.gateway = ipv4("192.0.2.1");
  stockDefault.table = RT_TABLE_MAIN;
  stockDefault.protocol = RTPROT_BOOT;
  kernel.routeTable.push_back(stockDefault);
  KernelRoute foreign;
  foreign.ifindex = 1;
  foreign.destination = ipv4("10.9.0.0");
  foreign.prefix = 16;
  foreign.table = RT_TABLE_MAIN;
  foreign.protocol = RTPROT_STATIC;
  kernel.routeTable.push_back(foreign);

  LeaseApplier applier(kernel, "wlan0");
  std::string error;
  check(applier.flush(error), "flush stale stock state: " + error);
  check(kernel.addressesOn(3) == 0 && kernel.defaultRoutes() == 0 && kernel.routeTable.size() == 1 &&
            kernel.routeTable[0].ifindex == 1,
        "only the foreign interface's route survives the flush");

  check(applier.apply(lease("192.0.2.57", "192.0.2.1"), error) && applier.applied(), "apply: " + error);
  check(kernel.addressesOn(3) == 1 && kernel.defaultRoutesVia(ipv4("192.0.2.1")) == 1, "address and default route");
  check(applier.apply(lease("192.0.2.57", "192.0.2.1"), error) && kernel.addressesOn(3) == 1 &&
            kernel.defaultRoutes() == 1,
        "renewal is idempotent");
  check(applier.apply(lease("192.0.2.58", "192.0.2.254"), error), "moved lease: " + error);
  check(kernel.addressesOn(3) == 1 && kernel.addressTable.back().address == ipv4("192.0.2.58") &&
            kernel.defaultRoutes() == 1 && kernel.defaultRoutesVia(ipv4("192.0.2.254")) == 1,
        "old address and route replaced");
  check(applier.apply(lease("192.0.2.58", nullptr), error) && kernel.defaultRoutes() == 0,
        "lease without router drops our default route");
  check(applier.apply(lease("10.20.30.40", "10.0.0.1", 32), error), "/32 lease: " + error);
  check(kernel.operations.back() == "replace-default 10.0.0.1 onlink", "gateway outside the subnet is on-link");
  check(applier.remove(error) && !applier.applied(), "remove: " + error);
  check(kernel.addressesOn(3) == 0 && kernel.defaultRoutes() == 0 && kernel.routeTable.size() == 1,
        "remove leaves only foreign state");
  check(applier.remove(error), "second remove is a no-op");

  kernel.operations.clear();
  kernel.addressTable.clear();
  check(applier.apply(lease("192.0.2.57", "192.0.2.1"), error), "apply before external removal");
  kernel.addressTable.clear();
  kernel.routeTable.resize(1);
  check(applier.remove(error), "remove tolerates state that is already gone: " + error);

  kernel.interfaces.erase("wlan0");
  check(!applier.apply(lease("192.0.2.57", "192.0.2.1"), error) && !applier.applied() &&
            error.find("interface lookup") == 0,
        "missing interface");
}

void testLeaseRenewalFailure() {
  for (int failure : {EIO, ETIMEDOUT}) {
    ip_test::FakeKernel kernel;
    LeaseApplier applier(kernel, "wlan0");
    std::string error;
    const DhcpLease installed = lease("192.0.2.57", "192.0.2.1");
    check(applier.apply(installed, error), "initial lease installed");
    kernel.addAddressError = failure;
    check(!applier.apply(lease("192.0.2.57", "192.0.2.254"), error), "replacement address operation fails");
    check(applier.applied() && applier.lease().address == installed.address &&
              applier.lease().prefix == installed.prefix && applier.lease().router == installed.router &&
              applier.ifindex() == 3 && kernel.defaultRoutesVia(installed.router) == 1,
          "failed renewal keeps the last confirmed lease and route ownership");
    check(applier.remove(error) && kernel.addressesOn(3) == 0 && kernel.defaultRoutes() == 0,
          "failed renewal still permits complete cleanup");
    check(!applier.apply(installed, error) && !applier.applied(), "failed initial install claims no address");
  }
}

}

int main() {
  testCaptiveDns();
  testAccessPointFiles();
  testIpv4();
  testRecordFormat();
  testLeaseParse();
  testStaticLease();
  testResolv();
  testHostname();
  testSntpPackets();
  testSntpClient();
  testClientOutput();
  testMdns();
  testNetlinkCodec();
  testLeaseApplier();
  testLeaseRenewalFailure();
  return ip_test::finish("tc002d-ip-units");
}
