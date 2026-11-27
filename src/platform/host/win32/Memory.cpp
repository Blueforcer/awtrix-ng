#include "platform/host/Memory.h"

#include <windows.h>

namespace awtrix::os {

bool availableMemory(std::uint64_t& bytes) {
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof status;
  if (!::GlobalMemoryStatusEx(&status)) return false;
  bytes = status.ullAvailPhys;
  return true;
}

}
