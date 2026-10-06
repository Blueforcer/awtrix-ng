#pragma once

#include <string>
#include <string_view>

#include "persistence/Filesystem.h"

namespace awtrix {
namespace fs {

std::string vfsPath(const std::string& path);

int openRead(std::string_view path);

long fileSize(const std::string& path);

bool isFile(const std::string& path);

}
}
