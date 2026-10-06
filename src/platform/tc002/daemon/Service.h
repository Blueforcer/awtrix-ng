#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "platform/tc002/contract/SupervisorProtocol.h"

// Building blocks of awtrix-tc002d. The daemon runs one poll loop; every subsystem is a
// Service that reports the descriptors and deadline it waits for and is called back from that
// loop. Services never block, never own threads and never install signal handlers.
namespace awtrix {
namespace tc002d {

struct PollInterest {
  int fd = -1;
  short events = 0;
};

class Service {
 public:
  virtual ~Service() = default;
  virtual const char* name() const = 0;
  virtual bool start(int64_t nowMs) = 0;
  virtual void pollInterest(std::vector<PollInterest>& out) const { (void)out; }
  virtual void onReady(int fd, short revents, int64_t nowMs) { (void)fd; (void)revents; (void)nowMs; }
  virtual int64_t nextDeadlineMs() const { return -1; }
  virtual void onTime(int64_t nowMs) { (void)nowMs; }
  virtual bool onChildExit(pid_t pid, int status, int64_t nowMs) {
    (void)pid; (void)status; (void)nowMs;
    return false;
  }
  virtual void requestStop(int64_t nowMs) { (void)nowMs; }
  virtual bool stopped() const { return true; }
};

// Shared, observable device state. Producers assign a new value through the setters; every
// change is fanned out to subscribers (runtime bridge, status socket, log) synchronously.
class DeviceState {
 public:
  using Listener = std::function<void()>;

  const tc002::PowerStatus& power() const { return power_; }
  const tc002::NetworkStatus& network() const { return network_; }
  const tc002::TimeStatus& time() const { return time_; }

  void setPower(const tc002::PowerStatus& value) { power_ = value; notify(powerListeners_); }
  void setNetwork(const tc002::NetworkStatus& value) { network_ = value; notify(networkListeners_); }
  void setTime(const tc002::TimeStatus& value) { time_ = value; notify(timeListeners_); }

  void onPower(Listener listener) { powerListeners_.push_back(std::move(listener)); }
  void onNetwork(Listener listener) { networkListeners_.push_back(std::move(listener)); }
  void onTime(Listener listener) { timeListeners_.push_back(std::move(listener)); }

 private:
  static void notify(const std::vector<Listener>& listeners) {
    for (const auto& listener : listeners) listener();
  }
  tc002::PowerStatus power_;
  tc002::NetworkStatus network_;
  tc002::TimeStatus time_;
  std::vector<Listener> powerListeners_, networkListeners_, timeListeners_;
};

// Commands that reach the network side from the runtime (web UI / API) or from USB provisioning.
class WifiControl {
 public:
  using ScanDone = std::function<void(const std::vector<tc002::WifiNetwork>& networks)>;
  virtual ~WifiControl() = default;
  virtual bool setCredentials(const tc002::WifiCredentials& credentials) = 0;
  virtual void eraseCredentials() = 0;
  virtual void scan(ScanDone done) = 0;
};

class TimeControl {
 public:
  virtual ~TimeControl() = default;
  virtual void setServer(const std::string& server) = 0;
};

class HostnameControl {
 public:
  virtual ~HostnameControl() = default;
  virtual void setHostname(const std::string& hostname) = 0;
};

class AddressControl {
 public:
  virtual ~AddressControl() = default;
  virtual void setStaticAddress(const tc002::StaticAddress& address) = 0;
};

}
}
