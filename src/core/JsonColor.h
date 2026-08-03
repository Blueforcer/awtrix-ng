#pragma once

#include <cstdint>

#include "core/api/JsonReader.h"

namespace awtrix {
namespace color {

bool readColor(api::JsonReader r, uint32_t& out);

}
}
