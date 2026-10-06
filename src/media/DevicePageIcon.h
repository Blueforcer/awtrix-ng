#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <string>

#include "core/render/Canvas.h"
#include "core/render/RenderPipeline.h"
#include "media/GifPlayer.h"
#include "media/IconRenderer.h"
#include "media/ImageInfo.h"

namespace awtrix {

class DevicePageIcon : public IPageIcon {
 public:
  // Icon ids carry no extension, so try GIF first and fall back to JPG. A GIF may be wider than
  // 8 px and sets the page's icon width accordingly; a JPG is always 8x8.
  IconLoad begin(const std::string& iconId, int maxWidth, int maxHeight) override {
    if (icons::parse(iconId).remote()) {
      clear();
      return IconLoad::kMissing;
    }
    return load(iconId, maxWidth, maxHeight);
  }
  IconLoad beginNative(std::string_view iconId, int maxWidth, int maxHeight) override {
    return beginNativeBounded(iconId, maxWidth, maxHeight, std::numeric_limits<std::size_t>::max());
  }
  IconLoad beginNativeBounded(std::string_view id, int maxWidth, int maxHeight,
                              std::size_t maxResidentBytes) override {
    clear();
    const icons::Source source = icons::parse(id);
    if (source.remote()) return IconLoad::kMissing;
    for (const auto format : {icons::ImageFormat::kGif, icons::ImageFormat::kJpeg}) {
      media::PodBuffer<uint8_t> bytes;
      int width = 0, height = 0;
      const auto read = media::readNativeImage(source, format, maxWidth, maxHeight,
                                               maxResidentBytes, bytes, width, height);
      if (read == media::IconRead::kOom) return IconLoad::kOom;
      if (read != media::IconRead::kGood) continue;
      if (format == icons::ImageFormat::kGif) {
        // Every frame must fit the admitted logical canvas, including decoder scratch.
        const auto result = gif_.openBytes(std::move(bytes), width, height);
        if (result == GifPlayer::OpenResult::kOom) return IconLoad::kOom;
        if (result == GifPlayer::OpenResult::kGood) return takeGif();
        continue;
      }
      bool oom = false;
      if (icon::decodeNative(bytes.data(), bytes.size(), maxWidth, maxHeight,
                             pixels_, width_, height_, &oom)) return IconLoad::kGood;
      if (oom) return IconLoad::kOom;
    }
    clear();
    return IconLoad::kMissing;
  }
  bool nativeInfo(std::string_view id, int maxWidth, int maxHeight,
                  int& width, int& height, std::size_t& bytes) const override {
    if (icons::parse(id).remote()) return false;
    return media::inspectNative(id,maxWidth,maxHeight,width,height,bytes);
  }

 private:
  IconLoad load(const std::string& iconId, int maxWidth, int maxHeight) {
    clear();
    const auto result = gif_.open(iconId, maxWidth, maxHeight, false, 1);
    if (result == GifPlayer::OpenResult::kGood) return takeGif();
    if (result == GifPlayer::OpenResult::kOom) return IconLoad::kOom;
    if (!pixels_.resize(8 * 8)) return IconLoad::kOom;
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

  IconLoad takeGif() {
    if (!adoptGif()) {
      clear();
      return IconLoad::kOom;
    }
    return IconLoad::kGood;
  }

 protected:
  bool adoptGif() {
    if (gif_.takeFrame(pixels_) == GifPlayer::Frame::kOom) return false;
    width_ = gif_.width();
    height_ = gif_.height();
    return true;
  }

 public:
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

 protected:
  media::PodBuffer<uint32_t> pixels_;
  GifPlayer gif_;
  int width_ = 0;
  int height_ = 0;
};

}
