#pragma once

#include <string>

#include "core/render/Palette.h"

namespace awtrix {
namespace render {

// One RRGGBB per line, optionally suffixed "@0..100" to place the stop. Either every line carries
// a position or none do; any malformed line rejects the whole file.
bool parsePaletteFile(const std::string& text, Palette& out);

}
}
