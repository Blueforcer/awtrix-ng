#pragma once

#include <string>

#include "core/api/StateJson.h"

namespace awtrix {
class CoreEngine;
class IBoard;

std::string buildDeviceStateJson(CoreEngine& engine, IBoard& board, const std::string& uid,
                                 bool scriptingRunning);

}
