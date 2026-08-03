#include "core/render/PaletteFile.h"

#include <cstddef>
#include <cstdlib>

#include "core/render/ColorRamp.h"

namespace awtrix {
namespace render {

namespace {

constexpr std::size_t kMaxStops = 16;

bool isHex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

}

bool parsePaletteFile(const std::string& text, Palette& out) {
  PaletteStop stops[kMaxStops];
  std::size_t n = 0;
  std::size_t positioned = 0;

  std::size_t pos = 0;
  // pos is allowed to reach one past the end so a final line without a trailing newline still
  // gets parsed; the last iteration pushes it beyond that to stop.
  while (pos <= text.size() && n < kMaxStops) {
    std::size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    std::string line = text.substr(pos, eol - pos);
    if (eol == text.size()) pos = text.size() + 1;
    else pos = eol + 1;

    std::size_t b = 0, e = line.size();
    while (b < e && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r')) ++b;
    while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' || line[e - 1] == '\r')) --e;
    if (b < e && line[b] == '#') ++b;
    if (b >= e) continue;

    const std::size_t at = line.find('@', b);
    const std::size_t colourEnd = (at == std::string::npos || at > e) ? e : at;

    if (colourEnd - b != 6) return false;
    for (std::size_t i = b; i < colourEnd; ++i)
      if (!isHex(line[i])) return false;
    const uint32_t colour =
        static_cast<uint32_t>(std::strtoul(line.substr(b, 6).c_str(), nullptr, 16));

    int place = -1;
    if (colourEnd != e) {
      std::size_t p = colourEnd + 1;
      if (p >= e) return false;
      int v = 0;
      for (; p < e; ++p) {
        if (line[p] < '0' || line[p] > '9') return false;
        v = v * 10 + (line[p] - '0');
        if (v > 100) return false;
      }
      place = v;
      ++positioned;
    }

    stops[n].color = colour;
    stops[n].pos = static_cast<uint8_t>(place < 0 ? 0 : place);
    ++n;
  }

  if (n == 0) return false;
  if (positioned != 0 && positioned != n) return false;

  if (positioned == 0) {
    uint32_t plain[kMaxStops];
    for (std::size_t i = 0; i < n; ++i) plain[i] = stops[i].color;
    out = paletteFromStops(plain, n);
    return true;
  }

  // Insertion sort by position: n is at most 16 and files are usually already in order.
  for (std::size_t i = 1; i < n; ++i) {
    const PaletteStop key = stops[i];
    std::size_t j = i;
    while (j > 0 && stops[j - 1].pos > key.pos) {
      stops[j] = stops[j - 1];
      --j;
    }
    stops[j] = key;
  }
  out = paletteFromPositionedStops(stops, n);
  return true;
}

}
}
