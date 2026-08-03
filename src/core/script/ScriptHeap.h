#pragma once

#include <cstddef>


extern "C" {
void* awtrix_script_heap_alloc(size_t size);
void* awtrix_script_heap_realloc(void* ptr, size_t size);
void awtrix_script_heap_free(void* ptr);
}

namespace awtrix {
namespace script {
namespace heap {

struct Info {
  const char* name = "internal";
  std::size_t budgetBytes = 0;
};

Info info();

void setInstallReserve(std::size_t bytes);
void clearInstallReserve();

std::size_t installLowWater();

// Whether an allocation may proceed without eating into the reserve. Written to survive
// unsigned wrap: subtracting first would let a request larger than the free heap pass.
inline bool allocFitsReserve(std::size_t freeNow, std::size_t requested,
                             std::size_t reserve) {
  if (reserve == 0) return true;
  if (requested > freeNow) return false;
  return freeNow - requested >= reserve;
}

// Holds bytes back from the script allocator for the duration of an install, so compiling an
// oversized script fails inside the VM instead of starving the rest of the firmware.
class InstallReserve {
 public:
  explicit InstallReserve(std::size_t bytes) { setInstallReserve(bytes); }
  ~InstallReserve() { clearInstallReserve(); }
  InstallReserve(const InstallReserve&) = delete;
  InstallReserve& operator=(const InstallReserve&) = delete;
};

}
}
}
