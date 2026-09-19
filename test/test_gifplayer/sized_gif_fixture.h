#pragma once

#include <cstdint>
#include <vector>

// Solid red then green frames. Clearing the LZW dictionary after every pixel keeps this
// fixture builder independent of a GIF encoder and exercises arbitrary dimensions.
inline std::vector<uint8_t> sizedGif(int width, int height, bool animated = true) {
  std::vector<uint8_t> gif = {'G', 'I', 'F', '8', '9', 'a'};
  auto word = [&](int value) {
    gif.push_back(static_cast<uint8_t>(value));
    gif.push_back(static_cast<uint8_t>(value >> 8));
  };
  word(width);
  word(height);
  gif.insert(gif.end(), {0x81, 0, 0, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255});
  for (int frame = 0; frame < (animated ? 2 : 1); ++frame) {
    gif.insert(gif.end(), {0x21, 0xf9, 4, 4, 10, 0, 0, 0, 0x2c});
    word(0);
    word(0);
    word(width);
    word(height);
    gif.insert(gif.end(), {0, 2});
    std::vector<uint8_t> packed;
    unsigned bits = 0;
    int count = 0;
    auto code = [&](unsigned value) {
      bits |= value << count;
      count += 3;
      if (count >= 8) {
        packed.push_back(static_cast<uint8_t>(bits));
        bits >>= 8;
        count -= 8;
      }
    };
    for (int i = 0; i < width * height; ++i) {
      code(4);
      code(frame + 1);
    }
    code(5);
    if (count) packed.push_back(static_cast<uint8_t>(bits));
    for (size_t i = 0; i < packed.size();) {
      const size_t length = packed.size() - i < 255 ? packed.size() - i : 255;
      gif.push_back(static_cast<uint8_t>(length));
      gif.insert(gif.end(), packed.begin() + i, packed.begin() + i + length);
      i += length;
    }
    gif.push_back(0);
  }
  gif.push_back(0x3b);
  return gif;
}
