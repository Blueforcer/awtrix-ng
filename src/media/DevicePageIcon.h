#pragma once

#include <cstdint>

#include "core/render/Canvas.h"
#include "core/render/RenderPipeline.h"
#include "media/GifPlayer.h"
#include "media/IconRenderer.h"

namespace awtrix {

class DevicePageIcon : public IPageIcon {
 public:
  // Icon ids carry no extension, so try GIF first and fall back to JPG. A GIF may be wider than
  // 8 px and sets the page's icon width accordingly; a JPG is always 8x8.
  bool begin(const std::string& iconId) override {
    buf_.clear(0x000000u);
    width_ = 0;
    if (gif_.open(iconId, false, 1) ==
        GifPlayer::OpenResult::kGood) {
      width_ = gif_.width();
      return true;
    }
    if (!icon::draw(buf_, iconId, 0, 0)) return false;
    width_ = 8;
    return true;
  }
  void clear() override {
    gif_.close();
    width_ = 0;
  }
  void advance(int64_t nowMs) override {
    if (gif_.active()) gif_.render(buf_, nowMs);
  }
  void blit(Canvas& dst, int xOffset) const override {
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < width_; ++x) dst.setPixel(x + xOffset, y, buf_.getPixel(x, y));
  }
  int width() const override { return width_; }

 private:
  Canvas buf_{32, 8};
  GifPlayer gif_;
  int width_ = 0;
};

}
