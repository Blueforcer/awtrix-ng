#pragma once

namespace awtrix {
namespace palettefiles {

// Makes render::paletteByName() resolve custom palettes from /PALETTES/<name>.txt on LittleFS.
void install();

}
}
