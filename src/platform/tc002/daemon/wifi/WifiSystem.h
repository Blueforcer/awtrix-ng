#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Everything WifiService does to the system goes through this seam, so the state machine can be
// driven against a fake in host tests. Every call returns promptly; slow work (module init,
// PBKDF2, flash writes) runs in a child process whose exit reaches the service via onChildExit.
namespace awtrix {
namespace tc002d {
namespace wifi {

struct ProcessInfo {
  pid_t pid = 0;
  std::string cmdline;
};

class WifiSystem {
 public:
  virtual ~WifiSystem() = default;
  virtual bool interfacePresent() = 0;
  virtual std::string interfaceAttribute(const char* name) = 0;
  virtual bool setInterfaceUp(int& error) = 0;
  // Child exit status is 0 on success (or already loaded), else the errno of the failure.
  // `parameters` are the module's options ("name=value ..."); an already loaded module keeps its own.
  virtual pid_t loadModule(const std::string& path, const std::string& parameters) = 0;
  // Writes `value` to a sysfs or procfs attribute; 0 or the errno of the failure.
  virtual int writeKernelAttribute(const std::string& path, const std::string& value) = 0;
  // Empties the kernel ring buffer; 0 or the errno of the failure.
  virtual int clearKernelLog() = 0;
  virtual pid_t setProperty(const std::string& key, const std::string& value) = 0;
  virtual pid_t startSupplicant(const std::string& program, const std::string& configPath) = 0;
  virtual pid_t runTask(const std::function<int()>& task) = 0;
  virtual std::vector<ProcessInfo> processes(const char* comm) = 0;
  // Sockets in LISTEN state on `port` across /proc/net/tcp{,6}; -1 when unreadable.
  virtual int tcpListeners(unsigned port) = 0;
  virtual void signal(pid_t pid, int number) = 0;
  // CLOCK_MONOTONIC in ms, the loop's clock; used when a command arrives outside a loop callback.
  virtual int64_t nowMs() = 0;
};

std::unique_ptr<WifiSystem> nativeWifiSystem();

int countTcpListeners(std::string_view table, unsigned port);
int writeAttribute(const std::string& path, std::string_view value);

}
}
}
