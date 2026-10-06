#include "platform/tc002/daemon/update/ReleaseSlot.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "platform/posix/Files.h"
#include "platform/tc002/contract/release_slot.h"
#include "platform/tc002/contract/tc002_layout.h"
#include "platform/tc002/flasher/mtd_table.h"

namespace awtrix {
namespace tc002d {

uint64_t releaseSlotCapacity(const std::string& sysRoot, std::string& error) {
  std::string table;
  if (!posix::readText(sysRoot + "/proc/mtd", table, 4096)) {
    error = "cannot read /proc/mtd";
    return 0;
  }
  mtd_entry entry{};
  if (mtd_table_find_name(table.c_str(), TC002_RES_PARTITION, &entry) != 0) {
    error = "no " TC002_RES_PARTITION " partition in /proc/mtd";
    return 0;
  }
  const std::string device = sysRoot + TC002_MTD_DIR "/mtd" + std::to_string(entry.index) + "ro";
  posix::UniqueFd fd(::open(device.c_str(), O_RDONLY | O_CLOEXEC));
  uint8_t superblock[RELEASE_SLOT_SUPERBLOCK_BYTES];
  if (!fd.valid() || !posix::readAll(fd.get(), superblock, sizeof superblock)) {
    error = "cannot read " + device + ": " + std::strerror(errno);
    return 0;
  }
  release_slot_layout layout{};
  if (release_slot_locate(superblock, entry.size, entry.erase_size, &layout) != 0) {
    error = TC002_RES_PARTITION " leaves no release slot";
    return 0;
  }
  return layout.capacity;
}

}
}
