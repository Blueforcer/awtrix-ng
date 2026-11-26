#include "core/net/RgbRows.h"

#include "core/net/Datagram.h"

namespace awtrix {
namespace net {
namespace rgb {

int rowsPerDatagram(int width, std::size_t headerBytes) {
  if (width <= 0 || headerBytes >= kMaxDatagram) return 0;
  return static_cast<int>((kMaxDatagram - headerBytes) / (static_cast<std::size_t>(width) * 3u));
}

bool validRows(uint16_t width, uint16_t height, uint16_t firstRow, uint16_t rows, std::size_t rgbBytes) {
  return width > 0 && height > 0 && rows > 0 && firstRow < height && rows <= height - firstRow &&
         rgbBytes == static_cast<std::size_t>(width) * rows * 3u;
}

uint8_t* pack(const Canvas& canvas, int firstRow, int rows, uint8_t* out) {
  const uint32_t* pixel = canvas.data() + static_cast<std::size_t>(firstRow) * canvas.width();
  for (std::size_t i = 0, n = static_cast<std::size_t>(canvas.width()) * rows; i < n; ++i) {
    *out++ = static_cast<uint8_t>(pixel[i] >> 16);
    *out++ = static_cast<uint8_t>(pixel[i] >> 8);
    *out++ = static_cast<uint8_t>(pixel[i]);
  }
  return out;
}

void draw(const uint8_t* rgb, Canvas& canvas) {
  for (int y = 0; y < canvas.height(); ++y)
    for (int x = 0; x < canvas.width(); ++x, rgb += 3)
      canvas.setPixel(x, y, (static_cast<uint32_t>(rgb[0]) << 16) | (static_cast<uint32_t>(rgb[1]) << 8) | rgb[2]);
}

}
}
}
