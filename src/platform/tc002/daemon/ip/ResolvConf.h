#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace awtrix {
namespace tc002d {
namespace ip {

// resolv.conf text for the lease's DNS servers, or the gateway when the lease names none;
// empty (no nameserver) when it names neither.
std::string resolvContent(const std::vector<uint32_t>& dns, uint32_t gateway);

// Number of mounts whose mount point is target in a /proc/self/mountinfo text.
unsigned countMountsAt(std::string_view mountinfo, std::string_view target);

class ResolverFile {
 public:
  virtual ~ResolverFile() = default;
  virtual bool install(std::string& error) = 0;
  virtual bool write(const std::string& content, std::string& error) = 0;
  virtual void uninstall() = 0;
};

// Our own file bind-mounted over the read-only /etc/resolv.conf. It starts without a nameserver
// and never takes over the stock file's servers. Contents are rewritten in place so the bind
// mount (and musl, which rereads the file per lookup) sees every change.
class BoundResolvConf final : public ResolverFile {
 public:
  explicit BoundResolvConf(std::string ownPath, std::string target = "/etc/resolv.conf",
                           std::string mountinfo = "/proc/self/mountinfo");
  ~BoundResolvConf() override;
  BoundResolvConf(const BoundResolvConf&) = delete;
  BoundResolvConf& operator=(const BoundResolvConf&) = delete;

  bool install(std::string& error) override;
  bool write(const std::string& content, std::string& error) override;
  void uninstall() override;

 private:
  bool visible(std::string& error) const;

  std::string ownPath_, target_, mountinfo_;
  std::string content_;
  int fd_ = -1;
  bool bound_ = false;
};

}
}
}
