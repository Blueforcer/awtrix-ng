#pragma once

#include <cstddef>
#include <cstdint>

#include "core/render/Canvas.h"

namespace awtrix {
namespace net {
namespace rgb {

int rowsPerDatagram(int width, std::size_t headerBytes);
bool validRows(uint16_t width, uint16_t height, uint16_t firstRow, uint16_t rows, std::size_t rgbBytes);
uint8_t* pack(const Canvas& canvas, int firstRow, int rows, uint8_t* out);
void draw(const uint8_t* rgb, Canvas& canvas);

}
}
}
