#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/tc002/daemon/Service.h"

namespace awtrix {
namespace tc002d {
namespace ip {

namespace sntp {
constexpr std::size_t kPacketSize = 48;

struct Reply {
  uint8_t leap = 0, version = 0, mode = 0, stratum = 0;
  uint64_t originate = 0, receive = 0, transmit = 0;
};

struct Sample {
  int64_t offsetNs = 0;
  int64_t delayNs = 0;
};

// Client request carrying cookie as its transmit timestamp; the server echoes it as originate.
void encodeRequest(uint64_t cookie, uint8_t (&out)[kPacketSize]);
bool decodeReply(const uint8_t* data, std::size_t size, uint64_t cookie, Reply& out, const char*& error);
// NTP era handling pivots at 1968/2036, which covers 1968 through 2104.
int64_t ntpToUnixNs(uint64_t timestamp);
uint64_t unixNsToNtp(int64_t unixNs);
// Offset of the local realtime clock at receipt. Round trip is measured on the monotonic clock,
// so a clock that starts at 1970 on every boot does not disturb the arithmetic.
bool sample(const Reply& reply, int64_t sentMonotonicNs, int64_t receivedMonotonicNs,
            int64_t receivedRealtimeNs, Sample& out);
}

class SystemClock {
 public:
  virtual ~SystemClock() = default;
  virtual int64_t monotonicNs() = 0;
  virtual int64_t realtimeNs() = 0;
  virtual bool step(int64_t offsetNs) = 0;
  virtual bool slew(int64_t offsetNs) = 0;
};

class RealClock final : public SystemClock {
 public:
  int64_t monotonicNs() override;
  int64_t realtimeNs() override;
  bool step(int64_t offsetNs) override;
  bool slew(int64_t offsetNs) override;
};

struct SntpTiming {
  int64_t pollMs = 3600 * 1000;
  int64_t firstRetryMs = 10 * 1000;
  int64_t maxRetryMs = 300 * 1000;
  int64_t resolveTimeoutMs = 5000;
  int64_t replyTimeoutMs = 3000;
  int64_t stepThresholdNs = 1000 * 1000 * 1000;
  int64_t slewThresholdNs = 5 * 1000 * 1000;
  uint16_t port = 123;
};

// Minimal SNTPv4 client for the daemon loop. Names are resolved in a short-lived forked child
// that writes the addresses to a pipe, so the loop never waits on DNS.
class SntpClient {
 public:
  explicit SntpClient(SystemClock& clock, SntpTiming timing = SntpTiming());
  ~SntpClient();
  SntpClient(const SntpClient&) = delete;
  SntpClient& operator=(const SntpClient&) = delete;

  void setServer(const std::string& server, int64_t nowMs);
  void setFallbackServers(const std::vector<uint32_t>& servers);
  void setOnline(bool online, int64_t nowMs);
  bool synchronized() const { return synchronized_; }
  const std::string& server() const { return server_; }

  void pollInterest(std::vector<PollInterest>& out) const;
  void onReady(int fd, short revents, int64_t nowMs);
  int64_t nextDeadlineMs() const;
  void onTime(int64_t nowMs);
  bool onChildExit(pid_t pid);
  void stop();
  bool childRunning() const { return resolverPid_ > 0; }

  std::function<void(const std::string&)> log;
  std::function<void()> synchronizedChanged;

 private:
  enum class Phase { Waiting, Resolving, Querying };
  void attempt(int64_t nowMs);
  void startResolver(int64_t nowMs);
  void readResolver(int64_t nowMs);
  void query(uint32_t address, int64_t nowMs);
  void readReply(int64_t nowMs);
  void succeed(const sntp::Sample& sample, int64_t nowMs);
  void fail(const std::string& reason, int64_t nowMs);
  void cancel();
  void note(const std::string& message) const;

  SystemClock& clock_;
  SntpTiming timing_;
  std::string server_ = "pool.ntp.org";
  std::vector<uint32_t> fallback_;
  bool online_ = false;
  bool synchronized_ = false;
  Phase phase_ = Phase::Waiting;
  int64_t nextAttemptMs_ = -1;
  int64_t deadlineMs_ = -1;
  int64_t retryMs_ = 0;
  unsigned failures_ = 0;
  std::string target_;
  pid_t resolverPid_ = -1;
  int resolverFd_ = -1;
  unsigned char resolved_[16]{};
  std::size_t resolvedSize_ = 0;
  int socket_ = -1;
  uint64_t cookie_ = 0;
  int64_t sentMonotonicNs_ = 0;
};

}
}
}
