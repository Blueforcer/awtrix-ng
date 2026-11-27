#pragma once

#include <optional>
#include <string>

#include "platform/host/app/win32/StartAtLogin.h"

namespace awtrix::hostapp::win32 {

struct RunPlaces {
  std::wstring run;
  std::wstring approved;
  std::wstring settings;
  std::wstring name;
};

RunPlaces loginPlaces();

class RunRegistry : public LoginRegistry {
 public:
  explicit RunRegistry(RunPlaces places);

  std::optional<bool> choice() override;
  bool remember(bool on) override;
  std::optional<std::wstring> command() override;
  bool setCommand(const std::wstring& command) override;
  bool removeCommand() override;
  bool blockedByWindows() override;
  bool unblock() override;

 private:
  RunPlaces places_;
};

}
