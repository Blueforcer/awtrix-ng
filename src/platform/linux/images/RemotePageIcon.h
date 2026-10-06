#pragma once

#include "media/DevicePageIcon.h"
#include "media/RemoteImages.h"

namespace awtrix {

class RemotePageIcon final : public DevicePageIcon {
 public:
  explicit RemotePageIcon(media::IRemoteImages* pictures) : pictures_(pictures) {}

  IconLoad begin(const std::string& id, int maxWidth, int maxHeight) override {
    if (!icons::parse(id).remote()) return localBox(DevicePageIcon::begin(id, maxWidth, maxHeight));
    const int side = std::min(maxWidth, maxHeight);
    return openRemote(id, side, side, std::numeric_limits<std::size_t>::max());
  }
  IconLoad beginNativeBounded(std::string_view id, int maxWidth, int maxHeight,
                             std::size_t maxResidentBytes) override {
    if (!icons::parse(id).remote())
      return localBox(DevicePageIcon::beginNativeBounded(id, maxWidth, maxHeight, maxResidentBytes));
    return openRemote(id, maxWidth, maxHeight, maxResidentBytes);
  }
  bool nativeInfo(std::string_view id, int maxWidth, int maxHeight,
                  int& width, int& height, std::size_t& bytes) const override {
    if (!icons::parse(id).remote())
      return DevicePageIcon::nativeInfo(id, maxWidth, maxHeight, width, height, bytes);
    if (!pictures_ || maxWidth <= 0 || maxHeight <= 0) return false;
    width = maxWidth;
    height = maxHeight;
    return remoteCost(width, height, bytes);
  }
  void clear() override {
    DevicePageIcon::clear();
    boxWidth_ = boxHeight_ = 0;
    remote_.reset();
  }
  void advance(int64_t nowMs) override {
    if (remote_ && remote_->waiting && pictures_->generation() != remote_->seen) askRemote();
    DevicePageIcon::advance(nowMs);
  }
  void blit(Canvas& dst, int xOffset, int yOffset = 0) const override {
    DevicePageIcon::blit(dst, xOffset + (boxWidth_ - width_) / 2,
                        yOffset + (boxHeight_ - height_) / 2);
  }
  int width() const override { return boxWidth_; }
  int height() const override { return boxHeight_; }
  std::unique_ptr<IPageIcon> create() const override {
    return std::unique_ptr<IPageIcon>(new (std::nothrow) RemotePageIcon(pictures_));
  }

 private:
  IconLoad localBox(IconLoad result) {
    boxWidth_ = width_;
    boxHeight_ = height_;
    return result;
  }

  // Maximum resident cost, including a GIF frame and decoder scratch.
  static bool remoteCost(int width, int height, std::size_t& bytes) {
    return media::detail::residentCost(media::kMaxRemoteGifBytes, width, height, true, bytes);
  }

  // The page reserves the box while the picture is pending or missing.
  IconLoad openRemote(std::string_view url, int width, int height, std::size_t maxResidentBytes) {
    clear();
    std::size_t cost = 0;
    if (!pictures_ || width <= 0 || height <= 0 ||
        !remoteCost(width, height, cost) || cost > maxResidentBytes)
      return IconLoad::kMissing;
    remote_.reset(new (std::nothrow) Remote());
    if (!remote_) return IconLoad::kOom;
    remote_->url.assign(url.data(), url.size());
    remote_->bound = maxResidentBytes;
    boxWidth_ = width;
    boxHeight_ = height;
    askRemote();
    return IconLoad::kGood;
  }

  void askRemote() {
    remote_->seen = pictures_->generation();
    media::RemoteImage image;
    const media::RemoteState state = pictures_->get(remote_->url, boxWidth_, boxHeight_, image);
    remote_->waiting = state != media::RemoteState::kReady;
    if (state != media::RemoteState::kReady) return;
    if (image.gif.empty()) {
      pixels_ = std::move(image.pixels);
      width_ = image.width;
      height_ = image.height;
      return;
    }
    std::size_t cost = 0;
    if (!media::detail::residentCost(image.gif.size(), image.width, image.height, true, cost) ||
        cost > remote_->bound || gif_.openBytes(std::move(image.gif), image.width, image.height) !=
                                   GifPlayer::OpenResult::kGood)
      return;
    if (!adoptGif()) gif_.close();
  }

  struct Remote {
    std::string url;
    std::size_t bound = 0;
    uint32_t seen = 0;
    bool waiting = false;
  };

  media::IRemoteImages* pictures_;
  std::unique_ptr<Remote> remote_;
  int boxWidth_ = 0;
  int boxHeight_ = 0;
};

}
