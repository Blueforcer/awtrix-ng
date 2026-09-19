#pragma once

#include <cstddef>

#ifndef AWTRIX_VFS_ROOT
#define AWTRIX_VFS_ROOT "/littlefs"
#endif

namespace awtrix {
namespace fs {

constexpr const char* kMountPoint = AWTRIX_VFS_ROOT;
constexpr const char* kPartitionLabel = "spiffs";

bool begin();

bool usage(std::size_t& totalBytes, std::size_t& usedBytes);

}
}
