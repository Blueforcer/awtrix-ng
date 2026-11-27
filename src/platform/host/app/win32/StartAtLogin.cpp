#include "platform/host/app/win32/StartAtLogin.h"

#include <utility>

namespace awtrix::hostapp::win32 {

StartAtLogin::StartAtLogin(LoginRegistry& registry, std::wstring command)
    : registry_(registry), command_(std::move(command)) {}

void StartAtLogin::settle() {
  const std::optional<bool> chosen = registry_.choice();
  if (chosen) {
    apply(*chosen);
    return;
  }
  if (apply(true)) registry_.remember(true);
}

bool StartAtLogin::enabled() { return registry_.command() == command_ && !registry_.blockedByWindows(); }

bool StartAtLogin::set(bool on) {
  if (!apply(on)) return false;
  if (on && registry_.blockedByWindows() && !registry_.unblock()) return false;
  return registry_.remember(on);
}

bool StartAtLogin::apply(bool on) {
  const std::optional<std::wstring> current = registry_.command();
  if (!on) return !current || registry_.removeCommand();
  return current == command_ || registry_.setCommand(command_);
}

}
