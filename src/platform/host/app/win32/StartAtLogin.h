#pragma once

#include <optional>
#include <string>

namespace awtrix::hostapp::win32 {

class LoginRegistry {
 public:
  virtual ~LoginRegistry() = default;
  virtual std::optional<bool> choice() = 0;
  virtual bool remember(bool on) = 0;
  virtual std::optional<std::wstring> command() = 0;
  virtual bool setCommand(const std::wstring& command) = 0;
  virtual bool removeCommand() = 0;
  virtual bool blockedByWindows() = 0;
  virtual bool unblock() = 0;
};

class StartAtLogin {
 public:
  StartAtLogin(LoginRegistry& registry, std::wstring command);

  void settle();
  bool enabled();
  bool set(bool on);

 private:
  bool apply(bool on);

  LoginRegistry& registry_;
  std::wstring command_;
};

}
