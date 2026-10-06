#include "platform/posix/Bytes.h"
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/posix/Files.h"

namespace awtrix::ble {

using Bytes = std::vector<uint8_t>;

// One answer or event for whoever holds the resource `id` under the owner `script`: a script, or
// the firmware's own gamepad. done means nothing more follows for that id.
struct BleEvent {
  std::string script;
  uint32_t id = 0;
  bool done = false;
  std::string json;
};

// Six bytes in wire order (least significant first), shown most significant first.
struct Address {
  std::array<uint8_t, 6> b{};
  bool random = false;

  static bool parse(std::string_view text, bool random, Address& out);
  std::string str() const;
  bool operator==(const Address& o) const { return b == o.b && random == o.random; }
  bool operator!=(const Address& o) const { return !(*this == o); }
  bool operator<(const Address& o) const { return b != o.b ? b < o.b : random < o.random; }
};

// Stored as the full 128 bits in wire order. Bluetooth SIG numbers print and encode short.
struct Uuid {
  std::array<uint8_t, 16> b{};

  static Uuid from16(uint16_t v);
  static bool parse(std::string_view text, Uuid& out);
  static bool fromWire(const uint8_t* p, std::size_t n, Uuid& out);
  bool isShort() const;
  uint16_t short16() const { return static_cast<uint16_t>(b[12] | b[13] << 8); }
  void appendWire(Bytes& out) const;
  std::size_t wireSize() const { return isShort() ? 2 : 16; }
  std::string str() const;
  bool operator==(const Uuid& o) const { return b == o.b; }
  bool operator!=(const Uuid& o) const { return b != o.b; }
  bool operator<(const Uuid& o) const { return b < o.b; }
};

inline std::string toHex(const Bytes& v) { return posix::hexBytes(v.data(), v.size()); }
using posix::fromHex;
using posix::le16;
using posix::put16;

}
