#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace awtrix::tc002 {
// Owned, unprocessed PCM16 mono at 16 kHz. Position counts samples in this
// epoch; capturedMs is the MCU tick at the validating checkpoint (wraps at
// 2^32).
constexpr unsigned kStreamBlockSamples = 480;
// A consumer renews its stream at least every kStreamKeepaliveMs; a stream not renewed within
// kStreamLeaseMs is ended.
constexpr int64_t kStreamKeepaliveMs = 150, kStreamLeaseMs = 500;
struct StreamEvent {
  enum class Kind { Started, Audio, Ended, Support };
  int epoch = 0;
  Kind kind = Kind::Started;
  uint32_t firstSample = 0, capturedMs = 0;
  int64_t hostAtMs = 0;
  bool available = false;
  std::vector<int16_t> samples;
  std::string error;
};
struct StreamControl {
  enum Operation { Probe = 0, Start = 1, Stop = 2, Keepalive = 3 };
  int epoch = 0;
  Operation operation = Probe;
};
// Where a consumer sends its controls: in the runtime, a port of the microphone hub.
using MicrophonePort = std::function<bool(StreamControl)>;
std::string encodeStreamControl(const StreamControl& control);
std::string encodeStreamEvent(const StreamEvent& event);
bool decodeStreamControl(std::string_view json, StreamControl& control);
bool decodeStreamEvent(std::string_view json, StreamEvent& event);
}  // namespace awtrix::tc002
