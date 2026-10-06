#include "platform/linux/host/HostResolver.h"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <system_error>
#include <thread>

namespace awtrix {
namespace net {

namespace {

// getaddrinfo blocks, so lookups run on a detached thread and resolve() is polled until it reports
// Ready or Failed, matching the device's asynchronous DNS rather than stalling the render loop.
class HostResolver : public IHostResolver {
 public:
  explicit HostResolver(long delayMs) : delayMs_(delayMs) {}

  ResolveState resolve(const std::string& host) override {
    if (host.empty() || host.find('\0') != std::string::npos) {
      forget();
      error_ = LinkError::HostNotFound;
      return ResolveState::Failed;
    }
    uint32_t literal;
    if (parseIpv4(host, literal)) {
      forget();
      address_ = literal;
      cachedHost_ = host;
      error_ = LinkError::None;
      have_ = true;
      return ResolveState::Ready;
    }

    if (have_ && host == cachedHost_) return ResolveState::Ready;

    if (pending_ && host != pendingHost_) forget();

    if (!pending_) {
      if (!start(host)) return ResolveState::Failed;
      return ResolveState::Pending;
    }

    const uint8_t done = pending_->done.load(std::memory_order_acquire);
    if (done == kPending) return ResolveState::Pending;

    if (done == kFailed) {
      pending_.reset();
      error_ = LinkError::HostNotFound;
      return ResolveState::Failed;
    }

    address_ = pending_->result;
    pending_.reset();
    cachedHost_ = pendingHost_;
    error_ = LinkError::None;
    have_ = true;
    return ResolveState::Ready;
  }

  uint32_t address() const override { return address_; }
  LinkError error() const override { return error_; }

  void forget() override {
    have_ = false;
    cachedHost_.clear();
    pending_.reset();
    pendingHost_.clear();
  }

 private:
  static constexpr uint8_t kPending = 0;
  static constexpr uint8_t kReady = 1;
  static constexpr uint8_t kFailed = 2;

  struct Lookup {
    std::atomic<uint8_t> done{kPending};
    uint32_t result = 0;
  };

  bool start(const std::string& host) {
    pendingHost_ = host;
    have_ = false;
    auto lookup = std::make_shared<Lookup>();
    pending_ = lookup;
    const long delay = delayMs_;
    // The worker owns only its lookup result. Destruction/forget can drop our
    // reference without waiting on DNS or allowing a write into a dead resolver.
    try {
    std::thread([lookup, host, delay] {
      if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));

      uint32_t found = 0;
      addrinfo hints{};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      addrinfo* res = nullptr;
      if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
        for (addrinfo* ai = res; ai; ai = ai->ai_next) {
          if (ai->ai_family != AF_INET) continue;
          found = ntohl(reinterpret_cast<sockaddr_in*>(ai->ai_addr)->sin_addr.s_addr);
          break;
        }
        ::freeaddrinfo(res);
      }

      if (found != 0) {
        lookup->result = found;
        lookup->done.store(kReady, std::memory_order_release);
      } else {
        lookup->done.store(kFailed, std::memory_order_release);
      }
    }).detach();
    } catch (const std::system_error&) {
      pending_.reset();
      error_ = LinkError::HostNotFound;
      return false;
    }
    return true;
  }

  const long delayMs_;
  std::shared_ptr<Lookup> pending_;

  std::string cachedHost_;
  std::string pendingHost_;
  uint32_t address_ = 0;
  LinkError error_ = LinkError::None;
  bool have_ = false;
};

}

std::unique_ptr<IHostResolver> makeHostResolver() {
  return std::unique_ptr<IHostResolver>(new HostResolver(0));
}

std::unique_ptr<IHostResolver> makeSlowHostResolver(long delayMs) {
  return std::unique_ptr<IHostResolver>(new HostResolver(delayMs > 0 ? delayMs : 0));
}

}
}
