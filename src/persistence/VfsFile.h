#pragma once

#include <string>

#include "persistence/Filesystem.h"

namespace awtrix {
namespace fs {

std::string vfsPath(const std::string& path);

int openRead(const std::string& path);

long fileSize(const std::string& path);

bool isFile(const std::string& path);

}
}
