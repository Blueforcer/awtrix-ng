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
  // sent inline by the API. maxResidentFrames 0 means "whatever the RAM budget allows".
  OpenResult open(const std::string& iconId, bool firstFrameOnly = false,
                  int maxResidentFrames = 0);
  void close();
  bool active() const { return active_; }
  int width() const { return w_; }
  int height() const { return h_; }
  void render(Canvas& dst, int64_t nowMs);

 private:
  enum class PreDecode { kDone, kStream, kOom };
  PreDecode preDecode(bool firstFrameOnly, int maxResidentFrames);
  void blitFrame(Canvas& dst, int frame) const;

  media::PodBuffer<uint32_t> frames_;
  media::PodBuffer<uint16_t> delays_;
  int frameCount_ = 0;
  int cur_ = 0;
  int w_ = 0, h_ = 0;

  // Fallback path for GIFs whose decoded frames do not fit the budget: the compressed bytes stay
  // resident instead and each frame is decoded straight onto the destination canvas.
  media::MicroGif gif_;
  bool streaming_ = false;
  media::PodBuffer<uint8_t> data_;
  bool streamFirstFrame_ = true;

  bool active_ = false;
  int64_t nextFrameMs_ = 0;
};

}
