#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace awtrix::os::win32 {

inline std::wstring wide(std::string_view text) {
  if (text.empty()) return {};
  const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

inline std::string narrow(std::wstring_view text) {
  if (text.empty()) return {};
  const int size =
      ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string out(static_cast<std::size_t>(size), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
  return out;
}

void setErrno(DWORD error);

class PrivateSecurity {
 public:
  explicit PrivateSecurity(bool directory);
  ~PrivateSecurity();
  PrivateSecurity(const PrivateSecurity&) = delete;
  PrivateSecurity& operator=(const PrivateSecurity&) = delete;

  bool valid() const { return descriptor_ != nullptr; }
  SECURITY_ATTRIBUTES* attributes() { return valid() ? &attributes_ : nullptr; }
  PSECURITY_DESCRIPTOR descriptor() const { return descriptor_; }

 private:
  PSECURITY_DESCRIPTOR descriptor_ = nullptr;
  SECURITY_ATTRIBUTES attributes_{};
};

}
