#include "platform/host/Process.h"

#include <windows.h>

#include <thread>
#include <utility>

#include "platform/host/win32/Wide.h"

namespace awtrix::os {
namespace {

std::atomic<bool>* g_stop = nullptr;

std::wstring stopEventName(DWORD process) { return L"Local\\awtrix-stop-" + std::to_wstring(process); }

HANDLE stopEvent(DWORD process) { return ::CreateEventW(nullptr, TRUE, FALSE, stopEventName(process).c_str()); }

HANDLE childJob() {
  static const HANDLE job = [] {
    const HANDLE created = ::CreateJobObjectW(nullptr, nullptr);
    if (!created) return HANDLE{};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!::SetInformationJobObject(created, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
      ::CloseHandle(created);
      return HANDLE{};
    }
    return created;
  }();
  return job;
}

void quote(std::wstring& line, const std::wstring& arg) {
  if (!line.empty()) line += L' ';
  if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    line += arg;
    return;
  }
  line += L'"';
  for (std::size_t i = 0;; ++i) {
    std::size_t slashes = 0;
    while (i < arg.size() && arg[i] == L'\\') {
      ++i;
      ++slashes;
    }
    if (i == arg.size()) {
      line.append(slashes * 2, L'\\');
      break;
    }
    if (arg[i] == L'"') line.append(slashes * 2 + 1, L'\\');
    else line.append(slashes, L'\\');
    line += arg[i];
  }
  line += L'"';
}

BOOL WINAPI consoleRequest(DWORD) {
  if (g_stop) g_stop->store(true);
  return TRUE;
}

}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : id_(std::exchange(other.id_, -1)), handle_(std::exchange(other.handle_, nullptr)) {}

ChildProcess& ChildProcess::operator=(ChildProcess&& other) noexcept {
  if (this != &other) {
    stop();
    id_ = std::exchange(other.id_, -1);
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

bool ChildProcess::start(const std::vector<std::string>& args, const std::string& log) {
  if (args.empty() || running()) return false;
  std::wstring line;
  for (const std::string& arg : args) quote(line, win32::wide(arg));
  const std::wstring program = win32::wide(args.front());

  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const HANDLE out = ::CreateFileW(win32::wide(log).c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inherit, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
  SIZE_T size = 0;
  ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::string storage(size, '\0');
  auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  HANDLE inherited[] = {out};
  const bool withLog = out != INVALID_HANDLE_VALUE;
  const bool listed = ::InitializeProcThreadAttributeList(attributes, 1, 0, &size) &&
                      (!withLog || ::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                               inherited, sizeof inherited, nullptr, nullptr));
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof startup;
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdOutput = withLog ? out : nullptr;
  startup.StartupInfo.hStdError = withLog ? out : nullptr;
  startup.lpAttributeList = listed ? attributes : nullptr;
  PROCESS_INFORMATION process{};
  const DWORD flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT |
                      (listed ? EXTENDED_STARTUPINFO_PRESENT : 0);
  const bool created = listed && ::CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, withLog, flags,
                                                  nullptr, nullptr, &startup.StartupInfo, &process);
  if (listed) ::DeleteProcThreadAttributeList(attributes);
  if (withLog) ::CloseHandle(out);
  if (!created) return false;
  const HANDLE job = childJob();
  if (!job || !::AssignProcessToJobObject(job, process.hProcess)) {
    ::TerminateProcess(process.hProcess, 1);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return false;
  }
  ::ResumeThread(process.hThread);
  ::CloseHandle(process.hThread);
  id_ = static_cast<long>(process.dwProcessId);
  handle_ = process.hProcess;
  return true;
}

bool ChildProcess::ended(int& status) {
  if (id_ <= 0 || !handle_) return false;
  if (::WaitForSingleObject(handle_, 0) != WAIT_OBJECT_0) return false;
  DWORD code = 0;
  ::GetExitCodeProcess(handle_, &code);
  status = static_cast<int>(code);
  ::CloseHandle(std::exchange(handle_, nullptr));
  id_ = -1;
  return true;
}

void ChildProcess::stop(int graceMs) {
  if (id_ <= 0 || !handle_) return;
  const DWORD id = static_cast<DWORD>(std::exchange(id_, -1));
  const HANDLE process = std::exchange(handle_, nullptr);
  if (const HANDLE event = stopEvent(id)) {
    ::SetEvent(event);
    const bool ended = ::WaitForSingleObject(process, static_cast<DWORD>(graceMs)) == WAIT_OBJECT_0;
    ::CloseHandle(event);
    if (ended) {
      ::CloseHandle(process);
      return;
    }
  }
  ::TerminateProcess(process, 1);
  ::WaitForSingleObject(process, INFINITE);
  ::CloseHandle(process);
}

void stopOnRequest(std::atomic<bool>& stop) {
  g_stop = &stop;
  ::SetConsoleCtrlHandler(consoleRequest, TRUE);
  const HANDLE event = stopEvent(::GetCurrentProcessId());
  if (!event) return;
  std::thread([event] {
    ::WaitForSingleObject(event, INFINITE);
    if (g_stop) g_stop->store(true);
  }).detach();
}

void lowerThreadPriority(int nice) {
  if (nice > 0) ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
}

std::string hostName() {
  wchar_t name[256] = {};
  DWORD size = sizeof name / sizeof name[0];
  if (!::GetComputerNameExW(ComputerNameDnsHostname, name, &size) || !name[0]) return {};
  return win32::narrow(name);
}

}
