#include "platform/posix/Files.h"
#include "platform/linux/LinuxMemory.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>

namespace awtrix {

bool parseMemAvailable(std::string_view meminfo, std::uint64_t& bytes) {
  constexpr std::string_view kKey = "MemAvailable:";
  for (std::size_t at = 0; at < meminfo.size();) {
    const std::size_t end = std::min(meminfo.find('\n', at), meminfo.size());
    std::string_view line = meminfo.substr(at, end - at);
    at = end + 1;
    if (line.substr(0, kKey.size()) != kKey) continue;
    line.remove_prefix(kKey.size());
    while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
    std::uint64_t kib = 0;
    const auto parsed = std::from_chars(line.data(), line.data() + line.size(), kib);
    if (parsed.ec != std::errc() || parsed.ptr == line.data() ||
        std::string_view(parsed.ptr, static_cast<std::size_t>(line.data() + line.size() - parsed.ptr)) != " kB" ||
        kib > UINT64_MAX / 1024)
      return false;
    bytes = kib * 1024;
    return true;
  }
  return false;
}

bool readMemAvailable(std::uint64_t& bytes) {
  std::string text;
  return posix::readText("/proc/meminfo", text, 64 * 1024) && parseMemAvailable(text, bytes);
}

std::size_t scriptHeapBudget(std::uint64_t availableBytes) {
  const std::uint64_t quarter = availableBytes / 4;
  return quarter < kScriptHeapCapBytes ? static_cast<std::size_t>(quarter) : kScriptHeapCapBytes;
}

void LinuxMemoryGauge::tick(int64_t nowMs) {
  if (nowMs < nextSampleMs_) return;
  nextSampleMs_ = nowMs + kSampleMs;
  sample();
}

bool LinuxMemoryGauge::sample() {
  std::uint64_t bytes = 0;
  if (!readMemAvailable(bytes)) return known_;
  available_ = bytes;
  lowest_ = known_ ? std::min(lowest_, bytes) : bytes;
  known_ = true;
  return true;
}

}
