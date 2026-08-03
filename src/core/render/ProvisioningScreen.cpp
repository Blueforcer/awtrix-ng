#include "core/render/ProvisioningScreen.h"

#include "core/render/HsvText.h"

namespace awtrix {
namespace render {

void drawProvisioningScreen(Canvas& c, const GfxFont& font, int64_t nowMs) {
  c.clear(0x000000u);
  drawHsvText(c, font, 2, 6, "AP MODE", nowMs);
}

}
}
