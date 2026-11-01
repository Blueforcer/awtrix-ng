#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

namespace awtrix {

// MemAvailable of /proc/meminfo, the memory the kernel can hand out without swapping: the Linux
// counterpart of the ESP32's free heap. Both leave bytes alone when the kernel does not say.
bool parseMemAvailable(std::string_view meminfo, std::uint64_t& bytes);
bool readMemAvailable(std::uint64_t& bytes);

constexpr std::uint64_t kKeepFreeBytes = 4u << 20;

class MemoryRoom {
 public:
  using Reader = std::function<bool(std::uint64_t& available)>;

  MemoryRoom(Reader available, std::uint64_t keepFree) : available_(std::move(available)), keepFree_(keepFree) {}

  bool fits(std::uint64_t bytes) const {
    std::uint64_t available = 0;
    return available_ && available_(available) && available >= keepFree_ && available - keepFree_ >= bytes;
  }

 private:
  Reader available_;
  std::uint64_t keepFree_;
};

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
