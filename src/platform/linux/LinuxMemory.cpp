#include "platform/linux/LinuxMemory.h"

#include <algorithm>

#include "platform/host/Memory.h"

namespace awtrix {

bool parseMemAvailable(std::string_view meminfo, std::uint64_t& bytes) { return os::parseMemAvailable(meminfo, bytes); }

bool readMemAvailable(std::uint64_t& bytes) { return os::availableMemory(bytes); }

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
