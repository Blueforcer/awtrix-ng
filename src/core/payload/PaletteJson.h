#pragma once

#include "core/api/JsonReader.h"
#include "core/render/ColorRamp.h"

namespace awtrix {
namespace payload {

// Accepts a stored palette name, a flat list of colors, or a list of {color, pos} stops.
bool readPalette(api::JsonReader r, render::ColorRamp& out);

// A list of colors or of {color, pos} stops, built without looking up any stored palette.
bool readPaletteStops(api::JsonReader r, render::Palette& out);

}
}
