#pragma once

#include <cstdint>
#include <new>

#include "core/render/Canvas.h"
#include "core/render/RenderPipeline.h"
#include "media/GifPlayer.h"
#include "media/IconRenderer.h"

namespace awtrix {

class DevicePageIcon : public IPageIcon {
 public:
  // Icon ids carry no extension, so try GIF first and fall back to JPG. A GIF may be wider than
  // 8 px and sets the page's icon width accordingly; a JPG is always 8x8.
  IconLoad begin(const std::string& iconId, int maxWidth, int maxHeight) override {
    clear();
    const auto result = gif_.open(iconId, maxWidth, maxHeight, false, 1);
    if (result == GifPlayer::OpenResult::kGood) {
      const bool transferred = gif_.takeStaticFrame(pixels_) || gif_.takeInitialFrame(pixels_);
      if (!transferred &&
          !pixels_.resize(static_cast<size_t>(gif_.width()) * gif_.height())) {
        clear();
        return IconLoad::kOom;
      }
      width_ = gif_.width();
      height_ = gif_.height();
      if (!transferred) {
        Canvas buf(width_, height_, pixels_.data());
        buf.clear();
      }
      return IconLoad::kGood;
    }
    if (result == GifPlayer::OpenResult::kOom || !pixels_.resize(8 * 8)) return IconLoad::kOom;
    Canvas buf(8, 8, pixels_.data());
    buf.clear();
    bool outOfMemory = false;
    if (!icon::draw(buf, iconId, 0, 0, &outOfMemory)) {
      clear();
      return outOfMemory ? IconLoad::kOom : IconLoad::kMissing;
    }
    width_ = height_ = 8;
    return IconLoad::kGood;
  }
  void clear() override {
    gif_.close();
    pixels_.clear();
    width_ = height_ = 0;
  }
  void advance(int64_t nowMs) override {
    if (gif_.active()) {
      Canvas buf(width_, height_, pixels_.data());
      gif_.render(buf, nowMs);
    }
  }
  void blit(Canvas& dst, int xOffset, int yOffset = 0) const override {
    for (int y = 0; y < height_; ++y)
      for (int x = 0; x < width_; ++x)
        dst.setPixel(x + xOffset, y + yOffset, pixels_[static_cast<size_t>(y) * width_ + x]);
  }
  int width() const override { return width_; }
  int height() const override { return height_; }
  std::unique_ptr<IPageIcon> create() const override {
    return std::unique_ptr<IPageIcon>(new (std::nothrow) DevicePageIcon());
  }

 private:
  media::PodBuffer<uint32_t> pixels_;
  GifPlayer gif_;
  int width_ = 0;
  int height_ = 0;
};

}
