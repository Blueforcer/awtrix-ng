#include "platform/linux/ble/BleTypes.h"

#include <cstdio>

namespace awtrix::ble {
namespace {

// 0000xxxx-0000-1000-8000-00805f9b34fb in wire order, the base every SIG number sits in.
constexpr std::array<uint8_t, 16> kBase = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
                                           0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


}

bool Address::parse(std::string_view text, bool random, Address& out) {
  if (!posix::parseMac(text, out.b.data(), true)) return false;
  out.random = random;
  return true;
}

std::string Address::str() const {
  char buf[18];
  std::snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", b[5], b[4], b[3], b[2], b[1], b[0]);
  return buf;
}

Uuid Uuid::from16(uint16_t v) {
  Uuid u;
  u.b = kBase;
  u.b[12] = static_cast<uint8_t>(v & 0xff);
  u.b[13] = static_cast<uint8_t>(v >> 8);
  return u;
}

bool Uuid::parse(std::string_view text, Uuid& out) {
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text.remove_prefix(2);
  std::string digits;
  for (char c : text) {
    if (c == '-') continue;
    if (posix::hexValue(c) < 0) return false;
    digits += c;
  }
  if (digits.size() == 4 || digits.size() == 8) {
    uint32_t v = 0;
    for (char c : digits) v = v << 4 | static_cast<uint32_t>(posix::hexValue(c));
    out.b = kBase;
    for (int i = 0; i < 4; ++i) out.b[static_cast<std::size_t>(12 + i)] = static_cast<uint8_t>(v >> (8 * i));
    return true;
  }
  if (digits.size() != 32) return false;
  for (std::size_t i = 0; i < 16; ++i)
    out.b[15 - i] = static_cast<uint8_t>(posix::hexValue(digits[2 * i]) << 4 | posix::hexValue(digits[2 * i + 1]));
  return true;
}

bool Uuid::fromWire(const uint8_t* p, std::size_t n, Uuid& out) {
  if (n == 2) {
    out = from16(le16(p));
    return true;
  }
  if (n == 4) {
    out.b = kBase;
    for (std::size_t i = 0; i < 4; ++i) out.b[12 + i] = p[i];
    return true;
  }
  if (n != 16) return false;
  for (std::size_t i = 0; i < 16; ++i) out.b[i] = p[i];
  return true;
}

bool Uuid::isShort() const {
  for (std::size_t i = 0; i < 12; ++i)
    if (b[i] != kBase[i]) return false;
  return b[14] == 0 && b[15] == 0;
}

void Uuid::appendWire(Bytes& out) const {
  if (isShort()) put16(out, short16());
  else out.insert(out.end(), b.begin(), b.end());
}

std::string Uuid::str() const {
  char buf[40];
  if (isShort()) {
    std::snprintf(buf, sizeof buf, "%04x", short16());
    return buf;
  }
  std::snprintf(buf, sizeof buf, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[15],
                b[14], b[13], b[12], b[11], b[10], b[9], b[8], b[7], b[6], b[5], b[4], b[3], b[2], b[1], b[0]);
  return buf;
}


}
