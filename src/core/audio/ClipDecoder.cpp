#include "core/audio/ClipDecoder.h"

namespace awtrix {
namespace mp3 {

namespace {

constexpr std::size_t kReadChunkBytes = 1024;
constexpr std::size_t kWindowBytes = 8 * 1024;
constexpr std::size_t kCompactAtBytes = 4 * 1024;
// Large embedded cover art is the pathological case: give up once this much
// of the file has gone by without a single decodable frame.
constexpr std::size_t kScanCapBytes = 128 * 1024;

}

void ClipDecoder::refill(ReadFn read, void* ctx) {
  while (!eof_ && buf_.size() - consumed_ < kWindowBytes) {
    const std::size_t old = buf_.size();
    buf_.resize(old + kReadChunkBytes);
    const int n = read(ctx, buf_.data() + old, kReadChunkBytes);
    if (n <= 0) {
      buf_.resize(old);
      eof_ = true;
      return;
    }
    buf_.resize(old + static_cast<std::size_t>(n));
  }
}

ClipDecoder::Step ClipDecoder::next(ReadFn read, void* ctx, int16_t* pcm, DecodeResult& out) {
  for (;;) {
    if (consumed_ >= kCompactAtBytes) {
      buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(consumed_));
      consumed_ = 0;
    }
    refill(read, ctx);

    const std::size_t have = buf_.size() - consumed_;
    if (have == 0) return decodedAnything_ ? Step::Done : Step::Error;

    out = decoder_.decode(buf_.data() + consumed_, have, pcm);
    if (out.bytesConsumed == 0) {
      // Nothing consumed with a full window means a header that promises more
      // bytes than any real frame has; step past it rather than spin.
      if (eof_) return decodedAnything_ ? Step::Done : Step::Error;
      if (have >= kWindowBytes) consumed_ += 1;
      continue;
    }
    consumed_ += out.bytesConsumed;
    if (out.status == DecodeStatus::Ok) {
      decodedAnything_ = true;
      return Step::Frame;
    }
    if (!decodedAnything_) {
      scanned_ += out.bytesConsumed;
      if (scanned_ > kScanCapBytes) return Step::Error;
    }
  }
}

}
}
