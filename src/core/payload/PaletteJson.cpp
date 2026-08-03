#include "core/payload/PaletteJson.h"

#include <cstddef>
#include <string>
#include <utility>

#include "core/JsonColor.h"
#include "core/render/PaletteStore.h"

namespace awtrix {
namespace payload {

namespace {
constexpr std::size_t kMaxStops = 16;
}

// Accepts a stored palette name, a flat list of colors, or a list of {color, pos} stops with pos
// in 0..100. Mixing bare colors and positioned stops in one list is rejected.
bool readPalette(api::JsonReader r, render::ColorRamp& out) {
  if (r.isNull()) {
    out.pal = nullptr;
    return true;
  }

  if (r.isArray()) {
    render::PaletteStop stops[kMaxStops];
    std::size_t n = 0;
    std::size_t placed = 0;
    api::JsonReader arr = r;
    if (arr.enterArray()) {
      while (arr.nextElement()) {
        if (n >= kMaxStops) break;
        if (arr.isObject()) {
          api::JsonReader obj = arr;
          uint32_t c = 0u;
          long long p = -1;
          bool haveColour = false;
          if (!obj.enterObject()) return false;
          while (obj.nextMember()) {
            if (obj.keyEquals("color")) {
              if (!color::readColor(obj, c)) return false;
              haveColour = true;
            } else if (obj.keyEquals("pos")) {
              if (!obj.isNumber() || !obj.asLong(p)) return false;
              if (p < 0 || p > 100) return false;
            }
            if (!obj.skipValue()) break;
          }
          if (!haveColour || p < 0) return false;
          stops[n].color = c;
          stops[n].pos = static_cast<uint8_t>(p);
          ++placed;
        } else {
          uint32_t c = 0u;
          if (!color::readColor(arr, c)) return false;
          stops[n].color = c;
          stops[n].pos = 0;
        }
        ++n;
        if (!arr.skipValue()) break;
      }
    }
    if (n == 0) return false;
    if (placed != 0 && placed != n) return false;
    if (placed == 0) {
      uint32_t plain[kMaxStops];
      for (std::size_t i = 0; i < n; ++i) plain[i] = stops[i].color;
      out.pal = render::paletteFromStopList(plain, n);
      return true;
    }
    // Insertion sort by position: the ramp needs ascending stops and the list is at most 16 long.
    for (std::size_t i = 1; i < n; ++i) {
      const render::PaletteStop key = stops[i];
      std::size_t j = i;
      while (j > 0 && stops[j - 1].pos > key.pos) {
        stops[j] = stops[j - 1];
        --j;
      }
      stops[j] = key;
    }
    out.pal = render::paletteFromPositionedStopList(stops, n);
    return true;
  }

  if (r.isString()) {
    std::string name;
    if (!r.appendString(name)) return false;
    if (name.empty()) {
      out.pal = nullptr;
      return true;
    }
    std::shared_ptr<const render::Palette> found = render::paletteByName(name);
    if (!found) return false;
    out.pal = std::move(found);
    return true;
  }

  return false;
}

}
}
