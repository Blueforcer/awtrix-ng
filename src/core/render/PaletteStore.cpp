#include "core/render/PaletteStore.h"

#include <map>

#include "core/StrCase.h"
#include "core/render/ColorRamp.h"

namespace awtrix {
namespace render {

namespace {

PaletteLoader g_loader;

std::map<std::string, std::weak_ptr<const Palette>>& cache() {
  static std::map<std::string, std::weak_ptr<const Palette>> c;
  return c;
}

}

void setPaletteLoader(PaletteLoader loader) { g_loader = std::move(loader); }

std::shared_ptr<const Palette> paletteByName(const std::string& name) {
  if (name.empty()) return nullptr;
  const std::string key = strcase::toLower(name);

  auto& c = cache();
  const auto it = c.find(key);
  if (it != c.end()) {
    if (std::shared_ptr<const Palette> hit = it->second.lock()) return hit;
    c.erase(it);
  }

  std::shared_ptr<const Palette> made;
  Palette loaded{};
  if (g_loader && g_loader(name, loaded)) {
    made = std::make_shared<const Palette>(loaded);
  } else if (const Palette* stock = findStockPalette(name)) {
    // Stock palettes live in ROM for the whole run, so hand out a shared_ptr that never deletes.
    made = std::shared_ptr<const Palette>(stock, [](const Palette*) {});
  } else {
    return nullptr;
  }

  c[key] = made;
  return made;
}

std::shared_ptr<const Palette> paletteFromStopList(const uint32_t* stops, std::size_t n) {
  if (!stops || n == 0) return nullptr;
  return std::make_shared<const Palette>(paletteFromStops(stops, n));
}

std::shared_ptr<const Palette> paletteFromPositionedStopList(const PaletteStop* stops,
                                                             std::size_t n) {
  if (!stops || n == 0) return nullptr;
  return std::make_shared<const Palette>(paletteFromPositionedStops(stops, n));
}

void clearPaletteCache() { cache().clear(); }

}
}
