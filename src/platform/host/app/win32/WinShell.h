#pragma once

#include <windows.h>

#include <string>

#include "platform/host/app/win32/TrayController.h"

namespace awtrix::hostapp::win32 {

class WinShell : public Shell {
 public:
  explicit WinShell(HWND owner);

  bool open(const std::string& target) override;
  bool copy(const std::string& text) override;
  bool exists(const std::string& path) override;

 private:
  HWND owner_;
};

}
