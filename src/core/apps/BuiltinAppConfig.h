#pragma once

#include <string>
#include <string_view>

#include "core/Command.h"

namespace awtrix {
struct Settings;

namespace builtinconfig {

bool hasApp(std::string_view name);

// The fields the app offers under the device's clock profile, each with its factory default and
// current value, in the shape script configuration answers with.
void appendConfigJson(std::string& out, std::string_view name, const Settings& settings,
                      bool clockFaces);

// Validates an app's complete patch before changing the supplied settings. Date's public
// weekdayBar maps to its independent dateWeekdayBar; global settings are never accepted here.
// changed stays false for a patch without members.
DispatchResult applyPatch(std::string_view name, std::string_view patch, bool clockFaces,
                          Settings& settings, bool& changed, DispatchDetail& detail);

}
}
