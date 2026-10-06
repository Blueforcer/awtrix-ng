#pragma once

#include <string>

#include "platform/tc002/contract/tc002_layout.h"

namespace awtrix {
namespace tc002d {

constexpr const char* kDefaultUpdateDir = TC002_VOLATILE_DIR TC002_UPDATE_WORK;

struct InstallPaths {
  std::string data;
  std::string updateDir = kDefaultUpdateDir;

  std::string stateDir() const { return data + TC002_STATE; }
  // Where awtrix-tc002-flash records how a slot write ended, for the next daemon.
  std::string updateResult() const { return data + TC002_UPDATE_RESULT; }
};

}
}
