#include "platform/tc002/daemon/HardwareLease.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <initializer_list>

#include "platform/posix/Files.h"
#include "platform/tc002/contract/InputIdentity.h"
#include "platform/tc002/daemon/Log.h"
#include "platform/tc002/daemon/Process.h"

namespace awtrix {
namespace tc002d {
namespace {

constexpr int64_t kReleasePollMs = 20;
constexpr int64_t kOwnerScanBudgetMs = 40;
constexpr char kOtgRolePath[] = TC002_USB_ROLE;
constexpr char kUsbDeviceRole[] = TC002_USB_DEVICE_ROLE;

struct CloseDir {
  void operator()(DIR* directory) const { closedir(directory); }
};
using Directory = std::unique_ptr<DIR, CloseDir>;

class NativePanel final : public PanelBackend {
 public:
  explicit NativePanel(const std::string& root)
      : root_(root), gpio_(root + TC002_PANEL_LATCH), spi_(root + TC002_PANEL_SPI) {}

  bool readLatch(LatchState& out) override {
    out = LatchState{};
    struct stat info{};
    if (::stat(gpio_.c_str(), &info) < 0) return errno == ENOENT;
    std::string direction, value;
    if (!posix::readText(gpio_ + "/direction", direction, 64) || !posix::readText(gpio_ + "/value", value, 64)) return false;
    out.exported = true;
    out.direction = posix::trimmed(direction);
    out.value = posix::trimmed(value);
    return true;
  }

  bool exportLatch() override {
    return posix::writeText(root_ + "/sys/class/gpio/export", TC002_PANEL_LATCH_GPIO) || errno == EBUSY;
  }

  bool driveLatchHigh() override { return posix::writeText(gpio_ + "/direction", "high"); }

  bool panelUnowned() override {
    struct stat device{};
    if (::stat(spi_.c_str(), &device) < 0) return false;
    if (!S_ISCHR(device.st_mode)) {
      errno = ENODEV;
      return false;
    }
    const std::string proc = root_ + "/proc";
    Directory processes(opendir(proc.c_str()));
    if (!processes) return false;
    const int64_t deadline = posix::monotonicMs() + kOwnerScanBudgetMs;
    const std::string self = std::to_string(::getpid());
    for (;;) {
      errno = 0;
      const dirent* process = readdir(processes.get());
      if (!process) return errno == 0;
      if (!posix::numeric(process->d_name) || self == process->d_name) continue;
      if (posix::monotonicMs() > deadline) {
        errno = ETIMEDOUT;
        return false;
      }
      Directory fds(opendir((proc + "/" + process->d_name + "/fd").c_str()));
      if (!fds) {
        if (errno == ENOENT || errno == ESRCH) continue;
        return false;
      }
      for (;;) {
        errno = 0;
        const dirent* entry = readdir(fds.get());
        if (!entry) break;
        if (!posix::numeric(entry->d_name)) continue;
        struct stat held{};
        if (fstatat(dirfd(fds.get()), entry->d_name, &held, 0) < 0) continue;
        if (S_ISCHR(held.st_mode) && held.st_rdev == device.st_rdev) {
          errno = EBUSY;
          return false;
        }
      }
    }
  }

  bool readSpi(SpiSettings& out) override {
    posix::UniqueFd fd(::open(spi_.c_str(), O_RDWR | O_CLOEXEC));
    return fd.valid() && ::ioctl(fd.get(), SPI_IOC_RD_MODE, &out.mode) == 0 &&
           ::ioctl(fd.get(), SPI_IOC_RD_BITS_PER_WORD, &out.bits) == 0 &&
           ::ioctl(fd.get(), SPI_IOC_RD_LSB_FIRST, &out.lsbFirst) == 0 &&
           ::ioctl(fd.get(), SPI_IOC_RD_MAX_SPEED_HZ, &out.speedHz) == 0;
  }

  bool writeSpi(const SpiSettings& settings) override {
    posix::UniqueFd fd(::open(spi_.c_str(), O_RDWR | O_CLOEXEC));
    if (!fd.valid()) return false;
    bool okay = ::ioctl(fd.get(), SPI_IOC_WR_MODE, &settings.mode) == 0;
    okay = ::ioctl(fd.get(), SPI_IOC_WR_BITS_PER_WORD, &settings.bits) == 0 && okay;
    okay = ::ioctl(fd.get(), SPI_IOC_WR_LSB_FIRST, &settings.lsbFirst) == 0 && okay;
    okay = ::ioctl(fd.get(), SPI_IOC_WR_MAX_SPEED_HZ, &settings.speedHz) == 0 && okay;
    SpiSettings readback;
    return okay && readSpi(readback) && readback.mode == settings.mode && readback.bits == settings.bits &&
           readback.lsbFirst == settings.lsbFirst && readback.speedHz == settings.speedHz;
  }

 private:
  std::string root_, gpio_, spi_;
};

class NativeInput final : public InputBackend {
 public:
  explicit NativeInput(const std::string& root) : directory_(root + "/dev/input") {}

  bool discover(int& keys, int& knob) override {
    Directory directory(opendir(directory_.c_str()));
    if (!directory) return false;
    posix::UniqueFd foundKeys, foundKnob;
    while (const dirent* entry = readdir(directory.get())) {
      if (std::strncmp(entry->d_name, "event", 5) != 0 || !posix::numeric(entry->d_name + 5)) continue;
      posix::UniqueFd fd(::open((directory_ + "/" + entry->d_name).c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
      struct stat node{};
      if (!fd.valid() || ::fstat(fd.get(), &node) < 0 || !S_ISCHR(node.st_mode)) continue;
      const tc002::InputKind kind = tc002::inputKind(fd.get());
      if (kind == tc002::InputKind::Other) continue;
      posix::UniqueFd& slot = kind == tc002::InputKind::Keys ? foundKeys : foundKnob;
      if (slot.valid()) {
        errno = EEXIST;
        return false;
      }
      slot = std::move(fd);
    }
    if (!foundKeys.valid() || !foundKnob.valid()) {
      errno = ENODEV;
      return false;
    }
    keys = foundKeys.release();
    knob = foundKnob.release();
    return true;
  }

  bool grab(int fd, bool enable) override { return ::ioctl(fd, EVIOCGRAB, enable ? 1 : 0) == 0; }

  bool keysUp(int fd) override {
    std::array<unsigned char, (KEY_MAX + 8) / 8> held{};
    if (::ioctl(fd, EVIOCGKEY(held.size()), held.data()) < 0) return false;
    if (std::all_of(held.begin(), held.end(), [](unsigned char byte) { return byte == 0; })) return true;
    errno = EBUSY;
    return false;
  }

  bool drain(int fd, uint64_t& discarded) override {
    input_event events[32];
    for (unsigned batch = 0; batch < 64; ++batch) {
      const ssize_t count = ::read(fd, events, sizeof events);
      if (count < 0 && errno == EINTR) continue;
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
      if (count <= 0 || count % static_cast<ssize_t>(sizeof(input_event)) != 0) {
        errno = EIO;
        return false;
      }
      discarded += static_cast<uint64_t>(count) / sizeof(input_event);
    }
    errno = EOVERFLOW;
    return false;
  }

 private:
  std::string directory_;
};

}

std::unique_ptr<PanelBackend> nativePanelBackend(const std::string& root) {
  return std::make_unique<NativePanel>(root);
}

std::unique_ptr<InputBackend> nativeInputBackend(const std::string& root) {
  return std::make_unique<NativeInput>(root);
}

HardwareLease::HardwareLease(LeaseOptions options, std::unique_ptr<PanelBackend> panel,
                             std::unique_ptr<InputBackend> input)
    : options_(std::move(options)), panel_(std::move(panel)), input_(std::move(input)) {}

HardwareLease::~HardwareLease() { releaseInputs(); }

bool HardwareLease::boardMatches() {
  std::string model;
  if (!posix::readText(options_.root + "/proc/device-tree/model", model, 256)) {
    Log::line("lease", "cannot read board model: %s", std::strerror(errno));
    return false;
  }
  model = posix::trimmed(model);
  if (model != kBoardModel) {
    Log::line("lease", "unsupported board model \"%s\"", model.c_str());
    return false;
  }
  return true;
}

void HardwareLease::checkUsbRole(int64_t nowMs) {
  nextUsbCheck_ = nowMs + (nowMs - startedAt_ < options_.usbFastPhaseMs ? options_.usbCheckFastMs
                                                                         : options_.usbCheckSlowMs);
  if (otgWriter_ > 0) {
    nextUsbCheck_ = -1;
    return;
  }
  std::string raw;
  if (!posix::readText(options_.root + kOtgRolePath, raw, 64)) {
    if (otgFailures_++ % 20 == 0) Log::line("lease", "cannot read otg_role: %s", std::strerror(errno));
    return;
  }
  const std::string role = posix::trimmed(raw);
  if (role != otgRole_) Log::line("lease", "USB OTG role is %s", role.c_str());
  otgRole_ = role;
  if (role != "usb_host" && role != "usb_null") {
    if (role != kUsbDeviceRole && nowMs - startedAt_ < options_.usbUnknownPhaseMs)
      nextUsbCheck_ = nowMs + options_.usbUnknownPollMs;
    return;
  }
  if (otgWriteAt_ >= 0 && nowMs - otgWriteAt_ < options_.usbRetryMs) {
    nextUsbCheck_ = otgWriteAt_ + options_.usbRetryMs;
    return;
  }
  startUsbWrite(nowMs);
}

void HardwareLease::startUsbWrite(int64_t nowMs) {
  const std::string path = options_.root + kOtgRolePath;
  otgWriteAt_ = nowMs;
  const pid_t pid = forkTask(
      [&]() {
        const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC | O_NOCTTY);
        int error = fd < 0 ? errno : 0;
        errno = 0;
        constexpr ssize_t length = sizeof kUsbDeviceRole - 1;
        if (!error && ::write(fd, kUsbDeviceRole, length) != length) error = errno ? errno : EIO;
        return error;
      },
      SIGKILL);
  if (pid < 0) {
    Log::line("lease", "cannot start the usb_device write: %s", std::strerror(errno));
    nextUsbCheck_ = nowMs + options_.usbRetryMs;
    return;
  }
  otgWriter_ = pid;
  ++otgWrites_;
  nextUsbCheck_ = -1;
  Log::line("lease", "otg_role %s: writing usb_device", otgRole_.c_str());
}

bool HardwareLease::onChildExit(pid_t pid, int status, int64_t nowMs) {
  if (pid <= 0 || pid != otgWriter_) return false;
  otgWriter_ = -1;
  const int error = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  if (error)
    Log::line("lease", "cannot select usb_device: %s", error > 0 ? std::strerror(error) : "writer killed");
  else
    Log::line("lease", "usb_device written in %lld ms", static_cast<long long>(nowMs - otgWriteAt_));
  if (phase_ != Phase::Releasing && phase_ != Phase::Released) nextUsbCheck_ = nowMs + options_.usbRecheckMs;
  return true;
}

bool HardwareLease::ensureLatch(const char* when) {
  LatchState latch;
  if (!panel_->readLatch(latch)) {
    Log::line("lease", "cannot read panel latch (%s): %s", when, std::strerror(errno));
    return false;
  }
  if (!latch.exported) {
    if (!panel_->exportLatch() || !panel_->readLatch(latch) || !latch.exported) {
      Log::line("lease", "cannot export panel latch gpio35 (%s): %s", when, std::strerror(errno));
      return false;
    }
    Log::line("lease", "exported panel latch gpio35");
  }
  if (latch.idleHigh()) return true;
  if (!panel_->panelUnowned()) {
    Log::line("lease", "latch is direction=%s value=%s but the panel may be in use (%s); not touching it",
              latch.direction.c_str(), latch.value.c_str(), std::strerror(errno));
    return false;
  }
  LatchState after;
  if (!panel_->driveLatchHigh() || !panel_->readLatch(after) || !after.idleHigh()) {
    Log::line("lease", "cannot drive panel latch high (%s): %s", when, std::strerror(errno));
    return false;
  }
  ++latchRepairs_;
  Log::line("lease", "panel latch normalized from direction=%s value=%s (%s)", latch.direction.c_str(),
            latch.value.c_str(), when);
  return true;
}

bool HardwareLease::start(int64_t nowMs) {
  startedAt_ = nowMs;
  phase_ = Phase::Idle;
  if (!boardMatches()) return false;
  checkUsbRole(nowMs);
  if (!ensureLatch("start")) return false;
  if (!panel_->readSpi(savedSpi_)) {
    Log::line("lease", "cannot read SPI settings: %s", std::strerror(errno));
    return false;
  }
  spiSaved_ = true;
  Log::line("lease", "SPI settings saved: mode %u, %u bits, lsb %u, %u Hz", savedSpi_.mode, savedSpi_.bits,
            savedSpi_.lsbFirst, static_cast<unsigned>(savedSpi_.speedHz));
  if (!input_->discover(keys_, knob_)) {
    Log::line("lease", "TC002 input devices not found: %s", std::strerror(errno));
    return false;
  }
  drainBoth();
  phase_ = Phase::Quiet;
  quietUntil_ = nowMs + options_.quietMs;
  return true;
}

bool HardwareLease::drainBoth() {
  return keys_ >= 0 && knob_ >= 0 && input_->drain(keys_, discarded_) && input_->drain(knob_, discarded_);
}

void HardwareLease::attemptGrab(int64_t nowMs) {
  const uint64_t before = discarded_;
  if (!drainBoth()) {
    Log::line("lease", "cannot read input devices: %s", std::strerror(errno));
    quietUntil_ = nowMs + options_.grabRetryMs;
    return;
  }
  const bool up = input_->keysUp(keys_);
  if (discarded_ != before || !up) {
    if (!up && waitNotices_++ % 50 == 0) Log::line("lease", "waiting for all buttons to be released");
    quietUntil_ = nowMs + options_.quietMs;
    return;
  }
  if (!input_->grab(keys_, true)) {
    Log::line("lease", "cannot grab buttons: %s", std::strerror(errno));
    quietUntil_ = nowMs + options_.grabRetryMs;
    return;
  }
  keysGrabbed_ = true;
  if (!input_->grab(knob_, true)) {
    Log::line("lease", "cannot grab knob: %s", std::strerror(errno));
    input_->grab(keys_, false);
    keysGrabbed_ = false;
    quietUntil_ = nowMs + options_.grabRetryMs;
    return;
  }
  knobGrabbed_ = true;
  if (!input_->keysUp(keys_)) {
    input_->grab(knob_, false);
    input_->grab(keys_, false);
    keysGrabbed_ = knobGrabbed_ = false;
    quietUntil_ = nowMs + options_.quietMs;
    return;
  }
  phase_ = Phase::Held;
  quietUntil_ = -1;
  Log::line("lease", "inputs grabbed (keys fd %d, knob fd %d, %llu queued events discarded)", keys_, knob_,
            static_cast<unsigned long long>(discarded_));
}

bool HardwareLease::prepareChild(int64_t nowMs) {
  (void)nowMs;
  if (phase_ != Phase::Held) return false;
  if (!drainBoth()) {
    Log::line("lease", "cannot drain input devices: %s", std::strerror(errno));
    return false;
  }
  if (!input_->keysUp(keys_)) {
    if (waitNotices_++ % 50 == 0) Log::line("lease", "waiting for all buttons to be released");
    return false;
  }
  return ensureLatch("before runtime start");
}

void HardwareLease::releaseInputs() {
  if (keys_ < 0 && knob_ < 0) return;
  if (keysGrabbed_) input_->grab(keys_, false);
  if (knobGrabbed_) input_->grab(knob_, false);
  if (keys_ >= 0) ::close(keys_);
  if (knob_ >= 0) ::close(knob_);
  keys_ = knob_ = -1;
  keysGrabbed_ = knobGrabbed_ = false;
  Log::line("lease", "inputs released");
}

void HardwareLease::restorePanel() {
  if (!spiSaved_) return;
  if (!panel_->panelUnowned()) {
    Log::line("lease", "panel still in use (%s); SPI and latch left untouched", std::strerror(errno));
    return;
  }
  const bool spi = panel_->writeSpi(savedSpi_);
  LatchState latch;
  bool high = panel_->readLatch(latch) && latch.idleHigh();
  if (!high) high = panel_->driveLatchHigh() && panel_->readLatch(latch) && latch.idleHigh();
  spiSaved_ = !spi;
  Log::line("lease", "panel restore: SPI %s, latch %s", spi ? "restored" : "FAILED", high ? "high" : "FAILED");
}

int64_t HardwareLease::nextDeadlineMs() const {
  switch (phase_) {
    case Phase::Quiet:
      return nextUsbCheck_ >= 0 && nextUsbCheck_ < quietUntil_ ? nextUsbCheck_ : quietUntil_;
    case Phase::Releasing:
      return releaseDeadline_ >= 0 ? releaseDeadline_ : -1;
    case Phase::Released:
    case Phase::Idle:
      return -1;
    case Phase::Held:
      break;
  }
  return nextUsbCheck_;
}

void HardwareLease::onTime(int64_t nowMs) {
  if (phase_ == Phase::Releasing) {
    const bool drained = drainBoth();
    if (drained && !input_->keysUp(keys_) && nowMs < releaseTimeoutAt_) {
      releaseDeadline_ = nowMs + kReleasePollMs < releaseTimeoutAt_ ? nowMs + kReleasePollMs : releaseTimeoutAt_;
      return;
    }
    releaseInputs();
    restorePanel();
    phase_ = Phase::Released;
    releaseDeadline_ = -1;
    return;
  }
  if (nextUsbCheck_ >= 0 && nowMs >= nextUsbCheck_) checkUsbRole(nowMs);
  if (phase_ == Phase::Quiet && nowMs >= quietUntil_) attemptGrab(nowMs);
}

void HardwareLease::requestStop(int64_t nowMs) {
  if (phase_ == Phase::Released || phase_ == Phase::Releasing) return;
  nextUsbCheck_ = -1;
  quietUntil_ = -1;
  if (keys_ >= 0 || knob_ >= 0) {
    phase_ = Phase::Releasing;
    releaseTimeoutAt_ = nowMs + options_.releaseTimeoutMs;
    releaseDeadline_ = nowMs;
    return;
  }
  restorePanel();
  phase_ = Phase::Released;
}

void HardwareLease::appendStatus(api::JsonWriter& json) const {
  const char* phase = phase_ == Phase::Idle ? "idle" : phase_ == Phase::Quiet ? "acquiring" :
                      phase_ == Phase::Held ? "held" : phase_ == Phase::Releasing ? "releasing" : "released";
  json.key("lease").beginObject()
      .member("state", phase)
      .member("otgRole", otgRole_)
      .member("otgWrites", otgWrites_)
      .member("inputsGrabbed", keysGrabbed_ && knobGrabbed_)
      .member("discardedEvents", static_cast<unsigned long long>(discarded_))
      .member("latchRepairs", latchRepairs_)
      .member("spiSaved", spiSaved_);
  if (spiSaved_)
    json.key("spi").beginObject()
        .member("mode", static_cast<unsigned>(savedSpi_.mode))
        .member("bits", static_cast<unsigned>(savedSpi_.bits))
        .member("lsbFirst", static_cast<unsigned>(savedSpi_.lsbFirst))
        .member("speedHz", static_cast<unsigned>(savedSpi_.speedHz))
        .endObject();
  json.endObject();
}

}
}
