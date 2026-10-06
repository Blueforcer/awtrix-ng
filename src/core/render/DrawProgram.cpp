#include "core/render/DrawProgram.h"

#include "core/render/TextRenderer.h"

namespace awtrix {
namespace render {

void drawProgram(Canvas& c, const GfxFont& font, int textRise, const DrawProgram& p,
                 uint32_t inheritColor, int dx, int dy) {
  for (const DrawCommand& d : p.commands) {
    const uint32_t color = d.inheritColor ? inheritColor : d.color;
    const int x = d.x + dx, y = d.y + dy;
    const uint32_t* data = p.data.data() + d.first;
    switch (d.kind) {
      case DrawKind::Pixel: c.setPixel(x, y, color); break;
      case DrawKind::Pixels:
        for (uint32_t i = 0; i + 1 < d.count; i += 2)
          c.setPixel(static_cast<int>(data[i]) + dx, static_cast<int>(data[i + 1]) + dy, color);
        break;
      case DrawKind::Line: c.drawLine(x, y, d.a + dx, d.b + dy, color); break;
      case DrawKind::Rect: c.drawRect(x, y, d.a, d.b, color); break;
      case DrawKind::FillRect: c.fillRect(x, y, d.a, d.b, color); break;
      case DrawKind::Circle: c.drawCircle(x, y, d.a, color); break;
      case DrawKind::FillCircle: c.fillCircle(x, y, d.a, color); break;
      case DrawKind::Text:
        text::drawText(c, font, x, y + textRise, p.text.view().substr(d.first, d.count), color);
        break;
      case DrawKind::Bitmap: {
        uint32_t i = 0;
        for (int yy = 0; yy < d.b; ++yy)
          for (int xx = 0; xx < d.a; ++xx, ++i)
            if (i < d.count) c.setPixel(x + xx, y + yy, data[i]);
        break;
      }
    }
  }
}

}
}
