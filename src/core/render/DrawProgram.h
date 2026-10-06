#pragma once

#include <cstdint>

#include "core/memory/CheckedStorage.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {

enum class DrawKind : uint8_t {
  Pixel, Pixels, Line, Rect, FillRect, Circle, FillCircle, Text, Bitmap
};

namespace render {

// a/b are x2/y2 of a line, width/height of a rectangle or bitmap, and a is a circle's radius.
// first/count index the program's text (bytes) or data (coordinate pairs or bitmap colours).
struct DrawCommand {
  DrawKind kind = DrawKind::Pixel;
  bool inheritColor = false;
  int x = 0, y = 0, a = 0, b = 0;
  uint32_t color = 0xFFFFFFu;
  uint32_t first = 0, count = 0;
};

// The parsed "draw" commands of a pushed app or a layout region. Checked storage keeps a failed
// allocation from aborting the firmware.
struct DrawProgram {
  checked::CheckedArray<DrawCommand> commands;
  checked::CheckedArray<uint32_t> data;
  checked::CheckedString text;
  bool valid() const { return commands.valid() && data.valid() && text.valid(); }
};

// Commands without a colour take inheritColor; everything is shifted by dx/dy. A text command's
// baseline is textRise rows below its y, so text at y 1 sits where the page's own text does.
void drawProgram(Canvas& c, const GfxFont& font, int textRise, const DrawProgram& p,
                 uint32_t inheritColor, int dx = 0, int dy = 0);

}
}
