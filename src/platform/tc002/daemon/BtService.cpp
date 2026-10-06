#include "platform/posix/Bytes.h"
#include "platform/tc002/daemon/BtService.h"

#include <asm/ioctls.h>
#include <asm/termbits.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "platform/posix/Files.h"
#include "platform/tc002/daemon/Log.h"

namespace awtrix {
namespace tc002d {
namespace {

constexpr int kLineDisciplineHci = 15;
constexpr int kLineDisciplineTty = 0;
constexpr unsigned long kHciUartSetProto = _IOW('U', 200, int);
constexpr unsigned long kHciUartSetFlags = _IOW('U', 203, int);
constexpr int kProtoH4 = 0;
constexpr unsigned kBaud = 1500000;

// Configuration block of vendor command 0xfc4d, as stock hciattach sends it.
constexpr uint8_t kConfig[] = {
    0x00, 0xd7, 0x18, 0x00, 0x00, 0xf7, 0x18, 0x00, 0x40, 0x00, 0x00, 0x00, 0x28, 0x00, 0x90, 0x01, 0x90, 0x01,
    0x03, 0x00, 0x02, 0x00, 0x03, 0x00, 0x02, 0x00, 0x28, 0x00, 0x00, 0x02, 0x14, 0x00, 0x15, 0x00, 0x14, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4e,
    0x00, 0x00, 0x13, 0x01, 0x00, 0x00, 0x02, 0x73, 0x06, 0x20, 0x07, 0x00, 0x40, 0x00, 0x48, 0x00, 0x47, 0x00,
    0x20, 0x00, 0x00, 0x02, 0xa4, 0x01, 0x64, 0x00, 0x64, 0x00, 0x08, 0x00, 0x18, 0x00, 0x28, 0x00, 0x8c, 0x00,
    0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x20, 0x4e, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00};
static_assert(sizeof kConfig == 104, "the stock configuration block is 104 bytes");

class LinuxBtSystem : public BtSystem {
 public:
  int open(const std::string& path) override { return ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC); }
  bool configure(int fd) override {
    termios2 t{};
    if (::ioctl(fd, TCGETS2, &t) < 0) return false;
    t.c_iflag = 0;
    t.c_oflag = 0;
    t.c_lflag = 0;
    t.c_line = 0;
    t.c_cflag = B1500000 | CS8 | CREAD | CLOCAL | CRTSCTS;
    t.c_ispeed = kBaud;
    t.c_ospeed = kBaud;
    if (::ioctl(fd, TCSETS2, &t) < 0) return false;
    return ::ioctl(fd, TCFLSH, TCIOFLUSH) == 0;
  }
  bool attach(int fd) override {
    int discipline = kLineDisciplineHci;
    if (::ioctl(fd, TIOCSETD, &discipline) < 0) return false;
    ::ioctl(fd, kHciUartSetFlags, 0);
    return ::ioctl(fd, kHciUartSetProto, kProtoH4) == 0;
  }
  void detach(int fd) override {
    int discipline = kLineDisciplineTty;
    ::ioctl(fd, TIOCSETD, &discipline);
  }
  bool readText(const std::string& path, std::string& out) override { return posix::readText(path, out, 64); }
};

}

BtSystem& linuxBtSystem() {
  static LinuxBtSystem system;
  return system;
}

BtService::BtService(BtOptions options, BtSystem& system, Status status)
    : options_(std::move(options)), system_(system), status_(std::move(status)) {}

bool BtService::parseMac(const std::string& text, uint8_t address[6]) {
  return posix::parseMac(text, address, true);
}

std::vector<BtService::Command> BtService::setup(const uint8_t address[6]) {
  return {
      {0x0c03, {}},
      {0xfc70, std::vector<uint8_t>(address, address + 6)},
      {0xfc4d, std::vector<uint8_t>(kConfig, kConfig + sizeof kConfig)},
      {0xfc50, {0x02}},
      {0xfc51, {0x01}},
      {0xfc52, {0x01}},
  };
}

bool BtService::start(int64_t nowMs) {
  (void)nowMs;
  return true;
}

void BtService::request(bool on) {
  wanted_ = on;
  pending_ = true;
}

int64_t BtService::nextDeadlineMs() const {
  if (pending_) return 0;
  return state_ == State::Starting ? deadline_ : -1;
}

void BtService::pollInterest(std::vector<PollInterest>& out) const {
  if (state_ == State::Starting && line_.valid()) out.push_back({line_.get(), POLLIN});
}

void BtService::report(bool on, const std::string& error) {
  if (status_) status_(tc002::BluetoothStatus{on, error.substr(0, tc002::kMaxBluetoothError)});
}

void BtService::onTime(int64_t nowMs) {
  if (pending_) {
    pending_ = false;
    if (wanted_ && state_ == State::Off) begin(nowMs);
    else if (!wanted_ && state_ != State::Off) release("released");
    else if (state_ != State::Starting) report(state_ == State::On, "");
    return;
  }
  if (state_ == State::Starting && deadline_ >= 0 && nowMs >= deadline_) {
    if (driverDeadline_ >= 0) {
      begin(nowMs);
      return;
    }
    if (++attempt_ < options_.attempts) {
      Log::line("bluetooth", "no answer to %04x, sending it again", commands_[step_].opcode);
      sendCurrent(nowMs);
      return;
    }
    char why[64];
    std::snprintf(why, sizeof why, "no answer to %04x from %s", commands_[step_].opcode, options_.path.c_str());
    fail(why, nowMs);
  }
}

void BtService::begin(int64_t nowMs) {
  std::string mac;
  uint8_t address[6];
  if (!system_.readText(options_.macPath, mac) || !parseMac(posix::trimmed(mac), address)) {
    if (driverDeadline_ < 0) driverDeadline_ = nowMs + options_.driverWaitMs;
    if (nowMs >= driverDeadline_) {
      fail("the Wi-Fi driver is not loaded", nowMs);
      return;
    }
    if (state_ != State::Starting) Log::line("bluetooth", "waiting for the Wi-Fi driver");
    state_ = State::Starting;
    deadline_ = std::min(nowMs + options_.driverPollMs, driverDeadline_);
    return;
  }
  driverDeadline_ = -1;
  line_.reset(system_.open(options_.path));
  if (!line_.valid()) {
    fail(std::string("cannot open ") + options_.path + ": " + std::strerror(errno), nowMs);
    return;
  }
  if (!system_.configure(line_.get())) {
    fail(std::string("cannot configure ") + options_.path + ": " + std::strerror(errno), nowMs);
    return;
  }
  commands_ = setup(address);
  step_ = 0;
  attempt_ = 0;
  input_.clear();
  state_ = State::Starting;
  Log::line("bluetooth", "attaching the controller on %s as %s", options_.path.c_str(), posix::trimmed(mac).c_str());
  sendCurrent(nowMs);
}

void BtService::sendCurrent(int64_t nowMs) {
  const Command& c = commands_[step_];
  std::vector<uint8_t> packet{0x01, static_cast<uint8_t>(c.opcode & 0xff), static_cast<uint8_t>(c.opcode >> 8),
                              static_cast<uint8_t>(c.params.size())};
  packet.insert(packet.end(), c.params.begin(), c.params.end());
  deadline_ = nowMs + options_.replyTimeoutMs;
  if (!posix::writeAll(line_.get(), packet.data(), packet.size()))
    fail(std::string("cannot write to ") + options_.path + ": " + std::strerror(errno), nowMs);
}

void BtService::onReady(int fd, short revents, int64_t nowMs) {
  if (state_ != State::Starting || fd != line_.get()) return;
  if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
    fail("the UART closed", nowMs);
    return;
  }
  uint8_t buf[256];
  const ssize_t n = ::read(fd, buf, sizeof buf);
  if (n <= 0) {
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
    fail("the UART closed", nowMs);
    return;
  }
  input_.insert(input_.end(), buf, buf + n);
  for (;;) {
    while (!input_.empty() && input_[0] != 0x04) input_.erase(input_.begin());
    if (input_.size() < 3 || input_.size() < 3u + input_[2]) return;
    const std::vector<uint8_t> event(input_.begin(), input_.begin() + 3 + input_[2]);
    input_.erase(input_.begin(), input_.begin() + static_cast<long>(event.size()));
    if (event[1] != 0x0e || event.size() < 7) continue;
    const uint16_t opcode = static_cast<uint16_t>(event[4] | event[5] << 8);
    if (opcode != commands_[step_].opcode) continue;
    if (event[6] != 0) {
      char why[64];
      std::snprintf(why, sizeof why, "the controller refused %04x (status %u)", opcode, event[6]);
      fail(why, nowMs);
      return;
    }
    if (++step_ < commands_.size()) {
      attempt_ = 0;
      sendCurrent(nowMs);
      continue;
    }
    if (!system_.attach(line_.get())) {
      fail(std::string("cannot hand the UART to the HCI driver: ") + std::strerror(errno), nowMs);
      return;
    }
    state_ = State::On;
    deadline_ = -1;
    ++attaches_;
    lastError_.clear();
    Log::line("bluetooth", "controller attached");
    report(true, "");
    return;
  }
}

void BtService::fail(const std::string& why, int64_t nowMs) {
  (void)nowMs;
  lastError_ = why;
  Log::line("bluetooth", "%s", why.c_str());
  line_.reset();
  state_ = State::Off;
  deadline_ = -1;
  driverDeadline_ = -1;
  wanted_ = false;
  report(false, why);
}

void BtService::release(const char* why) {
  if (state_ == State::On) system_.detach(line_.get());
  line_.reset();
  if (state_ != State::Off) Log::line("bluetooth", "controller %s", why);
  state_ = State::Off;
  deadline_ = -1;
  driverDeadline_ = -1;
  report(false, "");
}

void BtService::requestStop(int64_t nowMs) {
  (void)nowMs;
  wanted_ = false;
  pending_ = false;
  release("released for shutdown");
}

void BtService::appendStatus(api::JsonWriter& json) const {
  static const char* const kNames[] = {"off", "starting", "on"};
  json.key("bluetooth").beginObject().key("state").value(kNames[static_cast<int>(state_)]).key("attaches")
      .value(attaches_).key("error").value(lastError_).endObject();
}

}
}
