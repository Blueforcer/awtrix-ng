#include "transport/net/HostResolver.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace awtrix {
namespace net {

namespace {

// getaddrinfo blocks, so lookups run on a detached thread and resolve() is polled until it reports
// Ready or Failed, matching the device's asynchronous DNS rather than stalling the render loop.
class HostResolverSim : public IHostResolver {
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

    if (inFlight_ && host != pendingHost_) forget();

    if (!inFlight_) {
      start(host);
      return ResolveState::Pending;
    }

    const uint8_t done = done_.load(std::memory_order_acquire);
    if (done == kPending) return ResolveState::Pending;

    inFlight_ = false;
    if (done == kFailed) {
      error_ = LinkError::HostNotFound;
      return ResolveState::Failed;
    }

    address_ = IPAddress(result_.load(std::memory_order_relaxed));
    cachedHost_ = pendingHost_;
    error_ = LinkError::None;
    have_ = true;
    return ResolveState::Ready;
  }

  IPAddress address() const override { return address_; }
  LinkError error() const override { return error_; }

  void forget() override {
    have_ = false;
    cachedHost_.clear();
    if (inFlight_) {
      generation_.fetch_add(1, std::memory_order_release);
      inFlight_ = false;
    }
  }

 private:
  static constexpr uint8_t kPending = 0;
  static constexpr uint8_t kReady = 1;
  static constexpr uint8_t kFailed = 2;

  // Test hook: fakes a slow resolver so the pending path can actually be exercised.
  static long injectedDelayMs() {
    const char* v = std::getenv("AWTRIX_SIM_DNS_DELAY_MS");
    return v ? std::strtol(v, nullptr, 10) : 0;
  }

  void start(const std::string& host) {
    pendingHost_ = host;
    done_.store(kPending, std::memory_order_release);
    inFlight_ = true;
    // Each lookup claims a generation. A thread that finishes after forget() or a newer lookup sees
    // a bumped counter and drops its result, which is why the thread can safely be detached.
    const uint32_t mine = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;

    std::thread([this, host, mine] {
      const long delay = injectedDelayMs();
      if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));

      uint32_t found = 0;
      addrinfo hints{};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      addrinfo* res = nullptr;
      if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
          if (ai->ai_family != AF_INET) continue;
          found = static_cast<uint32_t>(
              reinterpret_cast<sockaddr_in*>(ai->ai_addr)->sin_addr.s_addr);
          break;
        }
        ::freeaddrinfo(res);
      }

      if (mine != generation_.load(std::memory_order_acquire)) return;
      if (found != 0) {
        result_.store(found, std::memory_order_relaxed);
        done_.store(kReady, std::memory_order_release);
      } else {
        done_.store(kFailed, std::memory_order_release);
      }
    }).detach();
  }

  std::atomic<uint32_t> generation_{0};
  std::atomic<uint8_t> done_{kPending};
  std::atomic<uint32_t> result_{0};

  std::string cachedHost_;
  std::string pendingHost_;
  IPAddress address_;
  LinkError error_ = LinkError::None;
  bool have_ = false;
  bool inFlight_ = false;
};

}

std::unique_ptr<IHostResolver> makeHostResolver() {
  return std::unique_ptr<IHostResolver>(new HostResolverSim());
}

}
}
