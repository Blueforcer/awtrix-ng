#include "core/api/JsonWriter.h"
#include "platform/posix/Files.h"
#include "platform/linux/LinuxPerformanceReport.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace awtrix {
namespace {
int64_t micros(const timeval& value) { return int64_t(value.tv_sec) * 1000000 + value.tv_usec; }

void durationJson(api::JsonWriter& out, const LinuxDurationStats& duration) {
  out.beginObject().member("samples", duration.count).member("total_us", duration.totalUs)
      .member("avg_us", duration.count ? double(duration.totalUs) / duration.count : 0.0, 3)
      .member("max_us", duration.maximumUs).key("p95_upper_us");
  const auto bucket = duration.percentile95Bucket();
  if (duration.count && bucket < kPerformanceBoundsUs.size()) out.value(kPerformanceBoundsUs[bucket]);
  else out.null();
  out.key("histogram").beginArray();
  for (const auto count : duration.histogram) out.value(count);
  out.endArray().endObject();
}

void panelJson(api::JsonWriter& out, const LinuxPanelTiming& panel) {
  constexpr const char* names[]{"show", "packing", "transfer", "frame_wait_requested", "frame_wait",
      "descriptor", "gpio_low", "pre_write_sleep", "spi_write", "post_write_sleep", "gpio_high"};
  static_assert(sizeof(names) / sizeof(names[0]) == static_cast<std::size_t>(LinuxPanelPhase::Count));
  out.beginObject().member("scope", "runtime_loop").member("measurement_valid", panel.valid)
      .member("shows", panel.shows).member("show_failures", panel.showFailures)
      .member("transfers", panel.transfers).member("transfer_failures", panel.transferFailures)
      .member("spi_calls", panel.spiCalls).member("spi_bytes_requested", panel.spiBytesRequested)
      .member("spi_bytes_written", panel.spiBytesWritten).member("spi_errors", panel.spiErrors)
      .member("spi_short_writes", panel.spiShortWrites).member("descriptor_errors", panel.descriptorErrors)
      .member("gpio_low_errors", panel.gpioLowErrors).member("gpio_high_errors", panel.gpioHighErrors)
      .key("durations").beginObject();
  for (std::size_t i = 0; i < panel.phases.size(); ++i) {
    const auto& d = panel.phases[i];
    out.key(names[i]).beginObject().member("samples", d.count).member("total_us", d.totalUs)
        .member("avg_us", d.count ? double(d.totalUs) / d.count : 0.0, 3).member("max_us", d.maximumUs).endObject();
  }
  out.endObject().endObject();
}
}

void LinuxDurationStats::add(uint64_t durationUs) {
  ++count;
  totalUs += durationUs;
  maximumUs = std::max(maximumUs, durationUs);
  const auto bucket = std::lower_bound(kPerformanceBoundsUs.begin(), kPerformanceBoundsUs.end(), durationUs);
  ++histogram[static_cast<std::size_t>(bucket - kPerformanceBoundsUs.begin())];
}

std::size_t LinuxDurationStats::percentile95Bucket() const {
  // ceil(95 * count / 100), without multiplying the full sample count.
  const uint64_t rank = count / 100 * 95 + ((count % 100) * 95 + 99) / 100;
  uint64_t cumulative = 0;
  for (std::size_t i = 0; i < histogram.size(); ++i) {
    cumulative += histogram[i];
    if (count && cumulative >= rank) return i;
  }
  return kPerformanceBoundsUs.size();
}

LinuxPerformanceReport::LinuxPerformanceReport(uint32_t budget, bool physical, Clock clock)
    : frameBudgetUs_(budget), physicalDisplay_(physical), clock_(clock) { panel_.clock = clock; }

LinuxPerformanceReport::~LinuxPerformanceReport() {
  if (enabled()) writeReport("incomplete");
}

bool LinuxPerformanceReport::open(const std::string& path, std::string& error) {
  if (enabled() || path.empty() || !clock_ || !frameBudgetUs_) {
    error = "invalid performance report configuration";
    return false;
  }
  fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd_ < 0) { error = std::strerror(errno); return false; }
  struct stat info{};
  if (fstat(fd_, &info) < 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 || fchmod(fd_, 0600) < 0) {
    error = "performance report must be a new private regular file";
    ::close(fd_); fd_ = -1;
    return false;
  }
  return true;
}

void LinuxPerformanceReport::start() {
  if (!enabled() || started_) return;
  started_ = running_ = true;
  panel_.active = physicalDisplay_;
  startedUs_ = clock_();
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) < 0) measurementValid_ = false;
  else {
    cpuUserStartUs_ = micros(usage.ru_utime);
    cpuSystemStartUs_ = micros(usage.ru_stime);
  }
}

int64_t LinuxPerformanceReport::beginFrame() {
  if (!enabled()) return 0;
  ++attemptedFrames_;
  return clock_();
}

void LinuxPerformanceReport::record(const LinuxFrameTiming& f) {
  if (!enabled()) return;
  if (!running_ || f.startedUs < startedUs_ || f.renderStartedUs < f.startedUs ||
      f.renderEndedUs < f.renderStartedUs || f.displayEndedUs < f.renderEndedUs ||
      f.workEndedUs < f.displayEndedUs || f.loopEndedUs < f.workEndedUs ||
      loop_.count >= attemptedFrames_) {
    measurementValid_ = false;
    return;
  }
  render_.add(f.renderEndedUs - f.renderStartedUs);
  display_.add(f.displayEndedUs - f.renderEndedUs);
  work_.add(f.workEndedUs - f.startedUs);
  loop_.add(f.loopEndedUs - f.startedUs);
  const uint64_t late = f.workEndedUs > f.deadlineUs ? f.workEndedUs - f.deadlineUs : 0;
  lateness_.add(late);
  if (late) ++deadlineMisses_;
  if (f.workEndedUs - f.startedUs > frameBudgetUs_) ++workOverBudget_;
}

void LinuxPerformanceReport::stop() {
  if (!enabled() || !running_) return;
  endedUs_ = clock_();
  panel_.active = false;
  if (!panel_.valid) measurementValid_ = false;
  running_ = false;
  rusage usage{};
  if (endedUs_ < startedUs_ || getrusage(RUSAGE_SELF, &usage) < 0) measurementValid_ = false;
  else {
    cpuUserUs_ = micros(usage.ru_utime) - cpuUserStartUs_;
    cpuSystemUs_ = micros(usage.ru_stime) - cpuSystemStartUs_;
    peakRssKiB_ = usage.ru_maxrss;
    if (cpuUserUs_ < 0 || cpuSystemUs_ < 0) measurementValid_ = false;
  }
}

bool LinuxPerformanceReport::finish(bool clean) {
  return !enabled() || writeReport(clean ? "clean" : "failed");
}

bool LinuxPerformanceReport::writeReport(const char* outcome) noexcept {
  bool okay = true;
  try {
    stop();
    std::string json;
    api::JsonWriter out(json);
    out.beginObject().member("schema", 1).member("platform", physicalDisplay_ ? "tc002" : "linux")
        .member("status", outcome).member("measurement_valid", measurementValid_)
        .member("frame_budget_us", frameBudgetUs_).member("attempted_frames", attemptedFrames_)
        .member("frames", loop_.count).member("aborted_frames", attemptedFrames_ - loop_.count)
        .member("elapsed_us", started_ ? std::max<int64_t>(0, endedUs_ - startedUs_) : 0)
        .member("deadline_misses", deadlineMisses_).member("work_over_budget", workOverBudget_)
        .member("cpu_user_us", cpuUserUs_).member("cpu_system_us", cpuSystemUs_)
        .member("process_peak_rss_kib", peakRssKiB_).key("histogram_upper_bounds_us").beginArray();
    for (uint64_t bound : kPerformanceBoundsUs) out.value(bound);
    out.null().endArray().key("render"); durationJson(out, render_);
    out.key("display"); durationJson(out, display_);
    out.key("work"); durationJson(out, work_);
    out.key("loop"); durationJson(out, loop_);
    out.key("deadline_lateness"); durationJson(out, lateness_);
    if (physicalDisplay_) { out.key("panel"); panelJson(out, panel_); }
    out.endObject();
    json += '\n';
    struct stat file{};
    // Reset/cleanup can unlink an open report; writing that inode must not claim saved evidence.
    if (fstat(fd_, &file) < 0 || !S_ISREG(file.st_mode) || file.st_nlink != 1) okay = false;
    if (okay) okay = posix::writeAll(fd_, json.data(), json.size());
    if (okay && fsync(fd_) < 0) okay = false;
  } catch (...) { okay = false; }
  if (::close(fd_) < 0) okay = false;
  fd_ = -1;
  if (!okay || !measurementValid_) std::fputs("Performance report failed or timing/resource measurements invalid\n", stderr);
  return okay && measurementValid_;
}

}
