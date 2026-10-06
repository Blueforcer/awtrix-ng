#pragma once

#include "core/audio/Mp3Decoder.h"
#include "core/audio/StreamInputBuffer.h"

namespace awtrix::audio {

// Feeds buffered stream bytes to the decoder until maxFrames frames are out, the decoder needs
// more input or onFrame returns false. decode(view) decodes one frame from the view; onFrame gets
// each decoded frame. Returns whether any input was consumed.
template <typename Decode, typename OnFrame>
bool decodeFrames(StreamInputBuffer& input, int maxFrames, Decode&& decode, OnFrame&& onFrame) {
  bool consumed = false;
  int frames = 0;
  while (input.size() && frames < maxFrames) {
    const mp3::DecodeResult result = decode(input.decoderView());
    if (result.bytesConsumed == 0) break;
    input.consume(result.bytesConsumed);
    consumed = true;
    if (result.status == mp3::DecodeStatus::NeedMoreData) break;
    if (result.status != mp3::DecodeStatus::Ok) continue;
    ++frames;
    if (!onFrame(result)) break;
  }
  return consumed;
}

}
