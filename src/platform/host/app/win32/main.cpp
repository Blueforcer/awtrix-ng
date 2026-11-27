#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <filesystem>
#include <string>

#include "platform/host/app/StatusFetch.h"
#include "platform/host/app/StatusMenu.h"
#include "platform/host/app/win32/HostLayout.h"
#include "platform/host/app/win32/RunRegistry.h"
#include "platform/host/app/win32/StartAtLogin.h"
#include "platform/host/app/win32/TiedHost.h"
#include "platform/host/app/win32/TrayController.h"
#include "platform/host/app/win32/TrayWindow.h"
#include "platform/host/app/win32/WinShell.h"
#include "platform/linux/hostd/Contract.h"

namespace fs = std::filesystem;
using namespace awtrix::hostapp;
using namespace awtrix::hostapp::win32;

namespace {

constexpr wchar_t kProduct[] = L"AWTRIX Script Host";

fs::path programPath() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0) return {};
    if (length < path.size()) {
      path.resize(length);
      return path;
    }
    path.resize(path.size() * 2);
  }
}

fs::path dataFolder() {
  PWSTR local = nullptr;
  fs::path folder;
  if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)))
    folder = fs::path(local) / kProduct;
  ::CoTaskMemFree(local);
  return folder;
}

bool firstInstance(HANDLE& mutex) {
  mutex = ::CreateMutexW(nullptr, FALSE, L"Local\\AWTRIX Script Host");
  const DWORD error = ::GetLastError();
  return error != ERROR_ALREADY_EXISTS && error != ERROR_ACCESS_DENIED;
}

int runTray(HINSTANCE instance) {
  const fs::path program = programPath();
  const HostLayout layout{program.parent_path(), dataFolder()};
  if (layout.data.empty()) {
    ::MessageBoxW(nullptr, L"Your local app data folder was not found.", kProduct, MB_ICONERROR);
    return 1;
  }
  TrayWindow window(instance);
  if (!window.created()) return 1;
  TiedHost host([&window] { window.signalHostEnded(); });
  RunRegistry registry(loginPlaces());
  StartAtLogin login(registry, L"\"" + program.wstring() + L"\"");
  login.settle();
  WinShell shell(window.handle());
  TrayController controller(host, layout.hostArgs(), window, shell, login,
                            [](int port, std::string& body) { return fetchStatus(port, body); },
                            {AWTRIX_NG_VERSION, awtrix::hostd::kDefaultPort, layout.log().u8string()});
  return window.run(controller);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  HANDLE mutex = nullptr;
  const bool first = firstInstance(mutex);
  const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  int code = 0;
  if (first)
    code = runTray(instance);
  else
    WinShell(nullptr).open(statusPageUrl(awtrix::hostd::kDefaultPort));
  if (SUCCEEDED(com)) ::CoUninitialize();
  if (mutex) ::CloseHandle(mutex);
  return code;
}
