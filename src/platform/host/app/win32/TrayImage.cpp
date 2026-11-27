#include "platform/host/app/win32/TrayImage.h"

#include <cstring>

#include "platform/host/app/TrayIcon.h"

namespace awtrix::hostapp::win32 {

std::vector<uint32_t> iconPixels(HostState state, int size) {
  const std::vector<uint8_t> alpha = iconAlpha(size);
  const uint32_t color = iconColor(state) & 0xFFFFFF;
  std::vector<uint32_t> pixels(alpha.size(), 0);
  for (std::size_t i = 0; i < alpha.size(); ++i)
    if (alpha[i]) pixels[i] = static_cast<uint32_t>(alpha[i]) << 24 | color;
  return pixels;
}

HICON makeIcon(HostState state, int size) {
  const std::vector<uint32_t> pixels = iconPixels(state, size);
  if (pixels.empty()) return nullptr;
  BITMAPV5HEADER header{};
  header.bV5Size = sizeof header;
  header.bV5Width = size;
  header.bV5Height = -size;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000;
  header.bV5GreenMask = 0x0000FF00;
  header.bV5BlueMask = 0x000000FF;
  header.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  const HBITMAP color =
      ::CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!color) return nullptr;
  std::memcpy(bits, pixels.data(), pixels.size() * sizeof(uint32_t));
  const std::vector<uint8_t> blank(static_cast<std::size_t>((size + 15) / 16 * 2) * size, 0);
  const HBITMAP mask = ::CreateBitmap(size, size, 1, 1, blank.data());
  HICON icon = nullptr;
  if (mask) {
    ICONINFO info{TRUE, 0, 0, mask, color};
    icon = ::CreateIconIndirect(&info);
    ::DeleteObject(mask);
  }
  ::DeleteObject(color);
  return icon;
}

}
