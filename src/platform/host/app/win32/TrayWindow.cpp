#include "platform/host/app/win32/TrayWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <iterator>
#include <vector>

#include "platform/host/app/win32/MenuText.h"
#include "platform/host/app/win32/TrayImage.h"

namespace awtrix::hostapp::win32 {
namespace {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kHostEnded = WM_APP + 2;
constexpr UINT kIconId = 1;
constexpr UINT_PTR kWakeTimer = 1;
constexpr UINT kFirstCommand = 1;
constexpr wchar_t kProduct[] = L"AWTRIX Script Host";

int64_t nowMs() { return static_cast<int64_t>(::GetTickCount64()); }

}

TrayWindow::TrayWindow(HINSTANCE instance) : instance_(instance) {
  WNDCLASSEXW type{};
  type.cbSize = sizeof type;
  type.lpfnWndProc = procedure;
  type.hInstance = instance;
  type.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(1));
  type.lpszClassName = kWindowClass;
  if (!::RegisterClassExW(&type)) return;
  window_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, kProduct, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                              instance, this);
  if (!window_) return;
  taskbarCreated_ = ::RegisterWindowMessageW(L"TaskbarCreated");
  if (taskbarCreated_) ::ChangeWindowMessageFilterEx(window_, taskbarCreated_, MSGFLT_ALLOW, nullptr);
}

TrayWindow::~TrayWindow() {
  hide();
  if (alive_) ::DestroyWindow(window_);
  if (icon_) ::DestroyIcon(icon_);
  ::UnregisterClassW(kWindowClass, instance_);
}

void TrayWindow::signalHostEnded() { ::PostMessageW(window_, kHostEnded, 0, 0); }

int TrayWindow::run(TrayController& controller) {
  controller_ = &controller;
  controller.begin(nowMs());
  MSG message{};
  while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
    ::TranslateMessage(&message);
    ::DispatchMessageW(&message);
  }
  controller.quit();
  controller_ = nullptr;
  return 0;
}

void TrayWindow::show(HostState state, const std::string& tip) {
  state_ = state;
  tip_ = tipText(tip);
  place();
}

void TrayWindow::wakeIn(std::optional<int64_t> delayMs) {
  if (!alive_) return;
  ::KillTimer(window_, kWakeTimer);
  if (!delayMs) return;
  const int64_t delay = std::clamp<int64_t>(*delayMs, USER_TIMER_MINIMUM, USER_TIMER_MAXIMUM);
  ::SetTimer(window_, kWakeTimer, static_cast<UINT>(delay), nullptr);
}

void TrayWindow::hide() {
  if (hidden_ || !window_) return;
  hidden_ = true;
  NOTIFYICONDATAW data = notifyData(0, {});
  ::Shell_NotifyIconW(NIM_DELETE, &data);
}

LRESULT CALLBACK TrayWindow::procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_NCCREATE) {
    auto* created = static_cast<TrayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
    created->window_ = window;
    created->alive_ = true;
    ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
  }
  auto* self = reinterpret_cast<TrayWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
  return self ? self->receive(message, wParam, lParam) : ::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayWindow::receive(UINT message, WPARAM wParam, LPARAM lParam) {
  if (taskbarCreated_ && message == taskbarCreated_) {
    place();
    return 0;
  }
  switch (message) {
    case kTrayMessage:
      switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_CONTEXTMENU:
          openMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
          break;
        case WM_MOUSEMOVE:
          if (controller_) {
            const std::wstring tip = tipText(controller_->tip(nowMs()));
            if (tip != tip_) {
              tip_ = tip;
              place();
            }
          }
          break;
      }
      return 0;
    case kHostEnded:
      if (controller_) controller_->hostSignalled(nowMs());
      return 0;
    case WM_TIMER:
      if (wParam == kWakeTimer) {
        ::KillTimer(window_, kWakeTimer);
        if (controller_) controller_->wake(nowMs());
      }
      return 0;
    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
      place();
      return 0;
    case WM_CLOSE:
      close();
      return 0;
    case WM_QUERYENDSESSION:
      return TRUE;
    case WM_ENDSESSION:
      if (wParam && controller_) controller_->quit();
      return 0;
    case WM_DESTROY:
      ::PostQuitMessage(0);
      return 0;
    case WM_NCDESTROY:
      alive_ = false;
      break;
  }
  return ::DefWindowProcW(window_, message, wParam, lParam);
}

void TrayWindow::place() {
  if (hidden_ || !alive_) return;
  const UINT dpi = ::GetDpiForWindow(window_);
  const int size = ::GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : USER_DEFAULT_SCREEN_DPI);
  if (!icon_ || size != iconSize_ || state_ != iconState_) {
    if (const HICON fresh = makeIcon(state_, size)) {
      if (icon_) ::DestroyIcon(icon_);
      icon_ = fresh;
      iconSize_ = size;
      iconState_ = state_;
    }
  }
  NOTIFYICONDATAW data = notifyData(NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP, tip_);
  if (::Shell_NotifyIconW(NIM_MODIFY, &data)) return;
  NOTIFYICONDATAW added = notifyData(NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP, kProduct);
  if (!::Shell_NotifyIconW(NIM_ADD, &added)) return;
  added.uVersion = NOTIFYICON_VERSION_4;
  ::Shell_NotifyIconW(NIM_SETVERSION, &added);
  ::Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayWindow::openMenu(POINT at) {
  if (menuOpen_ || closing_ || !controller_) return;
  const std::vector<MenuItem> items = controller_->menu(nowMs());
  const HMENU menu = ::CreatePopupMenu();
  if (!menu) return;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const MenuItem& item = items[i];
    if (item.separator) {
      ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
      continue;
    }
    UINT flags = MF_STRING;
    if (!item.enabled) flags |= MF_GRAYED;
    if (item.checked) flags |= MF_CHECKED;
    ::AppendMenuW(menu, flags, kFirstCommand + i, menuLabel(item.text).c_str());
  }
  menuOpen_ = true;
  ::SetForegroundWindow(window_);
  const UINT align = ::GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
  const auto chosen = static_cast<UINT>(::TrackPopupMenuEx(
      menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, at.x, at.y, window_, nullptr));
  ::PostMessageW(window_, WM_NULL, 0, 0);
  ::DestroyMenu(menu);
  menuOpen_ = false;
  if (closing_) {
    close();
    return;
  }
  if (chosen < kFirstCommand || chosen - kFirstCommand >= items.size()) return;
  if (!controller_->choose(items[chosen - kFirstCommand], nowMs())) ::DestroyWindow(window_);
}

void TrayWindow::close() {
  closing_ = true;
  if (menuOpen_) {
    ::EndMenu();
    return;
  }
  if (controller_) controller_->quit();
  if (alive_) ::DestroyWindow(window_);
}

NOTIFYICONDATAW TrayWindow::notifyData(UINT flags, std::wstring_view tip) const {
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof data;
  data.hWnd = window_;
  data.uID = kIconId;
  data.uFlags = flags;
  data.uCallbackMessage = kTrayMessage;
  data.hIcon = icon_;
  tip.copy(data.szTip, std::size(data.szTip) - 1);
  return data;
}

}
