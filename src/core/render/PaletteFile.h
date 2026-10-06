#pragma once

#include <cstddef>
#include <string>
#include "core/FileNames.h"

#include "core/render/Palette.h"

namespace awtrix {
namespace render {

// Largest palette file an upload or a restore accepts.
constexpr std::size_t kMaxPaletteFileBytes = 512;

// One RRGGBB per line, optionally suffixed "@0..100" to place the stop. Either every line carries
// a position or none do; any malformed line rejects the whole file.
bool parsePaletteFile(const std::string& text, Palette& out);

using PaletteRead = bool (*)(const std::string& filename, std::string& text);
using PaletteNameVisitor = FileNameVisitor;
using PaletteNames = void (*)(const PaletteNameVisitor& visit);

// Exact filename first, then a case-insensitive .txt match. A true visitor result stops the scan.
bool loadPaletteFile(const std::string& name, Palette& out, PaletteRead read, PaletteNames forEachName);

}
}
