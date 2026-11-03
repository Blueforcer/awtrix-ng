#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "platform/posix/Files.h"

namespace awtrix::tc002::wake {

struct ModelFiles {
  std::shared_ptr<const posix::MappedFile> model;
  std::string settingsPath;
  double cutoff = 0;
  std::size_t window = 0;
};

bool readModelFiles(const std::string& modelPath, ModelFiles& out, std::string& error);

}
