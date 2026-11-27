#include "platform/host/Memory.h"

#include <string>

#include "platform/host/Files.h"

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace awtrix::os {

bool availableMemory(std::uint64_t& bytes) {
#if defined(__linux__)
  std::string text;
  return readFile("/proc/meminfo", text, 64 * 1024) && parseMemAvailable(text, bytes);
#elif defined(__APPLE__)
  vm_statistics64_data_t stats{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  const mach_port_t host = mach_host_self();
  vm_size_t page = 0;
  const bool read = host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) ==
                        KERN_SUCCESS &&
                    host_page_size(host, &page) == KERN_SUCCESS;
  mach_port_deallocate(mach_task_self(), host);
  if (!read || page == 0) return false;
  bytes = (static_cast<std::uint64_t>(stats.free_count) + stats.inactive_count) * page;
  return true;
#else
  (void)bytes;
  return false;
#endif
}

}
