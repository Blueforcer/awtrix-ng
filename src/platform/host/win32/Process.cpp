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

bool createProcess(const std::vector<std::string>& args, HANDLE input, HANDLE output, DWORD flags,
                   PROCESS_INFORMATION& process) {
  std::wstring line;
  for (const std::string& arg : args) quote(line, win32::wide(arg));
  const std::wstring program = win32::wide(args.front());
  HANDLE inherited[2];
  DWORD count = 0;
  if (input) inherited[count++] = input;
  if (output) inherited[count++] = output;
  SIZE_T size = 0;
  ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::string storage(size, '\0');
  auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
  if (!::InitializeProcThreadAttributeList(attributes, 1, 0, &size)) return false;
  const bool listed = count == 0 || ::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                                inherited, count * sizeof(HANDLE), nullptr, nullptr);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof startup;
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input;
  startup.StartupInfo.hStdOutput = output;
  startup.StartupInfo.hStdError = output;
  startup.lpAttributeList = attributes;
  const bool created = listed && ::CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, count > 0,
                                                  flags | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                                  &startup.StartupInfo, &process);
  ::DeleteProcThreadAttributeList(attributes);
  return created;
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
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const HANDLE out = ::CreateFileW(win32::wide(log).c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inherit, OPEN_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool withLog = out != INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION process{};
  const bool created =
      createProcess(args, nullptr, withLog ? out : nullptr,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT, process);
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

bool TiedProcess::start(const std::vector<std::string>& args) {
  if (args.empty() || running()) return false;
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE readEnd = nullptr, writeEnd = nullptr;
  if (!::CreatePipe(&readEnd, &writeEnd, &inherit, 0)) return false;
  ::SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);
  const HANDLE nul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool withNul = nul != INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION process{};
  const bool created = createProcess(args, readEnd, withNul ? nul : nullptr,
                                     CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, process);
  ::CloseHandle(readEnd);
  if (withNul) ::CloseHandle(nul);
  if (!created) {
    ::CloseHandle(writeEnd);
    return false;
  }
  ::CloseHandle(process.hThread);
  id_ = static_cast<long>(process.dwProcessId);
  handle_ = process.hProcess;
  input_ = reinterpret_cast<std::intptr_t>(writeEnd);
  return true;
}

bool TiedProcess::ended(int& status) {
  if (id_ <= 0 || !handle_) return false;
  if (::WaitForSingleObject(handle_, 0) != WAIT_OBJECT_0) return false;
  DWORD code = 0;
  ::GetExitCodeProcess(handle_, &code);
  status = static_cast<int>(code);
  ::CloseHandle(std::exchange(handle_, nullptr));
  id_ = -1;
  closeInput();
  return true;
}

bool TiedProcess::wait(int timeoutMs, int& status) {
  if (!handle_ || ::WaitForSingleObject(handle_, static_cast<DWORD>(timeoutMs)) != WAIT_OBJECT_0) return false;
  return ended(status);
}

void TiedProcess::closeInput() {
  if (input_ != -1) ::CloseHandle(reinterpret_cast<HANDLE>(std::exchange(input_, -1)));
}

void TiedProcess::stop(int graceMs) {
  closeInput();
  int status = 0;
  if (!handle_ || wait(graceMs, status)) return;
  ::TerminateProcess(handle_, 1);
  ::WaitForSingleObject(handle_, INFINITE);
  ::CloseHandle(std::exchange(handle_, nullptr));
  id_ = -1;
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

void stopAtInputEnd(std::atomic<bool>& stop) {
  std::thread([&stop] {
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    char buffer[256];
    DWORD count = 0;
    while (input && input != INVALID_HANDLE_VALUE && ::ReadFile(input, buffer, sizeof buffer, &count, nullptr) &&
           count > 0) {
    }
    stop.store(true);
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
