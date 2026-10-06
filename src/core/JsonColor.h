#pragma once

#include <cstdint>

#include "core/api/JsonReader.h"

namespace awtrix {
namespace color {

bool readColor(api::JsonReader r, uint32_t& out);

// True for the string "palette" in any case: the color then comes from a palette.
bool isPaletteWord(api::JsonReader r);

}
}
