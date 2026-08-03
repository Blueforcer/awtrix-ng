#pragma once

#include "core/Settings.h"

namespace awtrix {
namespace nvs {

void loadSettings(Settings& s);
void saveSettings(const Settings& s);

}
}
