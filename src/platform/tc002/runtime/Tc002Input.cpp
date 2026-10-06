#include "platform/tc002/runtime/Tc002Input.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "platform/tc002/contract/RuntimeContract.h"
#include "platform/tc002/contract/InputIdentity.h"
#include "platform/tc002/contract/tc002_layout.h"

#if defined(__arm__)
static_assert(sizeof(input_event) == 16, "Stock ARM32 evdev records are 16 bytes");
#endif

namespace awtrix {
namespace {
bool bit(const unsigned char* bits, unsigned index) {
  return (bits[index / 8] & (1u << (index % 8))) != 0;
}

bool identity(int fd, bool knob) {
  struct stat info{};
  const int flags = fcntl(fd, F_GETFL);
  return flags >= 0 && (flags & O_NONBLOCK) && (flags & O_ACCMODE) == O_RDONLY &&
      fstat(fd, &info) == 0 && S_ISCHR(info.st_mode) && major(info.st_rdev) == 13 &&
      tc002::inputKind(fd) == (knob ? tc002::InputKind::Knob : tc002::InputKind::Keys);
}
}

bool Tc002InputDecoder::event(bool knob, uint16_t type, uint16_t code, int32_t value,
                              const Sink& sink) {
  if (type == EV_SYN) return code != SYN_DROPPED;
  if (knob) {
    if (type != EV_ABS || code != ABS_X) return false;
    const int turn = (pendingTurn_ == 8 && value == 1) ? 1 :
                     (pendingTurn_ == 13 && value == 11) ? -1 : 0;
    pendingTurn_ = value == 8 || value == 13 ? value : 0;
    if (!turn) return true;
    if (turn > 0) ++clockwise_;
    else ++counterclockwise_;
    if (sink.knob) sink.knob(turn > 0 ? Knob::Clockwise : Knob::Counterclockwise);
    return true;
  }
  if (type != EV_KEY || value < 0 || value > 2) return false;
  const int physical = code == KEY_DOWN ? 0 : code == KEY_LEFT ? 1 :
                       code == KEY_RIGHT ? 2 : code == KEY_UP ? 3 : -1;
  if (physical < 0) return false;
  if (value == 2) return true;
  const bool pressed = value == 1;
  bool& held = physical == 3 ? knobDown_ : keys_[physical];
  if (held == pressed) return true;
  held = pressed;
  if (pressed) ++presses_;
  if (physical == 3) {
    if (sink.knob) sink.knob(pressed ? Knob::Down : Knob::Up);
  } else {
    sink.button(physical, pressed);
  }
  return true;
}

Tc002Input::~Tc002Input() {
  if (keys_ >= 0) close(keys_);
  if (knob_ >= 0) close(knob_);
  if (records_) std::fprintf(stdout, "TC002 input records=%llu presses=%llu clockwise=%llu counterclockwise=%llu\n",
      static_cast<unsigned long long>(records_), static_cast<unsigned long long>(decoder_.presses()),
      static_cast<unsigned long long>(decoder_.clockwise()), static_cast<unsigned long long>(decoder_.counterclockwise()));
}

bool Tc002Input::begin() {
  if (keys_ >= 0 || knob_ >= 0) { error_ = "TC002 input already initialized"; return false; }
  if (!identity(tc002::kKeysFd, false) || !identity(tc002::kKnobFd, true)) {
    error_ = "TC002 input requires the guardian's validated descriptors " + std::to_string(tc002::kKeysFd) +
             " and " + std::to_string(tc002::kKnobFd);
    return false;
  }
  // The guardian retains these same open file descriptions, and owns their grabs through shutdown.
  keys_ = fcntl(tc002::kKeysFd, F_DUPFD_CLOEXEC, 3);
  knob_ = fcntl(tc002::kKnobFd, F_DUPFD_CLOEXEC, 3);
  if (keys_ < 0 || knob_ < 0) { error_ = "Cannot retain TC002 input descriptors"; return false; }
  close(tc002::kKeysFd);
  close(tc002::kKnobFd);
  unsigned char held[(KEY_MAX + 8) / 8]{};
  if (ioctl(keys_, EVIOCGKEY(sizeof held), held) < 0) { error_ = "Cannot read initial TC002 key state"; return false; }
  for (const unsigned code : {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT}) {
    if (bit(held, code)) { error_ = "Release all TC002 buttons before starting"; return false; }
  }
  std::puts("TC002 input ready: three top buttons, knob push and rotation");
  return true;
}

bool Tc002Input::poll(const Tc002InputDecoder::Sink& sink) {
  for (int source = 0; source < 2; ++source) {
    // Bound work and commands per render tick; remaining records stay in the evdev queue.
    for (int count = 0; count < 8; ++count) {
      input_event event{};
      const ssize_t length = read(source ? knob_ : keys_, &event, sizeof event);
      if (length < 0 && errno == EINTR) continue;
      if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      if (length != sizeof event) { error_ = "TC002 input read failed or device disconnected"; return false; }
      ++records_;
      if (!decoder_.event(source != 0, event.type, event.code, event.value, sink)) {
        error_ = "TC002 input event lost or unsupported; stopping runtime";
        return false;
      }
    }
  }
  return true;
}
}
