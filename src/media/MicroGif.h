#pragma once

#include <cstddef>
#include <cstdint>

#include "core/render/Canvas.h"

namespace awtrix {
namespace media {

// Streaming GIF87a/89a decoder sized for the panel: no full-screen backbuffer, one frame at a
// time, and everything larger than kMaxW x kMaxH is clipped or rejected.
class MicroGif {
 public:
  static constexpr int kMaxW = 32;
  static constexpr int kMaxH = 8;

  bool begin(const uint8_t* data, std::size_t len);

  int width() const { return w_; }
  int height() const { return h_; }

  enum class Step {
    kFrame,
    kEnd,
    kError,
  };
  // Composites the next frame onto dst without clearing it first — GIF frames are deltas over
  // whatever the previous one left behind. delayMs is 0 unless kFrame is returned.
  Step nextFrame(Canvas& dst, int& delayMs);

  void rewind();

 private:
  struct LzwScratch;

  int readByte();
  int readWord();
  bool skipSubBlocks();
  bool parseGce();
  Step decodeImage(Canvas& dst);
  bool lzwDecode(int minCodeSize, LzwScratch& s, uint8_t* out, int npix);

  const uint8_t* data_ = nullptr;
  std::size_t len_ = 0;
  std::size_t pos_ = 0;
  std::size_t firstFramePos_ = 0;
  int w_ = 0, h_ = 0;
  int bgIndex_ = 0;
  int globalColors_ = 0;
  uint32_t palette_[256];

  int transparent_ = -1;
  int disposal_ = 0;
  int pendingDelayMs_ = 0;
  // Disposal of a frame happens lazily, just before the next one is drawn, so the rect of the
  // frame still on screen has to survive until then.
  int prevDisposal_ = 0;
  int prevX_ = 0, prevY_ = 0, prevW_ = 0, prevH_ = 0;
};

}
}
