#include "platform/tc002/daemon/McuPcm.h"

#include <cstring>

namespace awtrix::tc002d::mcu {
namespace {
unsigned word(const uint8_t* p) { return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8); }
}

bool PcmCapture::start(unsigned halves, int64_t nowMs) {
  if (active_ || !halves || halves > kMaxHalves) return false;
  active_ = true;
  started_ = ended_ = false;
  requested_ = halves;
  halves_ = part_ = 0;
  physicalHalf_ = previousHalf_ = 2;
  deadline_ = nowMs + kTimeoutMs;
  error_.clear();
  samples_.clear();
  samples_.reserve(halves * kSamplesPerHalf);
  return true;
}

void PcmCapture::fail(const char* reason) {
  if (error_.empty()) error_ = reason;
  samples_.clear();
}

void PcmCapture::corrupt() {
  if (active_) fail("PCM UART frame lost or corrupt");
}

void PcmCapture::abort(const char* reason) {
  if (!active_) return;
  fail(reason);
  active_ = false;
}

void PcmCapture::tick(int64_t nowMs) {
  if (active_ && nowMs >= deadline_) abort("PCM capture timed out");
}

void PcmCapture::receive(const Frame& frame) {
  if (!active_ || frame.command != kPcm) return;
  const auto* p = frame.payload;
  if (frame.length != 16) { fail("invalid PCM frame length"); return; }
  if (!std::memcmp(p, "PCM2", 4)) {
    if (p[4] == 1) {
      if (started_) { fail("duplicate PCM start"); return; }
      started_ = true;
      for (unsigned i = 5; i < 16; ++i)
        if (p[i]) { fail("invalid PCM start metadata"); return; }
    } else if (p[4] == 2) {
      if (!started_) fail("PCM end before start");
      if (p[5]) fail("MCU reported PCM acquisition error");
      if (part_ || word(p + 6) != halves_ || halves_ != requested_) fail("incomplete PCM capture");
      ended_ = true;
      active_ = false;
    } else fail("invalid PCM metadata type");
    return;
  }
  if (!error_.empty()) return;
  if (!started_) { fail("PCM data before start"); return; }
  if (std::memcmp(p, "P2", 2) || word(p + 2) != halves_ || p[4] != part_ || p[5] > 1 || p[6] || p[7] ||
      halves_ >= requested_) { fail("invalid PCM sequence or part"); return; }
  if (part_ == 0) {
    physicalHalf_ = p[5];
    if (physicalHalf_ == previousHalf_) { fail("PCM DMA half did not alternate"); return; }
  } else if (p[5] != physicalHalf_) { fail("PCM DMA half changed within a frame"); return; }
  for (unsigned i = 0; i < 4; ++i) {
    const unsigned value = word(p + 8 + i * 2);
    pending_[part_ * 4 + i] = static_cast<int16_t>(value < 32768 ? static_cast<int>(value) : static_cast<int>(value) - 65536);
  }
  if (++part_ == 6) {
    samples_.insert(samples_.end(), pending_, pending_ + kSamplesPerHalf);
    ++halves_;
    part_ = 0;
    previousHalf_ = physicalHalf_;
  }
}

}
