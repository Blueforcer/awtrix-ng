#pragma once

namespace awtrix {
class IBoard;
struct DeviceConfig;
IBoard& activeBoard(const DeviceConfig& cfg);
}
