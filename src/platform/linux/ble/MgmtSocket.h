#pragma once

#include <deque>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

// Linux management commands retain unrelated events for the radio loop.
class MgmtSocket {
 public:
  bool open();
  int fd() const { return fd_.get(); }
  int command(uint16_t op, uint16_t controller, const Bytes& params, Bytes* reply = nullptr);
  void write(const Bytes& packet) const;
  void readEvents();
  bool nextEvent(Bytes& event);
  bool hasEvents() const { return !events_.empty(); }

 private:
  posix::UniqueFd fd_;
  std::deque<Bytes> events_;
};

}  // namespace awtrix::ble
