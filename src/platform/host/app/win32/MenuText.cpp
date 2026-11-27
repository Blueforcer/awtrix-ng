#include "platform/host/app/win32/MenuText.h"

#include "platform/host/win32/Wide.h"

namespace awtrix::hostapp::win32 {

std::wstring menuLabel(std::string_view text) {
  std::wstring label;
  for (const wchar_t c : os::win32::wide(text)) {
    label += c;
    if (c == L'&') label += L'&';
  }
  return label;
}

std::wstring tipText(std::string_view text) {
  std::wstring tip = os::win32::wide(text);
  if (tip.size() <= kTipChars) return tip;
  tip.resize(kTipChars);
  if (tip.back() >= 0xD800 && tip.back() <= 0xDBFF) tip.pop_back();
  return tip;
}

}
