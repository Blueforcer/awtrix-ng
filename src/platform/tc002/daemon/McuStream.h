#pragma once
#include <array>
#include <functional>

#include "platform/tc002/contract/MicrophoneStream.h"
#include "platform/tc002/daemon/McuFrame.h"

namespace awtrix::tc002d::mcu {
// One S4 transaction on the service thread. The consumer must never block.
// An error revokes delivery immediately, but retains UART ownership until end
// or the independent MCU lease plus drain deadline. No network work lives here.
class Stream {
 public:
  using Consumer = std::function<bool(const tc002::StreamEvent&)>;
  using Write = std::function<bool(const uint8_t*, std::size_t)>;
  bool start(int epoch, int64_t now, Consumer consumer);
  static std::array<uint8_t, 13> startFrame(int epoch);
  void receive(const Frame& frame, int64_t now, const Write& write);
  void renew(int epoch, int64_t now);
  void stop(int64_t now);
  void fail(const char* reason, int64_t now);
  void abort(const char* reason, int64_t now);
  void tick(int64_t now);
  bool active() const { return active_; }
  int epoch() const { return epoch_; }
  int64_t deadline() const;
  uint64_t accepted() const { return accepted_; }
  uint64_t missed() const { return missed_; }
  uint32_t samples() const { return halves_ * 24; }
  const std::string& error() const { return error_; }
  bool powerValid() const { return powerValid_; }
  uint8_t usb() const { return usb_; }
  uint8_t batteryFirst() const { return batteryFirst_; }
  uint16_t batteryWord() const { return batteryWord_; }

 private:
  void emit(tc002::StreamEvent::Kind kind, uint32_t tick, int64_t now);
  void finish(uint32_t tick, int64_t now);
  Consumer consumer_;
  bool active_ = false, started_ = false, stopping_ = false, window_ = false,
       powerValid_ = false;
  int epoch_ = 0;
  int64_t leaseAt_ = 0, stopAt_ = -1, maximumAt_ = 0;
  uint32_t halves_ = 0, anchor_ = 0, startTick_ = 0, lastTick_ = 0;
  unsigned part_ = 0, physical_ = 2, previous_ = 2;
  uint16_t token_ = 0;
  uint64_t accepted_ = 0, missed_ = 0;
  uint8_t usb_ = 0, batteryFirst_ = 0;
  uint16_t batteryWord_ = 0;
  // Four output blocks allow a checkpoint delayed by the final ADC ring drain.
  std::array<int16_t, tc002::kStreamBlockSamples * 4> buffer_{};
  std::size_t buffered_ = 0;
  std::vector<int16_t> eventSamples_;
  std::string error_;
};
}  // namespace awtrix::tc002d::mcu
