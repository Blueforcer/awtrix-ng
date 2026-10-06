#include "platform/tc002/daemon/wifi/WifiSystem.h"

#include <dirent.h>
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/klog.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Kernel.h"
#include "platform/tc002/daemon/Process.h"
#include "platform/tc002/daemon/PropertyWorkspace.h"

namespace awtrix {
namespace tc002d {
namespace wifi {
namespace {

constexpr char kInterface[] = "wlan0";
constexpr char kInterfaceDirectory[] = "/sys/class/net/wlan0";
constexpr int kSyslogActionClear = 5;

bool parseHex(std::string_view text, unsigned long long& out) {
  if (text.empty() || text.size() > 16) return false;
  out = 0;
  for (char c : text) {
    int digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else return false;
    out = out << 4 | unsigned(digit);
  }
  return true;
}

bool allHex(std::string_view text) {
  unsigned long long ignored = 0;
  for (std::size_t at = 0; at < text.size(); at += 8)
    if (!parseHex(text.substr(at, 8), ignored)) return false;
  return !text.empty();
}

class NativeWifiSystem final : public WifiSystem {
 public:
  bool interfacePresent() override { return access(kInterfaceDirectory, F_OK) == 0; }

  std::string interfaceAttribute(const char* name) override {
    std::string text;
    if (!posix::readText(std::string(kInterfaceDirectory) + "/" + name, text, 256)) return {};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
  }

  bool setInterfaceUp(int& error) override {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { error = errno; return false; }
    struct ifreq request {};
    std::strncpy(request.ifr_name, kInterface, IFNAMSIZ - 1);
    bool ok = ioctl(fd, SIOCGIFFLAGS, &request) == 0;
    if (ok && !(request.ifr_flags & IFF_UP)) {
      request.ifr_flags = static_cast<short>(request.ifr_flags | IFF_UP);
      ok = ioctl(fd, SIOCSIFFLAGS, &request) == 0;
    }
    if (!ok) error = errno;
    close(fd);
    return ok;
  }

  pid_t loadModule(const std::string& path, const std::string& parameters) override {
    return forkTask([&]() { return loadKernelModule(path, parameters, ::geteuid()); }, SIGKILL);
  }

  int writeKernelAttribute(const std::string& path, const std::string& value) override {
    return writeAttribute(path, value);
  }

  int clearKernelLog() override { return klogctl(kSyslogActionClear, nullptr, 0) == 0 ? 0 : errno; }

  pid_t setProperty(const std::string& key, const std::string& value) override {
    ProcessSpec spec;
    std::string error;
    if (!PropertyWorkspace::setprop("/bin/setprop", key, value, spec)) return -1;
    return spawnProcess(spec, error);
  }

  pid_t startSupplicant(const std::string& program, const std::string& configPath) override {
    ProcessSpec spec;
    spec.path = program;
    spec.argv = {"wpa_supplicant", "-Dnl80211", std::string("-i") + kInterface, "-c" + configPath};
    spec.parentDeathSignal = SIGTERM;
    spec.umask = 077;
    std::string error;
    return spawnProcess(spec, error);
  }

  pid_t runTask(const std::function<int()>& task) override { return forkTask(task, SIGKILL); }

  std::vector<ProcessInfo> processes(const char* comm) override {
    std::vector<ProcessInfo> found;
    DIR* directory = opendir("/proc");
    if (!directory) return found;
    const std::string wanted = std::string(comm) + "\n";
    for (unsigned scanned = 0; scanned < 4096; ++scanned) {
      const dirent* entry = readdir(directory);
      if (!entry) break;
      char* end = nullptr;
      const long pid = std::strtol(entry->d_name, &end, 10);
      if (pid <= 0 || !end || *end) continue;
      std::string text;
      const std::string base = std::string("/proc/") + entry->d_name;
      if (!posix::readText(base + "/comm", text, 64) || text != wanted) continue;
      ProcessInfo info;
      info.pid = static_cast<pid_t>(pid);
      if (posix::readText(base + "/cmdline", text, 4096)) {
        for (char& c : text) if (c == '\0') c = ' ';
        while (!text.empty() && text.back() == ' ') text.pop_back();
        info.cmdline = text;
      }
      found.push_back(std::move(info));
    }
    closedir(directory);
    return found;
  }

  int tcpListeners(unsigned port) override {
    std::string table;
    if (!posix::readText("/proc/net/tcp", table, 256 * 1024)) return -1;
    int count = countTcpListeners(table, port);
    if (count < 0) return -1;
    if (posix::readText("/proc/net/tcp6", table, 256 * 1024)) {
      const int six = countTcpListeners(table, port);
      if (six < 0) return -1;
      count += six;
    } else if (errno != ENOENT) {
      return -1;
    }
    return count;
  }

  void signal(pid_t pid, int number) override {
    if (pid > 0) kill(pid, number);
  }

  int64_t nowMs() override { return posix::monotonicMs(); }

 private:
};

}

std::unique_ptr<WifiSystem> nativeWifiSystem() { return std::make_unique<NativeWifiSystem>(); }

int countTcpListeners(std::string_view table, unsigned port) {
  const std::size_t headerEnd = table.find('\n');
  if (headerEnd == std::string_view::npos || table.substr(0, headerEnd).find("local_address") == std::string_view::npos)
    return -1;
  table.remove_prefix(headerEnd + 1);
  int count = 0;
  while (!table.empty()) {
    const std::size_t end = table.find('\n');
    std::string_view line = table.substr(0, end);
    table = end == std::string_view::npos ? std::string_view() : table.substr(end + 1);
    std::string_view fields[4];
    unsigned found = 0;
    while (found < 4) {
      const std::size_t begin = line.find_first_not_of(" \t\r");
      if (begin == std::string_view::npos) break;
      line.remove_prefix(begin);
      const std::size_t stop = line.find_first_of(" \t\r");
      fields[found++] = line.substr(0, stop);
      line = stop == std::string_view::npos ? std::string_view() : line.substr(stop);
    }
    if (found == 0) continue;
    if (found < 4) return -1;
    const std::size_t colon = fields[1].rfind(':');
    unsigned long long localPort = 0, state = 0;
    if (colon == std::string_view::npos || (colon != 8 && colon != 32) || !allHex(fields[1].substr(0, colon)) ||
        !parseHex(fields[1].substr(colon + 1), localPort) || localPort > 65535 ||
        fields[3].size() != 2 || !parseHex(fields[3], state))
      return -1;
    if (state == 0x0a && localPort == port) ++count;
  }
  return count;
}

int writeAttribute(const std::string& path, std::string_view value) {
  const int fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) return errno;
  ssize_t written;
  do written = write(fd, value.data(), value.size());
  while (written < 0 && errno == EINTR);
  const int error = written < 0 ? errno : static_cast<std::size_t>(written) == value.size() ? 0 : EIO;
  close(fd);
  return error;
}

}
}
}
