#include "core/mirror/MirrorWire.h"

#include <algorithm>
#include <cstring>

namespace awtrix {
namespace mirror {
namespace wire {

namespace {
constexpr uint8_t kMagic[4] = {'A', 'W', 'M', 'R'};
constexpr std::size_t kControlBytes = 6;
constexpr std::size_t kSizedBytes = 10;
constexpr std::size_t kFrameFixedBytes = 19;
constexpr uint8_t kKindApp = 0;
constexpr uint8_t kKindNotification = 1;

void put16(uint8_t* at, uint16_t value) {
  at[0] = static_cast<uint8_t>(value & 0xFF);
  at[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t get16(const uint8_t* at) {
  return static_cast<uint16_t>(at[0] | (at[1] << 8));
}

std::size_t clipped(std::string_view name) { return std::min(name.size(), kMaxName); }

std::size_t frameHeaderBytes(const FrameHeader& header) {
  return kFrameFixedBytes + clipped(header.app) + clipped(header.incoming);
}

std::size_t putHeader(Type type, uint8_t* out) {
  std::memcpy(out, kMagic, sizeof(kMagic));
  out[4] = kVersion;
  out[5] = static_cast<uint8_t>(type);
  return kControlBytes;
}

uint8_t* putName(uint8_t* at, std::string_view name) {
  const std::size_t length = clipped(name);
  *at++ = static_cast<uint8_t>(length);
  std::memcpy(at, name.data(), length);
  return at + length;
}

bool takeName(const uint8_t*& at, const uint8_t* end, std::string_view& name) {
  if (at >= end) return false;
  const std::size_t length = *at++;
  if (static_cast<std::size_t>(end - at) < length) return false;
  name = std::string_view(reinterpret_cast<const char*>(at), length);
  at += length;
  return true;
}
}

bool decode(const uint8_t* data, std::size_t length, Packet& out) {
  if (!data || length < kControlBytes || std::memcmp(data, kMagic, sizeof(kMagic)) != 0 ||
      data[4] != kVersion)
    return false;
  out = Packet{};
  switch (static_cast<Type>(data[5])) {
    case Type::Leave:
      out.type = Type::Leave;
      return length == kControlBytes;
    case Type::Subscribe:
    case Type::Idle:
      if (length != kSizedBytes) return false;
      out.type = static_cast<Type>(data[5]);
      out.width = get16(data + 6);
      out.height = get16(data + 8);
      return out.width > 0 && out.height > 0;
    case Type::Frame:
      break;
    default:
      return false;
  }
  if (length < kFrameFixedBytes) return false;
  out.type = Type::Frame;
  out.width = get16(data + 6);
  out.height = get16(data + 8);
  out.frame = get16(data + 10);
  out.firstRow = get16(data + 12);
  out.rows = get16(data + 14);
  if (data[16] == kKindApp) out.kind = PageKind::App;
  else if (data[16] == kKindNotification) out.kind = PageKind::Notification;
  else return false;
  const uint8_t* at = data + 17;
  const uint8_t* end = data + length;
  if (!takeName(at, end, out.app) || !takeName(at, end, out.incoming)) return false;
  if (out.width == 0 || out.height == 0 || out.rows == 0 ||
      out.firstRow >= out.height || out.rows > out.height - out.firstRow)
    return false;
  const std::size_t pixels = static_cast<std::size_t>(out.width) * out.rows * 3u;
  if (static_cast<std::size_t>(end - at) != pixels) return false;
  out.rgb = at;
  return true;
}

namespace {
std::size_t encodeSized(Type type, uint16_t width, uint16_t height, uint8_t* out,
                        std::size_t capacity) {
  if (capacity < kSizedBytes) return 0;
  putHeader(type, out);
  put16(out + 6, width);
  put16(out + 8, height);
  return kSizedBytes;
}
}

std::size_t encodeSubscribe(uint16_t width, uint16_t height, uint8_t* out, std::size_t capacity) {
  return encodeSized(Type::Subscribe, width, height, out, capacity);
}

std::size_t encodeLeave(uint8_t* out, std::size_t capacity) {
  return capacity < kControlBytes ? 0 : putHeader(Type::Leave, out);
}

std::size_t encodeIdle(uint16_t width, uint16_t height, uint8_t* out, std::size_t capacity) {
  return encodeSized(Type::Idle, width, height, out, capacity);
}

int rowsPerDatagram(int width, const FrameHeader& header) {
  if (width <= 0) return 0;
  const std::size_t head = frameHeaderBytes(header);
  if (head >= kMaxDatagram) return 0;
  return static_cast<int>((kMaxDatagram - head) / (static_cast<std::size_t>(width) * 3u));
}

std::size_t encodeFrame(const Canvas& canvas, const FrameHeader& header, int firstRow, int rows,
                        uint8_t* out, std::size_t capacity) {
  const int width = canvas.width();
  if (rows <= 0 || firstRow < 0 || firstRow + rows > canvas.height() || width <= 0) return 0;
  const std::size_t total =
      frameHeaderBytes(header) + static_cast<std::size_t>(width) * rows * 3u;
  if (total > capacity) return 0;
  putHeader(Type::Frame, out);
  put16(out + 6, static_cast<uint16_t>(width));
  put16(out + 8, static_cast<uint16_t>(canvas.height()));
  put16(out + 10, header.frame);
  put16(out + 12, static_cast<uint16_t>(firstRow));
  put16(out + 14, static_cast<uint16_t>(rows));
  out[16] = header.kind == PageKind::Notification ? kKindNotification : kKindApp;
  uint8_t* at = putName(out + 17, header.app);
  at = putName(at, header.incoming);
  const uint32_t* pixel = canvas.data() + static_cast<std::size_t>(firstRow) * width;
  for (std::size_t i = 0, n = static_cast<std::size_t>(width) * rows; i < n; ++i) {
    *at++ = static_cast<uint8_t>(pixel[i] >> 16);
    *at++ = static_cast<uint8_t>(pixel[i] >> 8);
    *at++ = static_cast<uint8_t>(pixel[i]);
  }
  return total;
}

}
}
}
