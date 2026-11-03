#include "transport/net/HostResolver.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <atomic>
#include <mutex>

#include "system/Log.h"

namespace awtrix {
namespace net {

namespace {

constexpr uint32_t kStackBytes = 4096;
constexpr UBaseType_t kPriority = 1;
constexpr BaseType_t kCore = 0;

constexpr uint32_t kMdnsTimeoutMs = 3000;

// Backstop in case the worker never reports back; without it an inFlight_ query would wedge the
// caller's state machine forever.
constexpr uint32_t kResolveDeadlineMs = 20000;

// mDNS and DNS lookups block for seconds, which the main loop cannot afford, so they run on a
// worker task and callers poll resolve() until it stops returning Pending. The task exits once it
// has answered, so a resolver costs nothing between lookups. It must outlive that task and any
// late DNS reply (see dnsAnswer_); both instances in the firmware live as long as the firmware.
class HostResolverEsp32 : public IHostResolver {
 public:
  ResolveState resolve(const std::string& host) override {
    uint32_t literal;
    if (parseIpv4(host, literal)) {
      address_ = literal;
      error_ = LinkError::None;
      have_ = true;
      return ResolveState::Ready;
    }

    if (have_ && host == cachedHost_) return ResolveState::Ready;

    if (!WiFi.isConnected()) {
      error_ = LinkError::NoWifi;
      return ResolveState::Failed;
    }

    // The host changed under a running query; abandon it rather than waiting for an answer nobody
    // asked for.
    if (inFlight_ && host != pendingHost_) forget();

    if (!inFlight_) {
      if (start(host)) return ResolveState::Pending;
      error_ = LinkError::Timeout;
      return ResolveState::Failed;
    }

    const uint8_t done = done_.load(std::memory_order_acquire);
    if (done == kPending) {
      if (millis() - startedMs_ >= kResolveDeadlineMs) {
        forget();
        error_ = LinkError::Timeout;
        return ResolveState::Failed;
      }
      return ResolveState::Pending;
    }

    inFlight_ = false;
    if (done == kFailed) {
      error_ = LinkError::HostNotFound;
      logf("dns: %s not found (tried %s)", pendingHost_.c_str(),
           isMdnsName(pendingHost_) ? "mDNS, DNS, bare label" : "DNS");
      return ResolveState::Failed;
    }

    address_ = ntohl(result_.load(std::memory_order_relaxed));
    cachedHost_ = pendingHost_;
    error_ = LinkError::None;
    have_ = true;
    logf("dns: %s is %s via %s", cachedHost_.c_str(), IPAddress(htonl(address_)).toString().c_str(),
         viaName(via_.load(std::memory_order_relaxed)));
    return ResolveState::Ready;
  }

  uint32_t address() const override { return address_; }
  LinkError error() const override { return error_; }

  // Bumping the generation is how an in-flight query is cancelled: the worker cannot be
  // interrupted, so it finishes and then throws its own result away.
  void forget() override {
    have_ = false;
    cachedHost_.clear();
    if (inFlight_) {
      std::lock_guard<std::mutex> lock(requestMutex_);
      ++generation_;
      inFlight_ = false;
    }
  }

 private:
  static constexpr uint8_t kPending = 0;
  static constexpr uint8_t kReady = 1;
  static constexpr uint8_t kFailed = 2;

  static constexpr uint8_t kViaNone = 0;
  static constexpr uint8_t kViaMdns = 1;
  static constexpr uint8_t kViaDns = 2;
  static constexpr uint8_t kViaLabel = 3;

  static const char* viaName(uint8_t via) {
    switch (via) {
      case kViaMdns: return "mDNS";
      case kViaDns: return "DNS";
      case kViaLabel: return "DNS on the bare label";
      default: return "?";
    }
  }

  // False means there is no memory for the worker. Nothing stays behind, so the next call retries.
  bool start(const std::string& host) {
    pendingHost_ = host;
    startedMs_ = millis();
    inFlight_ = true;
    bool needTask = false;
    {
      std::lock_guard<std::mutex> lock(requestMutex_);
      ++generation_;
      queryHost_ = host;
      queryGeneration_ = generation_;
      done_.store(kPending, std::memory_order_release);
      queued_ = true;
      needTask = !taskAlive_;
      taskAlive_ = true;
    }
    if (!needTask) return true;
    // Pinned to core 0 so its multi-second blocking calls stay off the render core.
    if (xTaskCreatePinnedToCore(taskEntry, "mqttdns", kStackBytes, this, kPriority, nullptr, kCore) == pdPASS)
      return true;
    {
      std::lock_guard<std::mutex> lock(requestMutex_);
      queued_ = false;
      taskAlive_ = false;
    }
    inFlight_ = false;
    logf("dns: no memory to look up %s", host.c_str());
    return false;
  }

  static void taskEntry(void* self) {
    static_cast<HostResolverEsp32*>(self)->run();
    vTaskDelete(nullptr);
  }

  void run() {
    for (;;) {
      std::string host;
      uint32_t mine = 0;
      {
        // Looking at the queue and declaring the task gone happen under one lock with start(), so a
        // request either is seen here or makes start() create the next task.
        std::lock_guard<std::mutex> lock(requestMutex_);
        if (!queued_) {
          taskAlive_ = false;
          return;
        }
        queued_ = false;
        host = queryHost_;
        mine = queryGeneration_;
      }

      IPAddress found;
      uint8_t via = kViaNone;
      // For a .local name try mDNS first, then plain DNS, then DNS on the bare label - some routers
      // hand out LAN names without the suffix, and users type the host either way.
      if (isMdnsName(host)) {
        found = MDNS.queryHost(mdnsLabel(host).c_str(), kMdnsTimeoutMs);
        if (static_cast<uint32_t>(found) != 0) via = kViaMdns;
        if (via == kViaNone && WiFi.hostByName(host.c_str(), dnsAnswer_) == 1) {
          found = dnsAnswer_;
          via = kViaDns;
        }
        if (via == kViaNone && WiFi.hostByName(mdnsLabel(host).c_str(), dnsAnswer_) == 1) {
          found = dnsAnswer_;
          via = kViaLabel;
        }
      } else if (WiFi.hostByName(host.c_str(), dnsAnswer_) == 1) {
        found = dnsAnswer_;
        via = kViaDns;
      }

      // Cancelled or superseded while we were blocked; publishing now would answer a question the
      // caller has already moved on from.
      std::lock_guard<std::mutex> lock(requestMutex_);
      if (mine != generation_) continue;
      via_.store(via, std::memory_order_relaxed);
      if (static_cast<uint32_t>(found) != 0) {
        result_.store(static_cast<uint32_t>(found), std::memory_order_relaxed);
        done_.store(kReady, std::memory_order_release);
      } else {
        done_.store(kFailed, std::memory_order_release);
      }
    }
  }

  std::mutex requestMutex_;
  std::string queryHost_;
  uint32_t queryGeneration_ = 0;
  uint32_t generation_ = 0;
  bool queued_ = false;
  bool taskAlive_ = false;

  // lwIP keeps this address while it waits, and hostByName() gives up after 15 s without telling
  // it, so a later reply still lands here. On the task's stack that would be freed memory.
  IPAddress dnsAnswer_;

  std::atomic<uint8_t> done_{kPending};
  std::atomic<uint32_t> result_{0};
  std::atomic<uint8_t> via_{kViaNone};

  std::string cachedHost_;
  std::string pendingHost_;
  uint32_t address_ = 0;
  LinkError error_ = LinkError::None;
  uint32_t startedMs_ = 0;
  bool have_ = false;
  bool inFlight_ = false;
};

}

std::unique_ptr<IHostResolver> makeHostResolver() {
  return std::unique_ptr<IHostResolver>(new HostResolverEsp32());
}

}
}
