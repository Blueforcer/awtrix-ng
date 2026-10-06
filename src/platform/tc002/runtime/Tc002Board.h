#pragma once
#include "platform/tc002/contract/tc002_layout.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>

#include "platform/linux/LinuxBoard.h"
#include "platform/linux/LinuxPerformanceReport.h"

namespace awtrix {

// The transport seam lets host tests exercise panel failures without device nodes.
class Tc002PanelIo {
 public:
  virtual ~Tc002PanelIo() = default;
  virtual bool acquire(std::string& error) = 0;
  virtual void setTiming(LinuxPanelTiming*) {}
  virtual std::ptrdiff_t transfer(const uint8_t* bytes, std::size_t size,
                                std::string& error) = 0;
  virtual void release() noexcept = 0;
};

class Tc002Board final : public LinuxBoard {
 public:
  static constexpr int kWidth = TC002_PANEL_WIDTH;
  static constexpr int kHeight = TC002_PANEL_HEIGHT;
  static constexpr std::size_t kRowBytes = TC002_PANEL_ROW_BYTES;
  static constexpr std::size_t kFrameBytes = kRowBytes * kHeight;
  static const render::OutputTable kPanelLight;

  Tc002Board();
  explicit Tc002Board(std::unique_ptr<Tc002PanelIo> io);
  ~Tc002Board() override;
  const char* name() const override { return "TC002"; }
  bool displayReady() const override { return ready_; }
  const std::string& error() const { return error_; }
  void setTiming(LinuxPanelTiming* timing) { timing_ = timing; }
  void begin() override;
  // Continuous output: each call sends one complete frame and exposes the
  // previously buffered frame. The runtime also repeats unchanged content.
  void show(const Canvas& canvas) override;
  void setBrightness(uint8_t brightness) override;
  void applyColorGrade(const render::GradeParams& grade) override;

 private:
  bool submit(unsigned transfers);
  void fail();

  std::unique_ptr<Tc002PanelIo> io_;
  std::array<uint8_t, kFrameBytes> frame_{};
  LinuxPanelTiming* timing_ = nullptr;
  render::ColorGrade grade_;
  std::string error_;
  bool ready_ = false;
  bool acquired_ = false;
};

}
