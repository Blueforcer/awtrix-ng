#pragma once

#include <cstdint>
#include <string>

// The release slot behind the res squashfs (release_slot.h), seen from the daemon: how large a
// release image it takes. sysRoot prefixes /proc/mtd and the device, so tests can use files.
namespace awtrix {
namespace tc002d {

// 0 with error set when res is missing, unreadable or leaves no slot.
uint64_t releaseSlotCapacity(const std::string& sysRoot, std::string& error);

}
}
