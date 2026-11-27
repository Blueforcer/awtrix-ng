#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace awtrix::hostapp::win32 {

struct HostLayout {
  std::filesystem::path program;
  std::filesystem::path data;

  std::filesystem::path log() const;
  std::vector<std::string> hostArgs() const;
};

}
