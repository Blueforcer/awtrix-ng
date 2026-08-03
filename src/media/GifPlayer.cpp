#include "media/GifPlayer.h"

#include <cstring>

#include "media/AssetFile.h"

unsigned int decode_base64_length(const unsigned char input[], unsigned int input_length);
unsigned int decode_base64(const unsigned char input[], unsigned int input_length,
                           unsigned char output[]);

namespace awtrix {

namespace {

// Ceiling on decoded frames held in RAM. A GIF that wants more is played by streaming instead,
// which costs CPU per frame but keeps the compressed bytes only.
constexpr int kPreDecodeBudgetBytes = 16 * 1024;

}

GifPlayer::~GifPlayer() { close(); }

GifPlayer::OpenResult GifPlayer::open(const std::string& iconId, bool firstFrameOnly,
                                      int maxResidentFrames) {
  close();
  // No id that long can be a filename, so treat it as an inline base64 GIF from the API.
  if (iconId.size() > 64) {
    const auto* in = reinterpret_cast<const unsigned char*>(iconId.c_str());
    const unsigned int maxLen = decode_base64_length(in, iconId.size());
    if (!data_.resize(maxLen)) return OpenResult::kOom;
    const unsigned int n = decode_base64(in, iconId.size(), data_.data());
    if (n < 6) {
      data_.clear();
      return OpenResult::kMissing;
    }
    data_.resize(n);
  } else {
    if (!media::readAsset("/ICONS/" + iconId + ".gif", data_)) return OpenResult::kMissing;
  }
  if (data_.size() < 6 || std::memcmp(data_.data(), "GIF8", 4) != 0) {
    data_.clear();
    return OpenResult::kMissing;
  }
  if (!gif_.begin(data_.data(), data_.size())) {
    data_.clear();
    return OpenResult::kMissing;
  }
  w_ = gif_.width();
  h_ = gif_.height();

  const PreDecode pd = preDecode(firstFrameOnly, maxResidentFrames);
  if (pd == PreDecode::kDone) {
    data_.clear();
    if (frameCount_ == 0) {
      close();
      return OpenResult::kMissing;
    }
    frames_.shrinkToFit();
    delays_.shrinkToFit();
  } else if (pd == PreDecode::kStream) {
    gif_.rewind();
    streaming_ = true;
    frames_.clear();
    delays_.clear();
    frameCount_ = 0;
    cur_ = 0;
  } else {
    close();
    return OpenResult::kOom;
  }
  active_ = true;
  nextFrameMs_ = 0;
  return OpenResult::kGood;
}

// Caches frames as raw pixels until the budget or the caller's cap is reached. kStream means the
// GIF is too long to cache at all — open() drops the partial frames and rewinds for streaming.
GifPlayer::PreDecode GifPlayer::preDecode(bool firstFrameOnly, int maxResidentFrames) {
  const int frameBytes = w_ * h_;
  const int budgetFrames =
      kPreDecodeBudgetBytes / (frameBytes * static_cast<int>(sizeof(uint32_t)));
  const int maxFrames = maxResidentFrames > 0 ? maxResidentFrames : budgetFrames;
  Canvas scratch(w_, h_);
  scratch.clear(0x000000u);
  for (;;) {
    int delayMs = 0;
    const media::MicroGif::Step st = gif_.nextFrame(scratch, delayMs);
    if (st != media::MicroGif::Step::kFrame) break;
    if (frameCount_ == maxFrames) return PreDecode::kStream;
    if (!frames_.resize(static_cast<size_t>(frameCount_ + 1) * frameBytes) ||
        !delays_.resize(static_cast<size_t>(frameCount_) + 1))
      return PreDecode::kOom;
    uint32_t* out = frames_.data() + static_cast<size_t>(frameCount_) * frameBytes;
    for (int y = 0; y < h_; ++y)
      for (int x = 0; x < w_; ++x) *out++ = scratch.getPixel(x, y);
    // Plenty of GIFs declare a 0 ms delay; browsers substitute roughly 100 ms and so do we.
    if (delayMs <= 0) delayMs = 100;
    delays_[frameCount_] = static_cast<uint16_t>(delayMs < 65535 ? delayMs : 65535);
    ++frameCount_;
    if (firstFrameOnly) break;
  }
  return PreDecode::kDone;
}

void GifPlayer::close() {
  streaming_ = false;
  data_.clear();
  frames_.clear();
  delays_.clear();
  frameCount_ = 0;
  cur_ = 0;
  w_ = 0;
  h_ = 0;
  streamFirstFrame_ = true;
  active_ = false;
}

void GifPlayer::blitFrame(Canvas& dst, int frame) const {
  const uint32_t* px = frames_.data() + static_cast<size_t>(frame) * w_ * h_;
  for (int y = 0; y < h_; ++y)
    for (int x = 0; x < w_; ++x) dst.setPixel(x, y, *px++);
}

void GifPlayer::render(Canvas& dst, int64_t nowMs) {
  if (!active_) return;
  if (nowMs < nextFrameMs_) return;
  if (frameCount_ > 0) {
    blitFrame(dst, cur_);
    nextFrameMs_ = nowMs + delays_[cur_];
    cur_ = (cur_ + 1) % frameCount_;
    return;
  }
  if (!streaming_) return;
  if (streamFirstFrame_) {
    dst.fillRect(0, 0, w_, h_, 0x000000u);
    streamFirstFrame_ = false;
  }
  int delayMs = 0;
  // Looping in place: on the trailer, rewind and decode the first frame in the same call so the
  // animation never shows a blank tick.
  media::MicroGif::Step st = gif_.nextFrame(dst, delayMs);
  if (st == media::MicroGif::Step::kEnd) {
    gif_.rewind();
    dst.fillRect(0, 0, w_, h_, 0x000000u);
    st = gif_.nextFrame(dst, delayMs);
  }
  if (st != media::MicroGif::Step::kFrame) {
    gif_.rewind();
    streamFirstFrame_ = true;
    nextFrameMs_ = nowMs + 1000;
    return;
  }
  if (delayMs <= 0) delayMs = 100;
  nextFrameMs_ = nowMs + delayMs;
}

}
