#pragma once

#include <string>

#include "core/render/Palette.h"

namespace awtrix::host {

// Exact filename first, then case-insensitive matching as on the ESP32 loader.
bool loadPalette(const std::string& name, render::Palette& out);

}
