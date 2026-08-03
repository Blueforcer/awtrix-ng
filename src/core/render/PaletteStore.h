#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/render/ColorRamp.h"
#include "core/render/Palette.h"

namespace awtrix {
namespace render {

// Hook for the filesystem layer. A palette the loader can supply wins over the stock one of the
// same name.
using PaletteLoader = std::function<bool(const std::string& name, Palette& out)>;
void setPaletteLoader(PaletteLoader loader);

// Case-insensitive lookup, nullptr if nothing matches. Results are cached weakly, so a palette
// stays loaded only while something still holds it.
std::shared_ptr<const Palette> paletteByName(const std::string& name);

std::shared_ptr<const Palette> paletteFromStopList(const uint32_t* stops, std::size_t n);

std::shared_ptr<const Palette> paletteFromPositionedStopList(const PaletteStop* stops,
                                                             std::size_t n);

void clearPaletteCache();

}
}
