#include "persistence/PaletteFiles.h"

#include <LittleFS.h>

#include <string>

#include "core/StrCase.h"
#include "core/render/Palette.h"
#include "core/render/PaletteFile.h"
#include "core/render/PaletteStore.h"

namespace awtrix {
namespace palettefiles {

// LittleFS is case-sensitive, but a palette named in a script or over the API rarely matches
// the file's capitalisation, so fall back to a case-insensitive scan of the directory.
void install() {
  render::setPaletteLoader([](const std::string& name, render::Palette& out) {
    if (name.find("..") != std::string::npos || name.find('/') != std::string::npos) return false;
    File f = LittleFS.open((String("/PALETTES/") + name.c_str() + ".txt").c_str(), "r");
    if (!f) {
      // arduino-pico's open() has no default mode; "r" is the ESP32 default.
      File dir = LittleFS.open("/PALETTES", "r");
      for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        std::string leaf = e.name() ? e.name() : "";
        const std::size_t slash = leaf.rfind('/');
        if (slash != std::string::npos) leaf.erase(0, slash + 1);
        if (leaf.size() <= 4 || !strcase::equalsIgnoreCase(leaf.substr(leaf.size() - 4), ".txt"))
          continue;
        if (!strcase::equalsIgnoreCase(leaf.substr(0, leaf.size() - 4), name)) continue;
        f = LittleFS.open((String("/PALETTES/") + leaf.c_str()).c_str(), "r");
        break;
      }
    }
    if (!f) return false;
    std::string text;
    text.reserve(static_cast<std::size_t>(f.size()));
    while (f.available()) text.push_back(static_cast<char>(f.read()));
    f.close();
    return render::parsePaletteFile(text, out);
  });
}

}
}
