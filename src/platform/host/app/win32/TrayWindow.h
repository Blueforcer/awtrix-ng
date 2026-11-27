#pragma once

#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "platform/host/app/win32/TrayController.h"

namespace awtrix::hostapp::win32 {

inline constexpr wchar_t kWindowClass[] = L"AWTRIXScriptHostWindow";

class TrayWindow : public TrayView {
 public:
  explicit TrayWindow(HINSTANCE instance);
  ~TrayWindow() override;
  TrayWindow(const TrayWindow&) = delete;
  TrayWindow& operator=(const TrayWindow&) = delete;

  bool created() const { return window_ != nullptr; }
  HWND handle() const { return window_; }
  void signalHostEnded();
  int run(TrayController& controller);

  void show(HostState state, const std::string& tip) override;
  void wakeIn(std::optional<int64_t> delayMs) override;
  void hide() override;

 private:
  static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
  LRESULT receive(UINT message, WPARAM wParam, LPARAM lParam);
  void place();
  void openMenu(POINT at);
  void close();
  NOTIFYICONDATAW notifyData(UINT flags, std::wstring_view tip) const;

  HINSTANCE instance_;
  HWND window_ = nullptr;
  UINT taskbarCreated_ = 0;
  TrayController* controller_ = nullptr;
  HICON icon_ = nullptr;
  int iconSize_ = 0;
  HostState iconState_ = HostState::Stopped;
  HostState state_ = HostState::Stopped;
  std::wstring tip_;
  bool alive_ = false;
  bool hidden_ = false;
  bool menuOpen_ = false;
  bool closing_ = false;
};

}
