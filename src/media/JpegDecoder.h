#pragma once

#include <cstddef>
#include <cstdint>
#include <tjpgd.h>

#include "media/PodBuffer.h"

namespace awtrix::media {

class JpegDecoder {
 public:
  using PixelOutput = void (*)(void* target, int x, int y, uint32_t color);

  JpegDecoder() = default;
  JpegDecoder(const JpegDecoder&) = delete;
  JpegDecoder& operator=(const JpegDecoder&) = delete;

  JRESULT prepare(const uint8_t* bytes, std::size_t size);
  JRESULT decode(PixelOutput output, void* target, uint8_t scale = 0);
  int width() const { return decoder_.width; }
  int height() const { return decoder_.height; }
  int mcuWidth() const { return decoder_.msx * 8; }
  int mcuHeight() const { return decoder_.msy * 8; }

 private:
  static std::size_t input(JDEC* decoder, uint8_t* out, std::size_t count);
  static int output(JDEC* decoder, void* bitmap, JRECT* rect);

  JDEC decoder_{};
  PodBuffer<uint8_t> workspace_;
  const uint8_t* bytes_ = nullptr;
  std::size_t size_ = 0, position_ = 0;
  PixelOutput output_ = nullptr;
  void* target_ = nullptr;
};

}
