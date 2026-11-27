#include "platform/host/app/TrayIcon.h"

#include <cstddef>

namespace awtrix::hostapp {
namespace {

constexpr int kMaxSize = 1024;
constexpr uint32_t kUpColor = 0x7DC242;
constexpr uint32_t kDownColor = 0x8C8C8C;

constexpr const char* kPattern[kIconLeds] = {
    ".######.",
    "#......#",
    "#..#...#",
    "#..#...#",
    "#..###.#",
    "#......#",
    "#......#",
    ".######.",
};

}

bool iconLit(int column, int row) {
  return column >= 0 && column < kIconLeds && row >= 0 && row < kIconLeds && kPattern[row][column] == '#';
}

std::vector<uint8_t> iconAlpha(int size) {
  if (size < kIconLeds || size > kMaxSize) return {};
  std::vector<uint8_t> alpha(static_cast<std::size_t>(size) * size, 0);
  const int cell = size / kIconLeds;
  const int margin = (size - cell * kIconLeds) / 2;
  const int led = cell >= 4 ? cell - 1 : cell;
  for (int row = 0; row < kIconLeds; ++row)
    for (int column = 0; column < kIconLeds; ++column) {
      if (!iconLit(column, row)) continue;
      for (int y = 0; y < led; ++y)
        for (int x = 0; x < led; ++x)
          alpha[static_cast<std::size_t>(margin + row * cell + y) * size + margin + column * cell + x] = 255;
    }
  return alpha;
}

uint32_t iconColor(HostState state) {
  return state == HostState::Starting || state == HostState::Running ? kUpColor : kDownColor;
}

}
