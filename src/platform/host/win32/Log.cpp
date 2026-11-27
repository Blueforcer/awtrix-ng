#include "platform/host/Log.h"

#include <windows.h>

#include <cstdio>

#include "platform/host/win32/Wide.h"

namespace awtrix::os {

bool logToFile(const std::string& path, unsigned long keepBytes) {
  const std::wstring file = win32::wide(path);
  WIN32_FILE_ATTRIBUTE_DATA info{};
  if (::GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info) &&
      (info.nFileSizeHigh > 0 || info.nFileSizeLow > keepBytes))
    ::MoveFileExW(file.c_str(), (file + L".1").c_str(), MOVEFILE_REPLACE_EXISTING);
  return _wfreopen(file.c_str(), L"a", stdout) && _wfreopen(file.c_str(), L"a", stderr);
}

}
