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
constexpr TickType_t kIdlePollTicks = pdMS_TO_TICKS(50);

constexpr uint32_t kMdnsTimeoutMs = 3000;

// Backstop in case the worker never reports back; without it an inFlight_ query would wedge the
// caller's state machine forever.
constexpr uint32_t kResolveDeadlineMs = 20000;

// mDNS and DNS lookups block for seconds, which the main loop cannot afford, so they run on a
// worker task and callers poll resolve() until it stops returning Pending.
class HostResolverEsp32 : public IHostResolver {
 public:
  ResolveState resolve(const std::string& host) override {
    uint8_t octets[4];
    if (parseIpv4(host, octets)) {
      address_ = IPAddress(octets[0], octets[1], octets[2], octets[3]);
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
      start(host);
      return ResolveState::Pending;
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

    address_ = IPAddress(result_.load(std::memory_order_relaxed));
    cachedHost_ = pendingHost_;
    error_ = LinkError::None;
    have_ = true;
    logf("dns: %s is %s via %s", cachedHost_.c_str(), address_.toString().c_str(),
         viaName(via_.load(std::memory_order_relaxed)));
    return ResolveState::Ready;
  }

  IPAddress address() const override { return address_; }
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

  void start(const std::string& host) {
    {
      std::lock_guard<std::mutex> lock(requestMutex_);
      ++generation_;
      queryHost_ = host;
      queryGeneration_ = generation_;
    }
    pendingHost_ = host;
    done_.store(kPending, std::memory_order_release);
    startedMs_ = millis();
    inFlight_ = true;
    queued_.store(true, std::memory_order_release);
    // The task is created on the first lookup and then lives forever, parked on core 0 so its
    // multi-second blocking calls stay off the render core.
    if (!started_) {
      started_ = true;
      xTaskCreatePinnedToCore(taskEntry, "mqttdns", kStackBytes, this, kPriority, nullptr, kCore);
    }
  }

  static void taskEntry(void* self) { static_cast<HostResolverEsp32*>(self)->run(); }

  void run() {
    for (;;) {
      if (!queued_.exchange(false, std::memory_order_acquire)) {
        vTaskDelay(kIdlePollTicks);
        continue;
      }

      std::string host;
      uint32_t mine = 0;
      {
        std::lock_guard<std::mutex> lock(requestMutex_);
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
        IPAddress viaDns;
        if (via == kViaNone && WiFi.hostByName(host.c_str(), viaDns) == 1) {
          found = viaDns;
          via = kViaDns;
        }
        if (via == kViaNone && WiFi.hostByName(mdnsLabel(host).c_str(), viaDns) == 1) {
          found = viaDns;
          via = kViaLabel;
        }
      } else {
        IPAddress viaDns;
        if (WiFi.hostByName(host.c_str(), viaDns) == 1) {
          found = viaDns;
          via = kViaDns;
        }
      }
      via_.store(via, std::memory_order_relaxed);

      // Cancelled or superseded while we were blocked; publishing now would answer a question the
      // caller has already moved on from.
      {
        std::lock_guard<std::mutex> lock(requestMutex_);
        if (mine != generation_) continue;
      }

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

  std::atomic<bool> queued_{false};
  std::atomic<uint8_t> done_{kPending};
  std::atomic<uint32_t> result_{0};
  std::atomic<uint8_t> via_{kViaNone};

  std::string cachedHost_;
  std::string pendingHost_;
  IPAddress address_;
  LinkError error_ = LinkError::None;
  uint32_t startedMs_ = 0;
  bool have_ = false;
  bool inFlight_ = false;
  bool started_ = false;
};

}

std::unique_ptr<IHostResolver> makeHostResolver() {
  return std::unique_ptr<IHostResolver>(new HostResolverEsp32());
}

}
}
