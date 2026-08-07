#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/audio/Mp3Decoder.h"

namespace awtrix {
namespace mp3 {

// Pulls a whole MP3 file through the decoder one frame per call. The read
// callback hides where the bytes come from, so the walk itself runs in host
// tests while the device side only supplies File::read and I2S.
class ClipDecoder {
 public:
  enum class Step : uint8_t { Frame, Done, Error };

  // Fills dst with up to max encoded bytes; a return <= 0 means end of file.
  using ReadFn = int (*)(void* ctx, uint8_t* dst, std::size_t max);

  explicit ClipDecoder(Decoder& decoder) : decoder_(decoder) {}

  Step next(ReadFn read, void* ctx, int16_t* pcm, DecodeResult& out);

 private:
  void refill(ReadFn read, void* ctx);

  Decoder& decoder_;
  std::vector<uint8_t> buf_;
  std::size_t consumed_ = 0;
  std::size_t scanned_ = 0;
  bool eof_ = false;
  bool decodedAnything_ = false;
};

}
}
