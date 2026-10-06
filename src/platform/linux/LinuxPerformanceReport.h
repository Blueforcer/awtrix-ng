#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>

namespace awtrix {

// Inclusive upper bounds; the final counter is the unbounded overflow bucket.
inline constexpr std::array<uint64_t, 18> kPerformanceBoundsUs{
    100, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000, 20000,
    24000, 32000, 40000, 48000, 64000, 100000, 250000, 1000000};

struct LinuxDurationStats {
  uint64_t count = 0, totalUs = 0, maximumUs = 0;
  std::array<uint64_t, kPerformanceBoundsUs.size() + 1> histogram{};
  void add(uint64_t durationUs);
  std::size_t percentile95Bucket() const;
};

// All timestamps use the same monotonic clock, including the scheduler's deadline.
struct LinuxFrameTiming {
  int64_t startedUs, renderStartedUs, renderEndedUs, displayEndedUs;
  int64_t workEndedUs, loopEndedUs, deadlineUs;
};

// Fixed-size optional accounting. Nested show/transfer spans overlap; only leaf phases add up.
enum class LinuxPanelPhase : std::size_t {
  Show, Packing, Transfer, FrameWaitRequested, FrameWait, Descriptor,
  GpioLow, PreWriteSleep, SpiWrite, PostWriteSleep, GpioHigh, Count
};

struct LinuxPanelDuration {
  uint64_t count = 0, totalUs = 0, maximumUs = 0;
  void add(uint64_t us) {
    ++count;
    totalUs += us;
    if (us > maximumUs) maximumUs = us;
  }
};

struct LinuxPanelTiming {
  using Clock = int64_t (*)();
  Clock clock = nullptr;
  bool active = false, valid = true;
  std::array<LinuxPanelDuration, static_cast<std::size_t>(LinuxPanelPhase::Count)> phases{};
  uint64_t shows = 0, showFailures = 0, transfers = 0, transferFailures = 0;
  uint64_t spiCalls = 0, spiBytesRequested = 0, spiBytesWritten = 0, spiErrors = 0, spiShortWrites = 0;
  uint64_t descriptorErrors = 0, gpioLowErrors = 0, gpioHighErrors = 0;

  int64_t now() const {
    // Measurement must not replace the errno of a failed device syscall.
    const int saved = errno;
    const int64_t value = clock ? clock() : -1;
    errno = saved;
    return value;
  }
  void duration(LinuxPanelPhase phase, uint64_t us) {
    if (active) phases[static_cast<std::size_t>(phase)].add(us);
  }
  void elapsed(LinuxPanelPhase phase, int64_t start) {
    const auto end = now();
    if (start < 0 || end < start) valid = false;
    else duration(phase, static_cast<uint64_t>(end - start));
  }
  void spiResult(std::size_t requested, std::ptrdiff_t result) {
    if (!active) return;
    ++spiCalls;
    spiBytesRequested += requested;
    if (result < 0) ++spiErrors;
    else {
      spiBytesWritten += static_cast<uint64_t>(result);
      if (static_cast<std::size_t>(result) != requested) ++spiShortWrites;
    }
  }
};

class LinuxPanelSpan {
 public:
  LinuxPanelSpan(LinuxPanelTiming* timing, LinuxPanelPhase phase)
      : timing_(timing && timing->active ? timing : nullptr), phase_(phase),
        started_(timing_ ? timing_->now() : 0) {}
  ~LinuxPanelSpan() { if (timing_) timing_->elapsed(phase_, started_); }
  LinuxPanelSpan(const LinuxPanelSpan&) = delete;
  LinuxPanelSpan& operator=(const LinuxPanelSpan&) = delete;
 private:
  LinuxPanelTiming* timing_;
  LinuxPanelPhase phase_;
  int64_t started_;
};

class LinuxPerformanceReport {
 public:
  using Clock = int64_t (*)();
  LinuxPerformanceReport(uint32_t frameBudgetUs, bool physicalDisplay, Clock clock);
  ~LinuxPerformanceReport();
  LinuxPerformanceReport(const LinuxPerformanceReport&) = delete;
  LinuxPerformanceReport& operator=(const LinuxPerformanceReport&) = delete;

  bool open(const std::string& path, std::string& error);
  bool enabled() const { return fd_ >= 0; }
  LinuxPanelTiming* panelTiming() { return enabled() && physicalDisplay_ ? &panel_ : nullptr; }
  int64_t now() const { return enabled() ? clock_() : 0; }
  void start();
  int64_t beginFrame();
  void record(const LinuxFrameTiming& frame);
  void stop();
  // An unclosed report is written as incomplete during stack unwinding/early return.
  bool finish(bool clean);

 private:
  bool writeReport(const char* outcome) noexcept;
  int fd_ = -1;
  const uint32_t frameBudgetUs_;
  const bool physicalDisplay_;
  const Clock clock_;
  bool running_ = false, started_ = false, measurementValid_ = true;
  int64_t startedUs_ = 0, endedUs_ = 0;
  int64_t cpuUserStartUs_ = 0, cpuSystemStartUs_ = 0;
  int64_t cpuUserUs_ = 0, cpuSystemUs_ = 0, peakRssKiB_ = 0;
  uint64_t attemptedFrames_ = 0, deadlineMisses_ = 0, workOverBudget_ = 0;
  LinuxDurationStats render_, display_, work_, loop_, lateness_;
  LinuxPanelTiming panel_;
};

}
