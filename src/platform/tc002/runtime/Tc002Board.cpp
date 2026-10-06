#include "platform/tc002/runtime/Tc002Board.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "platform/tc002/contract/tc002_layout.h"

namespace awtrix {
namespace {

constexpr const char* kSpiPath = TC002_PANEL_SPI;
constexpr const char* kGpio = TC002_PANEL_LATCH "/";
constexpr uint32_t kSpiHz = TC002_PANEL_SPI_HZ;

bool failed(std::string& error, const std::string& operation) {
  error = operation + ": " + std::strerror(errno);
  return false;
}

class DevicePanelIo final : public Tc002PanelIo {
 public:
  ~DevicePanelIo() override { release(); }

  // The supervisor checks the board model and brings the latch to idle high before it starts the
  // runtime; the exclusive SPI lock keeps a second runtime out.
  bool acquire(std::string& error) override {
    spi_ = open(kSpiPath, O_RDWR | O_CLOEXEC);
    if (spi_ < 0) return failed(error, "open TC002 SPI");
    if (flock(spi_, LOCK_EX | LOCK_NB) < 0) return failed(error, "lock TC002 SPI");
    struct stat device{};
    if (fstat(spi_, &device) < 0) return failed(error, "identify TC002 SPI");
    if (!S_ISCHR(device.st_mode)) {
      error = "TC002 SPI is not a character device";
      return false;
    }
    device_ = device.st_rdev;
    if (ioctl(spi_, SPI_IOC_RD_MODE, &savedMode_) < 0 ||
        ioctl(spi_, SPI_IOC_RD_BITS_PER_WORD, &savedBits_) < 0 ||
        ioctl(spi_, SPI_IOC_RD_LSB_FIRST, &savedLsb_) < 0 ||
        ioctl(spi_, SPI_IOC_RD_MAX_SPEED_HZ, &savedSpeed_) < 0)
      return failed(error, "save TC002 SPI configuration");
    saved_ = true;
    uint8_t mode = SPI_MODE_0, bits = 8, lsb = 0;
    uint32_t speed = kSpiHz;
    if (ioctl(spi_, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(spi_, SPI_IOC_RD_MODE, &mode) < 0 ||
        ioctl(spi_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(spi_, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0 ||
        ioctl(spi_, SPI_IOC_WR_LSB_FIRST, &lsb) < 0 ||
        ioctl(spi_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0 ||
        ioctl(spi_, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0 ||
        ioctl(spi_, SPI_IOC_RD_LSB_FIRST, &lsb) < 0)
      return failed(error, "configure TC002 SPI");
    if (mode != SPI_MODE_0 || bits != 8 || lsb != 0 || speed != kSpiHz) {
      error = "TC002 SPI configuration readback differs";
      return false;
    }
    gpio_ = open((std::string(kGpio) + "direction").c_str(), O_WRONLY | O_CLOEXEC);
    if (gpio_ < 0) return failed(error, "open TC002 latch");
    return true;
  }

  void setTiming(LinuxPanelTiming* timing) override { timing_ = timing; }

  std::ptrdiff_t transfer(const uint8_t* bytes, std::size_t size, std::string& error) override {
    auto* measured = timing_ && timing_->active ? timing_ : nullptr;
    LinuxPanelSpan transferSpan(measured, LinuxPanelPhase::Transfer);
    if (measured) { ++measured->transfers; ++measured->transferFailures; }
    if (spi_ < 0 || gpio_ < 0 || size != Tc002Board::kFrameBytes) {
      error = "invalid TC002 transfer";
      return -1;
    }
    {
      LinuxPanelSpan span(measured, LinuxPanelPhase::FrameWait);
      if (measured) {
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            nextFrame_ - std::chrono::steady_clock::now()).count();
        measured->duration(LinuxPanelPhase::FrameWaitRequested, remaining > 0 ? remaining : 0);
      }
      std::this_thread::sleep_until(nextFrame_);
    }
    struct stat device{};
    bool identified;
    {
      LinuxPanelSpan span(measured, LinuxPanelPhase::Descriptor);
      identified = fstat(spi_, &device) == 0 && S_ISCHR(device.st_mode) && device.st_rdev == device_;
    }
    if (!identified) {
      if (measured) ++measured->descriptorErrors;
      error = "TC002 SPI descriptor identity changed";
      return -1;
    }
    latchLow_ = true;
    bool low;
    {
      LinuxPanelSpan span(measured, LinuxPanelPhase::GpioLow);
      low = latch(false);
    }
    if (!low) {
      if (measured) ++measured->gpioLowErrors;
      failed(error, "lower TC002 latch");
      return -1;
    }
    {
      LinuxPanelSpan span(measured, LinuxPanelPhase::PreWriteSleep);
      std::this_thread::sleep_for(std::chrono::milliseconds(TC002_PANEL_LATCH_SETTLE_MS));
    }
    // A short SPI write is a failed frame, never a resumable byte stream.
    ssize_t written;
    {
      LinuxPanelSpan span(measured, LinuxPanelPhase::SpiWrite);
      written = write(spi_, bytes, size);
    }
    const int writeError = errno;
    if (measured) measured->spiResult(size, written);
    {
      LinuxPanelSpan span(measured, LinuxPanelPhase::PostWriteSleep);
      std::this_thread::sleep_for(std::chrono::milliseconds(TC002_PANEL_LATCH_SETTLE_MS));
    }
    bool high;
    {
      LinuxPanelSpan span(measured, LinuxPanelPhase::GpioHigh);
      high = latch(true);
    }
    if (!high) {
      if (measured) ++measured->gpioHighErrors;
      failed(error, "release TC002 latch");
      return -1;
    }
    latchLow_ = false;
    nextFrame_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(TC002_PANEL_FRAME_GAP_MS);
    if (written < 0) {
      errno = writeError;
      failed(error, "write TC002 frame");
    }
    if (measured && written == static_cast<ssize_t>(size)) --measured->transferFailures;
    return written;
  }

  void release() noexcept override {
    if (gpio_ >= 0 && latchLow_ && !latch(true))
      std::fprintf(stderr, "TC002 latch restoration failed: %s\n", std::strerror(errno));
    latchLow_ = false;
    if (spi_ >= 0 && saved_) {
      // Attempt every setting even when one restore operation fails.
      const bool mode = ioctl(spi_, SPI_IOC_WR_MODE, &savedMode_) == 0;
      const bool bits = ioctl(spi_, SPI_IOC_WR_BITS_PER_WORD, &savedBits_) == 0;
      const bool lsb = ioctl(spi_, SPI_IOC_WR_LSB_FIRST, &savedLsb_) == 0;
      const bool speed = ioctl(spi_, SPI_IOC_WR_MAX_SPEED_HZ, &savedSpeed_) == 0;
      if (!mode || !bits || !lsb || !speed)
        std::fprintf(stderr, "TC002 SPI configuration restoration failed\n");
    }
    saved_ = false;
    if (gpio_ >= 0) close(gpio_);
    if (spi_ >= 0) close(spi_);
    gpio_ = spi_ = -1;
  }

 private:
  bool latch(bool high) {
    const char* text = high ? "high" : "low";
    const std::size_t length = high ? 4 : 3;
    if (lseek(gpio_, 0, SEEK_SET) < 0) return false;
    const ssize_t n = write(gpio_, text, length);
    if (n == static_cast<ssize_t>(length)) return true;
    if (n >= 0) errno = EIO;
    return false;
  }

  int spi_ = -1;
  int gpio_ = -1;
  LinuxPanelTiming* timing_ = nullptr;
  dev_t device_ = 0;
  bool saved_ = false;
  bool latchLow_ = false;
  uint8_t savedMode_ = 0, savedBits_ = 0, savedLsb_ = 0;
  uint32_t savedSpeed_ = 0;
  std::chrono::steady_clock::time_point nextFrame_{};
};

}

// Light per output code, including MCU gamma: no light below 50; runs such as 57..70
// share a level. Red, green and blue use the same curve.
const render::OutputTable Tc002Board::kPanelLight = {{
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 918, 993, 1092, 1561, 1562, 1817, 2115, 2177, 2177, 2177,
    2177, 2177, 2177, 2177, 2177, 2177, 2177, 2177, 2177, 2177, 2177, 2360,
    2647, 3063, 3086, 3102, 3102, 3102, 3102, 3102, 3102, 3102, 3102, 3242,
    3723, 4058, 4058, 4058, 4058, 4058, 4058, 4058, 4060, 4205, 4780, 5056,
    5056, 5056, 5056, 5056, 5056, 5197, 5755, 6145, 6145, 6145, 6145, 6145,
    6301, 6863, 7086, 7086, 7086, 7086, 7466, 7847, 8208, 8208, 8208, 8208,
    8452, 8773, 9104, 9119, 9134, 9148, 9527, 9906, 10284, 10425, 10566, 10708,
    10824, 10938, 11054, 11488, 11922, 12356, 12519, 12654, 12780, 12893, 13004, 13115,
    13601, 14086, 14567, 15057, 15545, 16030, 16246, 16459, 16641, 16641, 16641, 16641,
    16863, 17173, 17485, 17844, 18205, 18564, 18958, 19351, 19746, 19926, 20106, 20286,
    20697, 21108, 21520, 21939, 22358, 22777, 23149, 23510, 23869, 24122, 24376, 24629,
    25257, 25886, 26512, 27166, 27818, 28469, 28913, 29355, 29783, 29783, 29783, 29783,
    30341, 30929, 31518, 32077, 32637, 33197, 33829, 34462, 35095, 35414, 35734, 36053,
    36699, 37345, 37991, 38708, 39426, 40140, 40140, 40140, 40140, 40431, 40723, 41016,
    41678, 42341, 43004, 43509, 44015, 44520, 45334, 46149, 46963, 47178, 47393, 47608,
    48060, 48511, 48962, 49507, 50050, 50581, 51218, 51855, 52490, 53029, 53566, 54103,
    55277, 56450, 57622, 58469, 59314, 60158, 60682, 61205, 61625, 61938, 62249, 62560,
    63797, 65031, 65284, 65535
}};

Tc002Board::Tc002Board() : Tc002Board(std::make_unique<DevicePanelIo>()) {}

Tc002Board::Tc002Board(std::unique_ptr<Tc002PanelIo> io)
    : LinuxBoard(kWidth, kHeight), io_(std::move(io)) {
  grade_.setOutput(&kPanelLight);
  grade_.setBrightness(0);
}

Tc002Board::~Tc002Board() {
  if (io_) io_->setTiming(nullptr); // Final blanking is outside runtime-loop accounting.
  if (ready_) {
    frame_.fill(0);
    if (!submit(2)) std::fprintf(stderr, "TC002 panel shutdown failed: %s\n", error_.c_str());
  }
  if (acquired_) io_->release();
}

void Tc002Board::begin() {
  if (ready_) return;
  error_.clear();
  if (!io_) {
    error_ = "missing TC002 panel transport";
    return;
  }
  io_->setTiming(nullptr); // Initialization is excluded even if accounting was attached early.
  acquired_ = true;
  if (!io_->acquire(error_)) {
    if (error_.empty()) error_ = "cannot acquire TC002 panel";
    fail();
    return;
  }
  ready_ = true;
  frame_.fill(0);
  submit(2);
}

void Tc002Board::fail() {
  ready_ = false;
  if (acquired_) io_->release();
  acquired_ = false;
}

bool Tc002Board::submit(unsigned transfers) {
  // Shows form a one-frame pipeline; initialization and final black send two transfers.
  for (unsigned repeat = 0; repeat < transfers; ++repeat) {
    const auto n = io_->transfer(frame_.data(), frame_.size(), error_);
    if (n != static_cast<std::ptrdiff_t>(frame_.size())) {
      if (error_.empty()) error_ = n < 0 ? "TC002 frame transfer failed" : "short TC002 frame transfer";
      fail();
      return false;
    }
  }
  return true;
}

void Tc002Board::show(const Canvas& canvas) {
  if (!ready_) return;
  auto* measured = timing_ && timing_->active ? timing_ : nullptr;
  io_->setTiming(measured);
  LinuxPanelSpan showSpan(measured, LinuxPanelPhase::Show);
  if (measured) { ++measured->shows; ++measured->showFailures; }
  if (!canvas.valid() || canvas.width() != kWidth || canvas.height() != kHeight) {
    error_ = "TC002 requires a 52x16 canvas";
    fail();
    return;
  }
  {
    LinuxPanelSpan packingSpan(measured, LinuxPanelPhase::Packing);
    frame_.fill(0);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        const uint32_t codes = grade_.applyPixel(canvas.getPixel(x, y));
        const std::size_t offset = static_cast<std::size_t>(y) * kRowBytes + x * 3;
        frame_[offset] = static_cast<uint8_t>(codes >> 16);
        frame_[offset + 1] = static_cast<uint8_t>(codes >> 8);
        frame_[offset + 2] = static_cast<uint8_t>(codes);
      }
    }
  }
  if (submit(1) && measured) --measured->showFailures;
}

void Tc002Board::setBrightness(uint8_t brightness) {
  grade_.setBrightness(brightness);
}

void Tc002Board::applyColorGrade(const render::GradeParams& grade) {
  grade_.setGrade(grade);
}

}
