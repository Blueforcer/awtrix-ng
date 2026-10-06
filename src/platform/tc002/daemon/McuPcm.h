#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "platform/tc002/daemon/McuFrame.h"

namespace awtrix::tc002d::mcu {

// One bounded PCM2 transaction. Samples become usable only after a matching,
// successful end frame. An error discards audio but drains through the end frame.
class PcmCapture {
 public:
  static constexpr unsigned kSampleRate = 16000;
  static constexpr unsigned kSamplesPerHalf = 24;
  static constexpr unsigned kMaxHalves = 1536; // 2.304 s, below the MCU's 3 s limit.
  static constexpr int64_t kTimeoutMs = 5000;

  bool start(unsigned halves, int64_t nowMs);
  void receive(const Frame& frame);
  void corrupt();
  void tick(int64_t nowMs);
  void abort(const char* reason);
  bool active() const { return active_; }
  bool succeeded() const { return ended_ && error_.empty(); }
  int64_t deadline() const { return active_ ? deadline_ : -1; }
  unsigned halves() const { return halves_; }
  unsigned requested() const { return requested_; }
  const std::vector<int16_t>& samples() const { return samples_; }
  const std::string& error() const { return error_; }

 private:
  void fail(const char* reason);
  bool active_ = false, started_ = false, ended_ = false;
  unsigned requested_ = 0, halves_ = 0, part_ = 0, physicalHalf_ = 2, previousHalf_ = 2;
  int64_t deadline_ = -1;
  int16_t pending_[kSamplesPerHalf]{};
  std::vector<int16_t> samples_;
  std::string error_;
};

}
