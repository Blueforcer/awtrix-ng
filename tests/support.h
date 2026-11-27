#pragma once

#ifdef __cplusplus
#if defined(_WIN32)
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <thread>

namespace awtrix::test {

inline std::size_t listedThreads() {
#if defined(_WIN32)
  std::size_t count = 0;
  const HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return count;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof entry;
  for (BOOL more = ::Thread32First(snapshot, &entry); more; more = ::Thread32Next(snapshot, &entry))
    if (entry.th32OwnerProcessID == ::GetCurrentProcessId()) ++count;
  ::CloseHandle(snapshot);
  return count;
#elif defined(__APPLE__)
  thread_act_array_t list = nullptr;
  mach_msg_type_number_t count = 0;
  if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS) return 0;
  for (mach_msg_type_number_t i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), list[i]);
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list), sizeof(*list) * count);
  return count;
#else
  const std::filesystem::directory_iterator tasks("/proc/self/task");
  return static_cast<std::size_t>(std::distance(begin(tasks), end(tasks)));
#endif
}

// The threads of this process, once their number has held for 200 ms: join() returns before the kernel
// takes the thread off the list, and under load that took long enough to fail a test.
inline std::size_t threadCount() {
  const auto listed = [] { return listedThreads(); };
  std::size_t count = listed();
  auto since = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - since < std::chrono::milliseconds(200)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::size_t now = listed();
    if (now != count) {
      count = now;
      since = std::chrono::steady_clock::now();
    }
  }
  return count;
}

inline bool privateMode(const std::filesystem::path& path, [[maybe_unused]] int mode) {
#if defined(_WIN32)
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (::GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                              nullptr, nullptr, &dacl, nullptr, &descriptor) != ERROR_SUCCESS)
    return false;
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  bool ok = dacl && ::GetSecurityDescriptorControl(descriptor, &control, &revision) && (control & SE_DACL_PROTECTED);
  HANDLE token = nullptr;
  std::string user(256, '\0');
  DWORD size = 0;
  ok = ok && ::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token) &&
       ::GetTokenInformation(token, TokenUser, user.data(), static_cast<DWORD>(user.size()), &size);
  if (token) ::CloseHandle(token);
  for (DWORD i = 0; ok && i < dacl->AceCount; ++i) {
    void* ace = nullptr;
    if (!::GetAce(dacl, i, &ace)) return false;
    const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(ace);
    if (allowed->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
    PSID sid = const_cast<DWORD*>(&allowed->SidStart);
    ok = ::EqualSid(sid, reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid) ||
         ::IsWellKnownSid(sid, WinLocalSystemSid) || ::IsWellKnownSid(sid, WinBuiltinAdministratorsSid);
  }
  ::LocalFree(descriptor);
  return ok;
#else
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0 && static_cast<int>(st.st_mode & 0777) == mode;
#endif
}

inline int& failures() {
  static int value = 0;
  return value;
}

inline std::atomic<unsigned>& passed() {
  static std::atomic<unsigned> value{0};
  return value;
}

inline void report(std::string_view message) {
  std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(message.size()), message.data());
}

// Collect independent failures so a suite can report all affected behavior.
inline void check(bool condition, std::string_view message = "condition failed") {
  if (condition) return;
  report(message);
  ++failures();
}

// Stop when continuing would use a failed setup or an invalid test resource.
inline void require(bool condition, std::string_view message = "condition failed") {
  if (!condition) {
    report(message);
    std::exit(EXIT_FAILURE);
  }
  ++passed();
}

#if !defined(_WIN32)
inline pid_t endedProcess() {
  const pid_t pid = ::fork();
  if (pid == 0) ::_exit(0);
  require(pid > 0 && ::waitpid(pid, nullptr, 0) == pid, "the pid of a process that ended");
  return pid;
}
#endif

inline int finish(const char* name) {
  if (failures()) {
    std::fprintf(stderr, "%s: %d failure(s)\n", name, failures());
    return EXIT_FAILURE;
  }
  std::printf("%s: ok\n", name);
  return EXIT_SUCCESS;
}

}

#else

#include <stdio.h>
#include <stdlib.h>

static int awtrix_test_failures;
static unsigned awtrix_test_passed;

static inline void awtrix_test_check(int condition, const char* expression,
                                    const char* file, int line, const char* context) {
  if (condition) return;
  fprintf(stderr, "%s: %s:%d: CHECK failed: %s\n", context, file, line, expression);
  ++awtrix_test_failures;
}

static inline void awtrix_test_require(int condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    exit(EXIT_FAILURE);
  }
  ++awtrix_test_passed;
}

#define AWTRIX_TEST_CHECK(condition, context) \
  awtrix_test_check(!!(condition), #condition, __FILE__, __LINE__, context)

#endif
