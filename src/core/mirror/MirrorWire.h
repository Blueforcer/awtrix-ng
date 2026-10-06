#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/render/Canvas.h"
#include "core/render/PageInfo.h"

namespace awtrix {
namespace mirror {
namespace wire {

// Every datagram starts with the magic "AWMR", the protocol version and the type. Integers are
// little endian. A viewer sends Subscribe with its panel size to the shared clock every few seconds
// and Leave when it stops; the shared clock answers each viewer with Idle while it has nothing to
// share and with Frame datagrams, each carrying whole rows of 8-bit RGB, while it has. A viewer of
// another panel size only ever gets one Idle back, which tells it the size of the shared panel.
constexpr uint16_t kPort = 4212;
constexpr uint8_t kVersion = 1;
// Stays below the Ethernet and Wi-Fi MTU, so no datagram is ever fragmented on the way.
constexpr std::size_t kMaxDatagram = 1400;
constexpr std::size_t kMaxName = 255;

enum class Type : uint8_t { Subscribe = 1, Leave = 2, Idle = 3, Frame = 4 };

struct Packet {
  Type type = Type::Subscribe;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t frame = 0;
  uint16_t firstRow = 0;
  uint16_t rows = 0;
  PageKind kind = PageKind::App;
  std::string_view app;
  std::string_view incoming;
  const uint8_t* rgb = nullptr;
};

// Views in the result point into data. Anything malformed, from another version or describing rows
// outside its own frame is refused.
bool decode(const uint8_t* data, std::size_t length, Packet& out);

std::size_t encodeSubscribe(uint16_t width, uint16_t height, uint8_t* out, std::size_t capacity);
std::size_t encodeLeave(uint8_t* out, std::size_t capacity);
std::size_t encodeIdle(uint16_t width, uint16_t height, uint8_t* out, std::size_t capacity);

struct FrameHeader {
  uint16_t frame = 0;
  PageKind kind = PageKind::App;
  std::string_view app;
  std::string_view incoming;
};

// How many rows of a frame fit in one datagram next to its header.
int rowsPerDatagram(int width, const FrameHeader& header);
std::size_t encodeFrame(const Canvas& canvas, const FrameHeader& header, int firstRow, int rows,
                        uint8_t* out, std::size_t capacity);

}
}
}
