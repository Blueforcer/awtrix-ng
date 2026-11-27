#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

#include "platform/host/app/HostSupervisor.h"

namespace awtrix::hostapp::win32 {

std::vector<uint32_t> iconPixels(HostState state, int size);
HICON makeIcon(HostState state, int size);

}
