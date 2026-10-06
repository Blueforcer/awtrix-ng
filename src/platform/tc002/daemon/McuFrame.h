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
constexpr uint8_t kAck = 0xfe;
constexpr std::size_t kMaxPayload = 32;
constexpr std::size_t kQueryBytes = 6;
constexpr std::size_t kPcmRequestBytes = 8;

struct Frame {
  uint8_t command = 0;
  uint8_t length = 0;
  uint8_t payload[kMaxPayload]{};
};

bool encodeQuery(uint8_t command, uint8_t (&out)[kQueryBytes]);
void encodePcmRequest(uint16_t halves, uint8_t (&out)[kPcmRequestBytes]);

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
