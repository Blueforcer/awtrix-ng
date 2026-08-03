#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {
namespace render {

void drawProvisioningScreen(Canvas& c, const GfxFont& font, int64_t nowMs);

}
}
