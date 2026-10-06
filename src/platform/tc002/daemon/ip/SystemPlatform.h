#pragma once

#include <string>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/ip/IpController.h"

namespace awtrix {
namespace tc002d {
namespace ip {

// The controller's view of the running system: rtnetlink, the real clocks, our resolv.conf bind
// mount under runDir, fork/exec, sethostname, sysfs and the daemon log.
class SystemPlatform final : public IpPlatform {
 public:
  explicit SystemPlatform(const std::string& runDir);

  IpKernel& kernel() override { return kernel_; }
  SystemClock& clock() override { return clock_; }
  ResolverFile& resolver() override { return resolver_; }
  pid_t spawn(const SpawnRequest& request, std::string& error) override;
  void signal(pid_t pid, int number, bool wholeGroup) override;
  bool setKernelHostname(const std::string& hostname) override;
  std::string interfaceMac(const std::string& interfaceName) override;
  bool executable(const std::string& path) override;
  bool prepareDirectory(const std::string& path) override;
  bool readFile(const std::string& path, std::string& out) override { return posix::readText(path, out); }
  bool replaceFile(const std::string& path, const std::string& text) override {
    return posix::replaceText(path, text);
  }
  bool removeFile(const std::string& path) override;
  bool prepareAccessPoint(const std::string& directory, const std::string& config, std::string& error) override;
  void removeAccessPoint(const std::string& directory) override;
  int64_t monotonicMs() override;
  bool multicast() override { return true; }
  void log(const char* component, const std::string& line) override;

 private:
  Rtnetlink kernel_;
  RealClock clock_;
  BoundResolvConf resolver_;
};

}
}
}
