#include "media/JpegDecoder.h"

#include <algorithm>
#include <cstring>

#include "core/render/Color.h"

namespace awtrix::media {

std::size_t JpegDecoder::input(JDEC* decoder, uint8_t* out, std::size_t count) {
  auto& self = *static_cast<JpegDecoder*>(decoder->device);
  count = std::min(count, self.size_ - self.position_);
  if (out) std::memcpy(out, self.bytes_ + self.position_, count);
  self.position_ += count;
  return count;
}

int JpegDecoder::output(JDEC* decoder, void* bitmap, JRECT* rect) {
  auto& self = *static_cast<JpegDecoder*>(decoder->device);
  const auto* pixel = static_cast<const uint16_t*>(bitmap);
  for (int y = rect->top; y <= rect->bottom; ++y)
    for (int x = rect->left; x <= rect->right; ++x)
      self.output_(self.target_, x, y, color::from565(*pixel++));
  return 1;
}

JRESULT JpegDecoder::prepare(const uint8_t* bytes, std::size_t size) {
  if (!bytes || !size) return JDR_INP;
  if (!workspace_.resize(TJPGD_WORKSPACE_SIZE, TJPGD_WORKSPACE_SIZE)) return JDR_MEM1;
  bytes_ = bytes;
  size_ = size;
  position_ = 0;
  return jd_prepare(&decoder_, input, workspace_.data(), workspace_.size(), this);
}

JRESULT JpegDecoder::decode(PixelOutput output, void* target, uint8_t scale) {
  output_ = output;
  target_ = target;
  decoder_.swap = 0;
  return jd_decomp(&decoder_, JpegDecoder::output, scale);
}

}
