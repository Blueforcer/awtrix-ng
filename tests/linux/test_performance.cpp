#include "../support.h"
#include "platform/linux/LinuxPerformanceReport.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int64_t fakeNow = 0;
int clockCalls = 0;
int64_t clockNow() { ++clockCalls; return fakeNow; }
int64_t errnoClock() { errno = EBUSY; return fakeNow; }
using awtrix::test::require;
std::string read(const std::string& path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void contains(const std::string& json, const char* expected) {
  require(json.find(expected) != std::string::npos, expected);
}
}

int main() {
  using namespace awtrix;
  LinuxDurationStats stats;
  for (int i = 0; i < 95; ++i) stats.add(100);
  for (int i = 0; i < 5; ++i) stats.add(1000001);
  require(stats.count == 100 && stats.histogram.front() == 95 && stats.histogram.back() == 5,
          "inclusive fixed buckets and overflow");
  require(stats.percentile95Bucket() == 0, "nearest-rank p95 at exact 95-percent boundary");
  stats.add(1000001);
  require(stats.percentile95Bucket() == kPerformanceBoundsUs.size(), "p95 overflow stays explicit");

  char directory[] = "/tmp/awtrix-performance-XXXXXX";
  require(mkdtemp(directory), "private test directory");
  const std::string path = std::string(directory) + "/report.json";
  std::string error;
  LinuxPerformanceReport report(24000, true, clockNow);
  require(report.panelTiming() == nullptr, "disabled reports expose no device accounting");
  require(report.open(path, error), "open new report");
  auto* panel = report.panelTiming();
  require(panel && !panel->active, "opened report does not count initialization");
  const int beforeInactive = clockCalls;
  { LinuxPanelSpan inactive(panel, LinuxPanelPhase::Transfer); }
  panel->spiResult(3072, 3072);
  require(clockCalls == beforeInactive && panel->spiCalls == 0, "inactive spans have no clock/syscall accounting");
  fakeNow = 1000000;
  report.start();
  require(report.beginFrame() == fakeNow, "first monotonic frame start");
  report.record({1000000, 1000200, 1000700, 1001000, 1020000, 1024000, 1024000});
  fakeNow = 1024000;
  report.beginFrame();
  report.record({1024000, 1024500, 1025500, 1027000, 1054000, 1054000, 1048000});
  fakeNow = 1054000;
  report.beginFrame(); // A failed third frame is not disguised as a completed frame.
  {
    LinuxPanelSpan show(panel, LinuxPanelPhase::Show);
    ++panel->shows;
    { LinuxPanelSpan pack(panel, LinuxPanelPhase::Packing); fakeNow += 20; }
    fakeNow += 30;
    { LinuxPanelSpan spi(panel, LinuxPanelPhase::SpiWrite); fakeNow += 200; }
    panel->spiResult(3072, 3072);
    panel->spiResult(3072, 17);
    panel->spiResult(3072, -1);
    panel->duration(LinuxPanelPhase::FrameWaitRequested, 15000);
  }
  fakeNow = 1055000;
  report.stop();
  panel->spiResult(3072, 3072);
  { LinuxPanelSpan cleanup(panel, LinuxPanelPhase::Transfer); }
  require(!panel->active && panel->spiCalls == 3, "stop freezes device accounting before cleanup");
  fakeNow = 2000000; // Shutdown time does not inflate the measured loop window.
  require(report.finish(false), "flush valid failed-run measurements");
  const auto json = read(path);
  contains(json, "\"platform\":\"tc002\",\"status\":\"failed\",\"measurement_valid\":true");
  contains(json, "\"frame_budget_us\":24000,\"attempted_frames\":3,\"frames\":2,\"aborted_frames\":1");
  contains(json, "\"elapsed_us\":55000,\"deadline_misses\":1,\"work_over_budget\":1");
  contains(json, "\"render\":{\"samples\":2,\"total_us\":1500,\"avg_us\":750,\"max_us\":1000,\"p95_upper_us\":1000");
  contains(json, "\"display\":{\"samples\":2,\"total_us\":1800,\"avg_us\":900,\"max_us\":1500");
  contains(json, "\"work\":{\"samples\":2,\"total_us\":50000,\"avg_us\":25000,\"max_us\":30000");
  contains(json, "\"loop\":{\"samples\":2,\"total_us\":54000,\"avg_us\":27000,\"max_us\":30000");
  contains(json, "\"deadline_lateness\":{\"samples\":2,\"total_us\":6000");
  contains(json, "\"panel\":{\"scope\":\"runtime_loop\",\"measurement_valid\":true,\"shows\":1");
  contains(json, "\"spi_calls\":3,\"spi_bytes_requested\":9216,\"spi_bytes_written\":3089,\"spi_errors\":1,\"spi_short_writes\":1");
  contains(json, "\"show\":{\"samples\":1,\"total_us\":250,\"avg_us\":250,\"max_us\":250}");
  contains(json, "\"packing\":{\"samples\":1,\"total_us\":20");
  contains(json, "\"spi_write\":{\"samples\":1,\"total_us\":200");
  contains(json, "\"frame_wait_requested\":{\"samples\":1,\"total_us\":15000");
  require(json.size() < 8192, "fixed panel aggregates retain bounded report size");
  struct stat file{};
  require(stat(path.c_str(), &file) == 0 && (file.st_mode & 0777) == 0600, "report stays private");
  LinuxPerformanceReport duplicate(24000, false, clockNow);
  require(!duplicate.open(path, error), "never truncate an existing report");
  require(read(path) == json, "duplicate open preserves evidence");
  const std::string incomplete = std::string(directory) + "/incomplete.json";
  {
    LinuxPerformanceReport abandoned(24000, false, clockNow);
    require(abandoned.open(incomplete, error), "early-return report open");
  }
  contains(read(incomplete), "\"status\":\"incomplete\"");
  require(read(incomplete).find("\"panel\"") == std::string::npos, "headless report omits physical-panel metrics");
  LinuxPanelTiming errorTiming;
  errorTiming.active = true; errorTiming.clock = errnoClock;
  errno = EIO;
  { LinuxPanelSpan checked(&errorTiming, LinuxPanelPhase::SpiWrite); fakeNow += 12; }
  require(errno == EIO, "phase clock preserves errno from the operation being measured");
  const std::string invalidPanel = std::string(directory) + "/invalid-panel.json";
  LinuxPerformanceReport brokenPanel(24000, true, clockNow);
  require(brokenPanel.open(invalidPanel, error), "invalid panel timing report open");
  brokenPanel.start();
  { LinuxPanelSpan reversed(brokenPanel.panelTiming(), LinuxPanelPhase::GpioLow); --fakeNow; }
  ++fakeNow;
  require(!brokenPanel.finish(true), "backwards panel clock invalidates the entire measurement");
  contains(read(invalidPanel), "\"measurement_valid\":false");
  const std::string invalid = std::string(directory) + "/invalid.json";
  LinuxPerformanceReport broken(24000, false, clockNow);
  require(broken.open(invalid, error), "invalid timing report open");
  broken.start();
  const auto began = broken.beginFrame();
  broken.record({began, began, began - 1, began, began, began, began + 24000});
  require(!broken.finish(true), "invalid clock ordering is an explicit measurement failure");
  contains(read(invalid), "\"measurement_valid\":false");
  const std::string removed = std::string(directory) + "/removed.json";
  LinuxPerformanceReport removedReport(24000, false, clockNow);
  require(removedReport.open(removed, error), "report subsequently removed by application reset");
  require(unlink(removed.c_str()) == 0, "remove live report path");
  require(!removedReport.finish(true), "unlinked output must not claim a successful report");
  unlink(path.c_str()); unlink(incomplete.c_str()); unlink(invalid.c_str()); unlink(invalidPanel.c_str()); rmdir(directory);
  std::puts("Performance contracts passed: bounded histograms, deadlines, CPU/window scope, private reports and invalid timing");
}
