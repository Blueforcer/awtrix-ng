#pragma once

#include <cstddef>
#include <cstdint>

// TC002 MCU wire format on /dev/ttyS1: ff 55 <command> <length> <payload...> <sum hi> <sum lo>,
// where the 16-bit sum covers every byte before it. Encodes queries and bounded PCM
// acquisition. Upgrade frames belong to McuUpgrade. Commands may receive an empty 0xfe ACK.
namespace awtrix {
namespace tc002d {
namespace mcu {

constexpr uint8_t kMicrophone = 0x01;
constexpr uint8_t kUsb = 0x02;
constexpr uint8_t kBattery = 0x03;
constexpr uint8_t kPcm = 0x06;
constexpr uint8_t kIdentity = 0x08;
constexpr uint8_t kVersion = 0x11;
constexpr uint8_t kLedGain = 0x12;
constexpr uint8_t kLedRegister = 0x16;
constexpr uint8_t kAck = 0xfe;
constexpr std::size_t kMaxPayload = 32;
constexpr std::size_t kQueryBytes = 6;
constexpr std::size_t kPcmRequestBytes = 8;
constexpr std::size_t kLedGainPayloadBytes = 6;
constexpr std::size_t kLedGainBytes = 4 + kLedGainPayloadBytes + 2;

struct Frame {
  uint8_t command = 0;
  uint8_t length = 0;
  uint8_t payload[kMaxPayload]{};
};

bool encodeQuery(uint8_t command, uint8_t (&out)[kQueryBytes]);
void encodePcmRequest(uint16_t halves, uint8_t (&out)[kPcmRequestBytes]);

// Sets the LED driver's per-channel current-gain register (0x16) on all three channels, the
// command the stock app uses. The value is 0..63; the vendor default is 30 and lower is dimmer.
// Nothing else in the firmware writes it, so a panel that was dimmed by the stock app comes back
// at the default after an MCU update.
void encodeLedGain(uint8_t value, uint8_t (&out)[kLedGainBytes]);

// Resynchronising stream parser: bytes that cannot start a valid frame are skipped one at a
// time, so garbage, truncated frames and checksum errors never stall later frames.
class FrameParser {
 public:
  static constexpr std::size_t kCapacity = 256;

  void push(const uint8_t* bytes, std::size_t length);
  bool next(Frame& out);
  void clear() { length_ = 0; }

  uint64_t discardedBytes() const { return discarded_; }
  uint64_t corruptFrames() const { return corrupt_; }

 private:
  void consume(std::size_t count);

  uint8_t bytes_[kCapacity]{};
  std::size_t length_ = 0;
  uint64_t discarded_ = 0;
  uint64_t corrupt_ = 0;
};

}
}
}
