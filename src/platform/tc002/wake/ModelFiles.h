#pragma once

#include <cstddef>
#include <string>

namespace awtrix::tc002::wake {

struct ModelFiles {
  std::string model;
  std::string settingsPath;
  double cutoff = 0;
  std::size_t window = 0;
};

bool readModelFiles(const std::string& modelPath, ModelFiles& out, std::string& error);

}
