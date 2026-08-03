#pragma once

#include <string>

namespace awtrix {
class CoreEngine;

namespace apporder {

void save(const std::string& json);
void load(CoreEngine& engine);

}
}
