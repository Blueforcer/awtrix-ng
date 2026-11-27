#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/host/app/HostSupervisor.h"
#include "platform/linux/hostd/Contract.h"

namespace awtrix::hostapp {

enum class MenuAction : uint8_t { None, Start, CopyCode, OpenClock, OpenStatusPage, OpenLog, StartAtLogin, Quit };

struct MenuItem {
  std::string text;
  bool enabled = false;
  MenuAction action = MenuAction::None;
  std::string value;
  bool checkable = false;
  bool checked = false;
  bool separator = false;
};

struct MenuInput {
  std::string version;
  int port = hostd::kDefaultPort;
  HostView host;
  std::string_view status;
  std::string logPath;
  bool startAtLogin = false;
};

std::string statusLine(const HostView& host, int port);
std::string statusPageUrl(int port);
std::vector<MenuItem> buildStatusMenu(const MenuInput& input);

}
