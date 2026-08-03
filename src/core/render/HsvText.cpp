#include "core/render/HsvText.h"

#include "core/render/Color.h"
#include "core/render/TextEncoding.h"
#include "core/render/TextRenderer.h"

namespace awtrix {
namespace render {

int drawHsvText(Canvas& c, const GfxFont& font, int x, int baselineY, const std::string& s,
                int64_t nowMs) {
  // Rainbow spread over the string: the glyphs share one full hue turn between them, and the whole
  // thing rotates once every 256 * 20 ms.
  const int len = static_cast<int>(text::glyphCount(font, s));
  if (!len) return 0;
  const int base = static_cast<int>((nowMs / 20) % 256);

  text::GlyphIter it(font, s);
  const FontGlyph* g = nullptr;
  int adv = 0;
  for (int i = 0; it.next(g); ++i) {
    const int hue = ((i * 256 / len + base) & 0xFF) * 360 / 256;
    adv += text::drawGlyph(c, font, x + adv, baselineY, g, color::fromHsv(hue, 100, 100));
  }
  return adv;
}

}
}
