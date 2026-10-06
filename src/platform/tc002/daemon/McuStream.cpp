#include "platform/tc002/daemon/McuStream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace awtrix::tc002d::mcu {
namespace {
uint32_t word(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}
void put(uint8_t* p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(n >> (8 * i));
}
void checksum(uint8_t* p, std::size_t n) {
  unsigned sum = 0;
  for (std::size_t i = 0; i < n - 2; ++i) sum += p[i];
  p[n - 2] = sum >> 8;
  p[n - 1] = sum;
}
}  // namespace
std::array<uint8_t, 13> Stream::startFrame(int epoch) {
  std::array<uint8_t, 13> f{255, 85, 6, 7, 'S', '4', 1};
  put(f.data() + 7, static_cast<uint32_t>(epoch));
  checksum(f.data(), f.size());
  return f;
}
bool Stream::start(int epoch, int64_t now, Consumer consumer) {
  if (active_ || epoch <= 0 || !consumer) return false;
  *this = Stream{};
  active_ = true;
  epoch_ = epoch;
  consumer_ = std::move(consumer);
  leaseAt_ = now + 500;
  maximumAt_ = now + 60000;
  return true;
}
void Stream::renew(int epoch, int64_t now) {
  if (active_ && !stopping_ && epoch == epoch_ && now < leaseAt_)
    leaseAt_ = now + 500;
}
void Stream::stop(int64_t now) {
  if (!active_ || stopping_) return;
  stopping_ = true;
  stopAt_ = now + 1500;
}
void Stream::fail(const char* reason, int64_t now) {
  if (!active_) return;
  if (error_.empty()) error_ = reason;
  buffered_ = 0;
  buffer_.fill(0);
  stop(now);
}
void Stream::abort(const char* reason, int64_t now) {
  if (active_) {
    fail(reason, now);
    finish(lastTick_, now);
  }
}
int64_t Stream::deadline() const {
  return !active_ ? -1 : stopping_ ? stopAt_ : std::min(leaseAt_, maximumAt_);
}
void Stream::tick(int64_t now) {
  if (!active_) return;
  if (stopping_) {
    if (now >= stopAt_) abort("microphone stop timed out", now);
  } else if (now >= leaseAt_) {
    fail("microphone consumer expired", now);
  } else if (now >= maximumAt_) {
    fail("microphone duration limit", now);
  }
}
void Stream::emit(tc002::StreamEvent::Kind kind, uint32_t tick, int64_t now) {
  tc002::StreamEvent event;
  event.epoch = epoch_;
  event.kind = kind;
  event.capturedMs = tick;
  event.hostAtMs = now;
  event.firstSample = halves_ * 24 - static_cast<uint32_t>(buffered_);
  event.error = error_;
  if (kind == tc002::StreamEvent::Kind::Audio) {
    // The callback only borrows the event. Keep one output block for the next
    // checkpoint, with a local event providing the callback's stable view.
    event.samples.swap(eventSamples_);
    event.samples.reserve(tc002::kStreamBlockSamples);
    for (std::size_t offset = 0; offset < buffered_;) {
      const auto size =
          std::min<std::size_t>(tc002::kStreamBlockSamples, buffered_ - offset);
      event.samples.assign(buffer_.begin() + offset,
                           buffer_.begin() + offset + size);
      if (consumer_ && !consumer_(event)) {
        fail("microphone consumer overflow or disconnect", now);
        break;
      }
      offset += size;
      event.firstSample += static_cast<uint32_t>(size);
    }
    event.samples.clear();
    event.samples.swap(eventSamples_);
    buffered_ = 0;
    buffer_.fill(0);
  } else if (consumer_ && !consumer_(event))
    fail("microphone consumer overflow or disconnect", now);
}
void Stream::finish(uint32_t tick, int64_t now) {
  if (error_.empty() && buffered_)
    emit(tc002::StreamEvent::Kind::Audio, tick, now);
  emit(tc002::StreamEvent::Kind::Ended, tick, now);
  active_ = false;
  consumer_ = {};
  std::vector<int16_t>().swap(eventSamples_);
  buffered_ = 0;
  buffer_.fill(0);
}
void Stream::receive(const Frame& frame, int64_t now, const Write& write) {
  if (!active_ || frame.command != kPcm) return;
  powerValid_ = false;
  const auto* p = frame.payload;
  if (frame.length != 16) {
    fail("invalid microphone frame length", now);
    return;
  }
  if (!std::memcmp(p, "S3", 2)) {
    const auto count = word(p + 8), tick = word(p + 12);
    if (word(p + 4) != static_cast<uint32_t>(epoch_) || p[2] < 1 || p[2] > 3) {
      fail("invalid microphone metadata identity", now);
      return;
    }
    if (p[2] == 1) {
      if (started_ || count || p[3]) {
        fail("invalid microphone start", now);
        return;
      }
      started_ = true;
      startTick_ = lastTick_ = tick;
      if (error_.empty()) emit(tc002::StreamEvent::Kind::Started, tick, now);
      return;
    }
    if (!started_ || part_ || count != halves_)
      fail("incomplete microphone samples", now);
    const uint32_t elapsed = tick - lastTick_, total = tick - startTick_;
    if (count < anchor_ || elapsed >= 0x80000000u ||
        std::abs(double(elapsed) - double(count - anchor_) * 1.5) > 3 ||
        std::abs(double(total) - double(count) * 1.5) >
            3 + double(count) * 1.5 * .005)
      fail("microphone sample/time discontinuity", now);
    if (p[3]) fail("MCU microphone acquisition error", now);
    if (p[2] == 3 && count <= anchor_)
      fail("microphone checkpoint did not advance", now);
    lastTick_ = tick;
    anchor_ = count;
    if (p[2] == 2) {
      finish(tick, now);
      return;
    }
    if (error_.empty() && buffered_)
      emit(tc002::StreamEvent::Kind::Audio, tick, now);
    return;
  }
  if (!std::memcmp(p, "W4", 2) || !std::memcmp(p, "A4", 2) ||
      !std::memcmp(p, "N4", 2)) {
    const uint16_t token = static_cast<uint16_t>(p[2] | (p[3] << 8));
    if (!started_ || word(p + 4) != static_cast<uint32_t>(epoch_)) {
      fail("invalid microphone control epoch", now);
      return;
    }
    if (p[0] == 'W') {
      if (window_ || token != static_cast<uint16_t>(token_ + 1) ||
          word(p + 8) != halves_ || part_) {
        fail("invalid microphone window sequence", now);
        return;
      }
      token_ = token;
      window_ = true;
      // A stopped or failed consumer ends acquisition; error drains send no control.
      tick(now);
      if (!error_.empty()) return;
      uint8_t reply[15]{
          255, 85, 6, 9, 'C', '4', static_cast<uint8_t>(stopping_ ? 2 : 3)};
      put(reply + 7, static_cast<uint32_t>(epoch_));
      reply[11] = token;
      reply[12] = token >> 8;
      checksum(reply, sizeof reply);
      if (!write(reply, sizeof reply))
        fail("microphone control write failed", now);
    } else {
      if (!window_ || token != token_ || word(p + 12) != halves_) {
        fail("unmatched microphone window closure", now);
        return;
      }
      window_ = false;
      if (p[0] == 'A')
        ++accepted_;
      else
        ++missed_;
      usb_ = p[8];
      batteryFirst_ = p[9];
      batteryWord_ = static_cast<uint16_t>((p[10] << 8) | p[11]);
      powerValid_ = true;
    }
    return;
  }
  if (std::memcmp(p, "D3", 2)) {
    fail("unknown microphone payload", now);
    return;
  }
  if (!error_.empty()) return;
  if (!started_ || word(p + 2) != halves_ || p[6] != part_ || p[7] > 1 ||
      buffered_ + 4 > buffer_.size()) {
    fail("microphone sample sequence or buffer limit", now);
    return;
  }
  if (!part_) {
    physical_ = p[7];
    if (physical_ == previous_) {
      fail("microphone DMA half repeated", now);
      return;
    }
  } else if (p[7] != physical_) {
    fail("microphone DMA half changed", now);
    return;
  }
  for (unsigned i = 0; i < 4; ++i) {
    const int value = p[8 + 2 * i] | (p[9 + 2 * i] << 8);
    buffer_[buffered_++] =
        static_cast<int16_t>(value < 32768 ? value : value - 65536);
  }
  if (++part_ == 6) {
    part_ = 0;
    ++halves_;
    previous_ = physical_;
  }
}
}  // namespace awtrix::tc002d::mcu
