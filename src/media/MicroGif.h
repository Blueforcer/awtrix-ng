#pragma once

#include <cstddef>
#include <cstdint>

#include "core/render/Canvas.h"
#include "media/PodBuffer.h"

namespace awtrix {
namespace media {

// Streaming GIF87a/89a decoder bounded by the caller's panel dimensions. Scratch memory follows
// the current frame, not the panel; no full-screen backbuffer is kept here.
class MicroGif {
 public:
  // The input, including its palettes, must stay alive until the last frame is decoded.
  bool begin(const uint8_t* data, std::size_t len, int maxWidth, int maxHeight);

  int width() const { return w_; }
  int height() const { return h_; }

  enum class Step {
    kFrame,
    kEnd,
    kError,
    kOom,
  };
  // Composites the next frame onto dst without clearing it first — GIF frames are deltas over
  // whatever the previous one left behind. delayMs is 0 unless kFrame is returned.
  Step nextFrame(Canvas& dst, int& delayMs, bool clearFirst = false);

  // Counts structurally valid frame descriptors without decoding pixels. Used to choose
  // streaming before allocating a frame cache that would immediately be discarded.
  bool exceedsFrameCount(int limit) const;

  void rewind();

 private:
  struct LzwScratch;
  // Decoding is serialized on the render task. All live decoders share one dynamically sized
  // workspace; a claim keeps it warm and releases excess capacity when its GIF closes.
  struct ScratchClaim {
    ScratchClaim();
    ~ScratchClaim();
    ScratchClaim(ScratchClaim&& other) noexcept;
    ScratchClaim& operator=(ScratchClaim&& other) noexcept;
    uint16_t* acquire(std::size_t words);

   private:
    void release();
    ScratchClaim* prev_ = nullptr;
    ScratchClaim* next_ = nullptr;
    std::size_t words_ = 0;
    static ScratchClaim* head_;
    static PodBuffer<uint16_t>& workspace();
  };

  int readByte();
  int readWord();
  bool skipSubBlocks();
  bool parseGce();
  Step decodeImage(Canvas& dst, bool clearFirst);
  bool lzwDecode(int minCodeSize, LzwScratch& s, uint8_t* out, int npix);

  const uint8_t* data_ = nullptr;
  std::size_t len_ = 0;
  std::size_t pos_ = 0;
  std::size_t firstFramePos_ = 0;
  int w_ = 0, h_ = 0;
  int maxW_ = 0, maxH_ = 0;
  int bgIndex_ = 0;
  int globalColors_ = 0;
  const uint8_t* palette_ = nullptr;

  int transparent_ = -1;
  int disposal_ = 0;
  int pendingDelayMs_ = 0;
  // Disposal of a frame happens lazily, just before the next one is drawn, so the rect of the
  // frame still on screen has to survive until then.
  int prevDisposal_ = 0;
  int prevX_ = 0, prevY_ = 0, prevW_ = 0, prevH_ = 0;
  // Disposal 3 restores the pixels that were present before the frame. Allocate only the visible
  // frame rectangle; reuse its capacity until this GIF closes.
  PodBuffer<uint32_t> restore_;
  ScratchClaim scratch_;
};

}
}
