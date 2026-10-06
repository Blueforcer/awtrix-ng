#include "platform/posix/Files.h"
#include "platform/tc002/daemon/ip/ResolvConf.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/tc002/daemon/ip/Ipv4.h"

namespace awtrix {
namespace tc002d {
namespace ip {
namespace {

constexpr unsigned kMaxStaleMounts = 8;
constexpr std::size_t kMaxFile = 64 * 1024;

std::string unescapeMountPath(std::string_view text) {
  std::string out;
  for (std::size_t at = 0; at < text.size(); ++at) {
    if (text[at] == '\\' && at + 3 < text.size()) {
      const char a = text[at + 1], b = text[at + 2], c = text[at + 3];
      if (a >= '0' && a <= '3' && b >= '0' && b <= '7' && c >= '0' && c <= '7') {
        out += static_cast<char>((a - '0') * 64 + (b - '0') * 8 + (c - '0'));
        at += 3;
        continue;
      }
    }
    out += text[at];
  }
  return out;
}

bool writeAll(int fd, const std::string& content) {
  std::size_t done = 0;
  while (done < content.size()) {
    const ssize_t n = ::pwrite(fd, content.data() + done, content.size() - done, static_cast<off_t>(done));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    done += static_cast<std::size_t>(n);
  }
  return ::ftruncate(fd, static_cast<off_t>(content.size())) == 0;
}

std::string failure(const char* what) { return std::string(what) + ": " + std::strerror(errno); }

}

std::string resolvContent(const std::vector<uint32_t>& dns, uint32_t gateway) {
  std::string out;
  for (std::size_t i = 0; i < dns.size() && i < 3; ++i) out += "nameserver " + formatIpv4(dns[i]) + "\n";
  if (out.empty() && gateway) out = "nameserver " + formatIpv4(gateway) + "\n";
  return out;
}

unsigned countMountsAt(std::string_view mountinfo, std::string_view target) {
  unsigned count = 0;
  while (!mountinfo.empty()) {
    const std::size_t newline = mountinfo.find('\n');
    std::string_view line = mountinfo.substr(0, newline);
    mountinfo = newline == std::string_view::npos ? std::string_view() : mountinfo.substr(newline + 1);
    for (int field = 0; field < 4 && !line.empty(); ++field) {
      const std::size_t space = line.find(' ');
      line = space == std::string_view::npos ? std::string_view() : line.substr(space + 1);
    }
    const std::string_view mountPoint = line.substr(0, line.find(' '));
    if (!mountPoint.empty() && unescapeMountPath(mountPoint) == target) ++count;
  }
  return count;
}

BoundResolvConf::BoundResolvConf(std::string ownPath, std::string target, std::string mountinfo)
    : ownPath_(std::move(ownPath)), target_(std::move(target)), mountinfo_(std::move(mountinfo)) {}

BoundResolvConf::~BoundResolvConf() { uninstall(); }

bool BoundResolvConf::install(std::string& error) {
  if (bound_) return visible(error);
  std::string mounts;
  if (posix::readText(mountinfo_, mounts, kMaxFile)) {
    const unsigned stale = countMountsAt(mounts, target_);
    for (unsigned i = 0; i < stale && i < kMaxStaleMounts; ++i) {
      if (::umount2(target_.c_str(), MNT_DETACH) != 0) {
        error = failure("stale resolv.conf mount");
        return false;
      }
    }
  }
  if (fd_ < 0) {
    fd_ = ::open(ownPath_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd_ < 0) {
      error = failure("resolv.conf file");
      return false;
    }
    ::fchmod(fd_, 0644);
  }
  if (!writeAll(fd_, std::string())) {
    error = failure("resolv.conf file");
    return false;
  }
  content_.clear();
  if (::mount(ownPath_.c_str(), target_.c_str(), nullptr, MS_BIND, nullptr) != 0) {
    error = failure("resolv.conf bind mount");
    return false;
  }
  bound_ = true;
  return visible(error);
}

bool BoundResolvConf::visible(std::string& error) const {
  struct stat own {}, shown {};
  if (::fstat(fd_, &own) == 0 && ::stat(target_.c_str(), &shown) == 0 && own.st_ino == shown.st_ino &&
      own.st_dev == shown.st_dev)
    return true;
  error = "resolv.conf bind mount not visible";
  return false;
}

bool BoundResolvConf::write(const std::string& content, std::string& error) {
  if (fd_ < 0) {
    error = "resolv.conf not installed";
    return false;
  }
  if (content == content_) return true;
  if (!writeAll(fd_, content)) {
    error = failure("resolv.conf update");
    return false;
  }
  content_ = content;
  return true;
}

void BoundResolvConf::uninstall() {
  if (bound_) ::umount2(target_.c_str(), MNT_DETACH);
  bound_ = false;
  if (fd_ >= 0) {
    ::close(fd_);
    ::unlink(ownPath_.c_str());
  }
  fd_ = -1;
  content_.clear();
}

}
}
}
