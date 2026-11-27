#include "platform/host/Service.h"

#include <windows.h>

#include <cstdio>

#include "platform/host/win32/Wide.h"

namespace awtrix::os {
namespace {

constexpr DWORD kStopHintMs = 30000;

std::wstring g_name;
std::atomic<bool>* g_stop = nullptr;
const ServiceBody* g_body = nullptr;
SERVICE_STATUS_HANDLE g_status = nullptr;
DWORD g_checkpoint = 1;
int g_exit = 0;

void report(DWORD state, int exitCode = 0, DWORD waitHint = 0) {
  SERVICE_STATUS status{};
  status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  status.dwCurrentState = state;
  status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
  status.dwWin32ExitCode = exitCode ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR;
  status.dwServiceSpecificExitCode = static_cast<DWORD>(exitCode);
  status.dwWaitHint = waitHint;
  status.dwCheckPoint = state == SERVICE_RUNNING || state == SERVICE_STOPPED ? 0 : g_checkpoint++;
  ::SetServiceStatus(g_status, &status);
}

DWORD WINAPI control(DWORD code, DWORD, void*, void*) {
  if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
    report(SERVICE_STOP_PENDING, 0, kStopHintMs);
    if (g_stop) g_stop->store(true);
  }
  return NO_ERROR;
}

void WINAPI serviceMain(DWORD, LPWSTR*) {
  g_status = ::RegisterServiceCtrlHandlerExW(g_name.c_str(), control, nullptr);
  if (!g_status) return;
  report(SERVICE_START_PENDING, 0, 10000);
  report(SERVICE_RUNNING);
  g_exit = (*g_body)(true);
  report(SERVICE_STOPPED, g_exit);
}

}

int runService(const std::string& name, std::atomic<bool>& stop, const ServiceBody& body) {
  g_name = win32::wide(name);
  g_stop = &stop;
  g_body = &body;
  SERVICE_TABLE_ENTRYW table[] = {{g_name.data(), serviceMain}, {nullptr, nullptr}};
  if (::StartServiceCtrlDispatcherW(table)) return g_exit;
  if (::GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) return body(false);
  return 1;
}

bool logToFile(const std::string& path, unsigned long keepBytes) {
  const std::wstring file = win32::wide(path);
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if (::GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info) &&
      (info.nFileSizeHigh > 0 || info.nFileSizeLow > keepBytes))
    ::MoveFileExW(file.c_str(), (file + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
  return _wfreopen(file.c_str(), L"a", stdout) && _wfreopen(file.c_str(), L"a", stderr);
}

}
