#pragma once

#include <cstdint>

#include <string>

#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {
namespace render {

int drawHsvText(Canvas& c, const GfxFont& font, int x, int baselineY, const std::string& s,
                int64_t nowMs);

}
}
