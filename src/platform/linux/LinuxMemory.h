#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace awtrix {

// MemAvailable of /proc/meminfo, the memory the kernel can hand out without swapping: the Linux
// counterpart of the ESP32's free heap. Both leave bytes alone when the kernel does not say.
bool parseMemAvailable(std::string_view meminfo, std::uint64_t& bytes);
bool readMemAvailable(std::uint64_t& bytes);

// The script VM gets a quarter of memory available at startup, at most kScriptHeapCapBytes.
constexpr std::size_t kScriptHeapCapBytes = 4u << 20;
std::size_t scriptHeapBudget(std::uint64_t availableBytes);

// MemAvailable now and the lowest value seen since start. The low-water mark comes from a sample
// about once a second and from every read, so a dip shorter than that can pass unseen.
class LinuxMemoryGauge {
 public:
  static constexpr int64_t kSampleMs = 1000;

  void tick(int64_t nowMs);
  bool sample();
  std::uint64_t available() const { return available_; }
  std::uint64_t lowest() const { return lowest_; }

 private:
  int64_t nextSampleMs_ = 0;
  bool known_ = false;
  std::uint64_t available_ = 0;
  std::uint64_t lowest_ = 0;
};

}
