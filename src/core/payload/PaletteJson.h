#pragma once

#include "core/api/JsonReader.h"
#include "core/render/ColorRamp.h"

namespace awtrix {
namespace payload {

bool readPalette(api::JsonReader r, render::ColorRamp& out);

}
}
