#pragma once

#include <cstdint>
#include <string>

#include "core/render/Canvas.h"
#include "media/MicroGif.h"
#include "media/PodBuffer.h"

namespace awtrix {

class GifPlayer {
 public:
  enum class OpenResult {
    kGood,
    kMissing,
    kOom,
  };

  ~GifPlayer();
  // iconId is either the stem of /ICONS/<id>.gif or, above 64 characters, a base64-encoded GIF
  // sent inline by the API. Limits come from the panel; buffers follow the GIF's own size.
  // maxResidentFrames 0 means "whatever the RAM budget allows".
  OpenResult open(const std::string& iconId, int maxWidth, int maxHeight,
                  bool firstFrameOnly = false,
                  int maxResidentFrames = 0);
  void close();
  bool active() const { return active_; }
  int width() const { return w_; }
  int height() const { return h_; }
  void render(Canvas& dst, int64_t nowMs);
  // Transfers a single cached image to its caller without a second RGB buffer. Dimensions
  // remain available, but playback stops. Animated/streaming GIFs return false unchanged.
  bool takeStaticFrame(media::PodBuffer<uint32_t>& out);
  // Transfers the already validated first streaming frame. The caller must keep it as the
  // destination for render(); the first render starts its delay without decoding it twice.
  bool takeInitialFrame(media::PodBuffer<uint32_t>& out);

 private:
  enum class PreDecode { kDone, kStream, kOom };
  PreDecode preDecode(bool firstFrameOnly, int maxResidentFrames);
  void blitFrame(Canvas& dst, int frame) const;

  media::PodBuffer<uint32_t> frames_;
  media::PodBuffer<uint16_t> delays_;  // GIF centiseconds, preserving the full 16-bit range.
  int frameCount_ = 0;
  int cur_ = 0;
  int w_ = 0, h_ = 0;

  // Fallback path for GIFs whose decoded frames do not fit the budget: the compressed bytes stay
  // resident instead and each frame is decoded straight onto the destination canvas.
  media::MicroGif gif_;
  bool streaming_ = false;
  media::PodBuffer<uint8_t> data_;
  bool streamFirstFrame_ = true;
  bool streamInitialPending_ = false;
  int initialDelayMs_ = 0;

  bool active_ = false;
  int64_t nextFrameMs_ = 0;
};

}
