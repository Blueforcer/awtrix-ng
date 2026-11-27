#include "platform/host/app/win32/WinShell.h"

#include <shellapi.h>

#include <cstring>

#include "platform/host/win32/Wide.h"

namespace awtrix::hostapp::win32 {

using os::win32::wide;

WinShell::WinShell(HWND owner) : owner_(owner) {}

bool WinShell::open(const std::string& target) {
  const auto result = reinterpret_cast<INT_PTR>(
      ::ShellExecuteW(owner_, nullptr, wide(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  return result > 32;
}

bool WinShell::copy(const std::string& text) {
  const std::wstring copied = wide(text);
  const std::size_t bytes = (copied.size() + 1) * sizeof(wchar_t);
  const HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (!memory) return false;
  void* target = ::GlobalLock(memory);
  if (!target) {
    ::GlobalFree(memory);
    return false;
  }
  std::memcpy(target, copied.c_str(), bytes);
  ::GlobalUnlock(memory);
  if (!::OpenClipboard(owner_)) {
    ::GlobalFree(memory);
    return false;
  }
  const bool placed = ::EmptyClipboard() && ::SetClipboardData(CF_UNICODETEXT, memory);
  ::CloseClipboard();
  if (!placed) ::GlobalFree(memory);
  return placed;
}

bool WinShell::exists(const std::string& path) {
  const DWORD attributes = ::GetFileAttributesW(wide(path).c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

}
