#include "platform/host/Files.h"

#include <aclapi.h>
#include <fcntl.h>
#include <io.h>
#include <sddl.h>
#include <windows.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>
#include <utility>

#include "platform/host/win32/Wide.h"

namespace awtrix::os {
namespace win32 {

void setErrno(DWORD error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
      errno = ENOENT;
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
      errno = EACCES;
      break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      errno = EEXIST;
      break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      errno = ENOSPC;
      break;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
      errno = EBUSY;
      break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      errno = ENOMEM;
      break;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
      errno = EINVAL;
      break;
    case ERROR_DIRECTORY:
      errno = ENOTDIR;
      break;
    case ERROR_CANT_RESOLVE_FILENAME:
    case ERROR_STOPPED_ON_SYMLINK:
      errno = ELOOP;
      break;
    default:
      errno = EIO;
  }
}

namespace {

std::wstring ownSid() {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::wstring sid;
  if (size > 0) {
    std::string buffer(size, '\0');
    LPWSTR text = nullptr;
    if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size) &&
        ::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &text)) {
      sid = text;
      ::LocalFree(text);
    }
  }
  ::CloseHandle(token);
  return sid;
}

const std::wstring& privateSddl(bool directory) {
  static const std::wstring sid = ownSid();
  static const std::wstring forDirectories =
      L"D:P(A;OICI;FA;;;" + sid + L")(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
  static const std::wstring forFiles = L"D:P(A;;FA;;;" + sid + L")(A;;FA;;;SY)(A;;FA;;;BA)";
  return directory ? forDirectories : forFiles;
}

}

PrivateSecurity::PrivateSecurity(bool directory) {
  const std::wstring& sddl = privateSddl(directory);
  if (sddl.find(L";;;)") != std::wstring::npos) return;
  if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
    descriptor_ = nullptr;
  attributes_.nLength = sizeof attributes_;
  attributes_.lpSecurityDescriptor = descriptor_;
  attributes_.bInheritHandle = FALSE;
}

PrivateSecurity::~PrivateSecurity() {
  if (descriptor_) ::LocalFree(descriptor_);
}

}

namespace {

using win32::wide;

constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
std::atomic<unsigned long> g_sequence{0};

bool isLink(const std::wstring& path) {
  const DWORD attributes = ::GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool writeAll(HANDLE file, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const char*>(data);
  while (size > 0) {
    const DWORD chunk = size > 0x40000000u ? 0x40000000u : static_cast<DWORD>(size);
    DWORD written = 0;
    if (!::WriteFile(file, bytes, chunk, &written, nullptr)) {
      win32::setErrno(::GetLastError());
      return false;
    }
    if (written == 0) {
      errno = EIO;
      return false;
    }
    bytes += written;
    size -= written;
  }
  return true;
}

bool moveOver(const std::wstring& from, const std::wstring& to) {
  for (int attempt = 0;; ++attempt) {
    if (::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    const DWORD error = ::GetLastError();
    if (attempt >= 50 || (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)) {
      win32::setErrno(error);
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

}

bool readFile(const std::string& path, std::string& out, std::size_t maximum) {
  const HANDLE file = ::CreateFileW(wide(path).c_str(), GENERIC_READ, kShareAll, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    win32::setErrno(::GetLastError());
    return false;
  }
  out.clear();
  char buffer[512];
  bool ok = true;
  for (;;) {
    DWORD count = 0;
    if (!::ReadFile(file, buffer, sizeof buffer, &count, nullptr)) {
      win32::setErrno(::GetLastError());
      ok = false;
      break;
    }
    if (count == 0) break;
    if (out.size() + count > maximum) {
      errno = EFBIG;
      ok = false;
      break;
    }
    out.append(buffer, count);
  }
  ::CloseHandle(file);
  return ok;
}

bool replaceFile(const std::string& path, std::string_view text) {
  const std::wstring target = wide(path);
  win32::PrivateSecurity security(false);
  HANDLE file = INVALID_HANDLE_VALUE;
  std::wstring temporary;
  for (int attempt = 0; attempt < 16 && file == INVALID_HANDLE_VALUE; ++attempt) {
    temporary = target + L".tmp." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(++g_sequence);
    file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, security.attributes(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (file == INVALID_HANDLE_VALUE && ::GetLastError() != ERROR_FILE_EXISTS) break;
  }
  if (file == INVALID_HANDLE_VALUE) {
    win32::setErrno(::GetLastError());
    return false;
  }
  bool ok = writeAll(file, text.data(), text.size());
  if (ok && !::FlushFileBuffers(file)) {
    win32::setErrno(::GetLastError());
    ok = false;
  }
  ::CloseHandle(file);
  if (ok) ok = moveOver(temporary, target);
  if (!ok) {
    const int error = errno;
    ::DeleteFileW(temporary.c_str());
    errno = error;
  }
  return ok;
}

bool syncDirectory(const std::string& path) {
  const DWORD attributes = ::GetFileAttributesW(wide(path).c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    win32::setErrno(::GetLastError());
    return false;
  }
  if (!(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    errno = ENOTDIR;
    return false;
  }
  return true;
}

bool ensurePrivateDirectory(const std::string& path) {
  const std::wstring directory = wide(path);
  win32::PrivateSecurity security(true);
  if (!security.valid()) {
    errno = EPERM;
    return false;
  }
  if (::CreateDirectoryW(directory.c_str(), security.attributes())) return true;
  if (::GetLastError() != ERROR_ALREADY_EXISTS) {
    win32::setErrno(::GetLastError());
    return false;
  }
  const DWORD attributes = ::GetFileAttributesW(directory.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    errno = EPERM;
    return false;
  }
  BOOL present = FALSE, defaulted = FALSE;
  PACL dacl = nullptr;
  if (!::GetSecurityDescriptorDacl(security.descriptor(), &present, &dacl, &defaulted) || !present) {
    errno = EPERM;
    return false;
  }
  const DWORD status = ::SetNamedSecurityInfoW(const_cast<LPWSTR>(directory.c_str()), SE_FILE_OBJECT,
                                               DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                               nullptr, nullptr, dacl, nullptr);
  if (status != ERROR_SUCCESS) {
    win32::setErrno(status);
    return false;
  }
  return true;
}

bool removeFile(const std::string& path) {
  if (::DeleteFileW(wide(path).c_str())) return true;
  win32::setErrno(::GetLastError());
  return false;
}

void keepNewFilesPrivate() {}

int openForReading(const std::string& path) {
  const HANDLE file = ::CreateFileW(wide(path).c_str(), GENERIC_READ, kShareAll, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    win32::setErrno(::GetLastError());
    return -1;
  }
  BY_HANDLE_FILE_INFORMATION info{};
  if (!::GetFileInformationByHandle(file, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
    ::CloseHandle(file);
    errno = ELOOP;
    return -1;
  }
  const int fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(file), _O_RDONLY | _O_BINARY);
  if (fd < 0) {
    ::CloseHandle(file);
    errno = EMFILE;
  }
  return fd;
}

OutFile::~OutFile() { close(); }

OutFile::OutFile(OutFile&& other) noexcept
    : handle_(std::exchange(other.handle_, -1)), exists_(other.exists_), full_(other.full_) {}

OutFile& OutFile::operator=(OutFile&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = std::exchange(other.handle_, -1);
    exists_ = other.exists_;
    full_ = other.full_;
  }
  return *this;
}

OutFile OutFile::create(const std::string& path, bool exclusive) {
  OutFile file;
  const std::wstring name = wide(path);
  if (isLink(name)) {
    errno = ELOOP;
    return file;
  }
  win32::PrivateSecurity security(false);
  const HANDLE handle = ::CreateFileW(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                      security.attributes(), exclusive ? CREATE_NEW : CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    const DWORD error = ::GetLastError();
    win32::setErrno(error);
    file.exists_ = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
    file.full_ = error == ERROR_DISK_FULL || error == ERROR_HANDLE_DISK_FULL;
    return file;
  }
  file.handle_ = reinterpret_cast<std::intptr_t>(handle);
  return file;
}

bool OutFile::write(const void* data, std::size_t size) {
  if (handle_ < 0) return false;
  if (writeAll(reinterpret_cast<HANDLE>(handle_), data, size)) return true;
  full_ = errno == ENOSPC;
  return false;
}

bool OutFile::sync() {
  if (handle_ < 0) return false;
  if (::FlushFileBuffers(reinterpret_cast<HANDLE>(handle_))) return true;
  win32::setErrno(::GetLastError());
  return false;
}

bool OutFile::close() {
  if (handle_ < 0) return false;
  return ::CloseHandle(reinterpret_cast<HANDLE>(std::exchange(handle_, -1))) != 0;
}

FileLock::~FileLock() {
  if (handle_ >= 0) ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
}

bool FileLock::acquire(const std::string& path) {
  win32::PrivateSecurity security(false);
  const HANDLE file = ::CreateFileW(wide(path).c_str(), GENERIC_READ | GENERIC_WRITE, kShareAll, security.attributes(),
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    win32::setErrno(::GetLastError());
    return false;
  }
  OVERLAPPED whole{};
  if (!::LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &whole)) {
    win32::setErrno(::GetLastError());
    ::CloseHandle(file);
    return false;
  }
  if (handle_ >= 0) ::CloseHandle(reinterpret_cast<HANDLE>(handle_));
  handle_ = reinterpret_cast<std::intptr_t>(file);
  return true;
}

}
