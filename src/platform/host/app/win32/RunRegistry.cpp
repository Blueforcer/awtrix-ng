#include "platform/host/app/win32/RunRegistry.h"

#include <windows.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace awtrix::hostapp::win32 {
namespace {

constexpr wchar_t kChoice[] = L"StartAtLogin";

bool gone(LSTATUS status) { return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND; }

bool write(const std::wstring& key, const wchar_t* name, DWORD type, const void* data, DWORD size) {
  HKEY opened = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr) !=
      ERROR_SUCCESS)
    return false;
  const LSTATUS status = ::RegSetValueExW(opened, name, 0, type, static_cast<const BYTE*>(data), size);
  ::RegCloseKey(opened);
  return status == ERROR_SUCCESS;
}

std::optional<std::vector<BYTE>> read(const std::wstring& key, const wchar_t* name, DWORD types) {
  DWORD size = 0;
  if (::RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, types, nullptr, nullptr, &size) != ERROR_SUCCESS)
    return std::nullopt;
  std::vector<BYTE> data(size);
  if (::RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, types, nullptr, data.data(), &size) != ERROR_SUCCESS)
    return std::nullopt;
  data.resize(size);
  return data;
}

}

RunPlaces loginPlaces() {
  return {L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run",
          L"Software\\AWTRIX Script Host", L"AWTRIX Script Host"};
}

RunRegistry::RunRegistry(RunPlaces places) : places_(std::move(places)) {}

std::optional<bool> RunRegistry::choice() {
  const std::optional<std::vector<BYTE>> data = read(places_.settings, kChoice, RRF_RT_REG_DWORD);
  if (!data || data->size() != sizeof(DWORD)) return std::nullopt;
  DWORD value = 0;
  std::copy(data->begin(), data->end(), reinterpret_cast<BYTE*>(&value));
  return value != 0;
}

bool RunRegistry::remember(bool on) {
  const DWORD value = on ? 1 : 0;
  return write(places_.settings, kChoice, REG_DWORD, &value, sizeof value);
}

std::optional<std::wstring> RunRegistry::command() {
  const std::optional<std::vector<BYTE>> data = read(places_.run, places_.name.c_str(), RRF_RT_REG_SZ);
  if (!data) return std::nullopt;
  std::wstring text(reinterpret_cast<const wchar_t*>(data->data()), data->size() / sizeof(wchar_t));
  while (!text.empty() && text.back() == L'\0') text.pop_back();
  return text;
}

bool RunRegistry::setCommand(const std::wstring& command) {
  return write(places_.run, places_.name.c_str(), REG_SZ, command.c_str(),
               static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
}

bool RunRegistry::removeCommand() {
  return gone(::RegDeleteKeyValueW(HKEY_CURRENT_USER, places_.run.c_str(), places_.name.c_str()));
}

bool RunRegistry::blockedByWindows() {
  const std::optional<std::vector<BYTE>> data = read(places_.approved, places_.name.c_str(), RRF_RT_REG_BINARY);
  return data && !data->empty() && ((*data)[0] & 1) != 0;
}

bool RunRegistry::unblock() {
  return gone(::RegDeleteKeyValueW(HKEY_CURRENT_USER, places_.approved.c_str(), places_.name.c_str()));
}

}
