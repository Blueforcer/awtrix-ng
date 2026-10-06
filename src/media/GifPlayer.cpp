#include "media/GifPlayer.h"

#include <cstring>

#include "media/ImageInfo.h"

namespace awtrix {

namespace {

// Ceiling on decoded frames held in RAM. A GIF that wants more is played by streaming instead,
// which costs CPU per frame but keeps the compressed bytes only.
constexpr int kPreDecodeBudgetBytes = 16 * 1024;

// Plenty of GIFs declare a 0 ms delay; browsers substitute roughly 100 ms and so do we.
int playbackDelayMs(int delayMs) { return delayMs > 0 ? delayMs : 100; }

}

GifPlayer::~GifPlayer() { close(); }

GifPlayer::OpenResult GifPlayer::open(std::string_view icon, int maxWidth, int maxHeight,
                                      bool firstFrameOnly,
                                      int maxResidentFrames) {
  close();
  if (maxWidth <= 0 || maxHeight <= 0) return OpenResult::kMissing;
  switch (media::readIconBytes(icons::parse(icon), icons::ImageFormat::kGif, data_)) {
    case media::IconRead::kGood: break;
    case media::IconRead::kOom: return OpenResult::kOom;
    case media::IconRead::kMissing: return OpenResult::kMissing;
  }
  return decodeLoaded(maxWidth, maxHeight, firstFrameOnly, maxResidentFrames);
}

GifPlayer::OpenResult GifPlayer::openBytes(media::PodBuffer<uint8_t> bytes,
    int maxWidth, int maxHeight, bool firstFrameOnly, int maxResidentFrames) {
  close();
  data_ = std::move(bytes);
  return decodeLoaded(maxWidth, maxHeight, firstFrameOnly, maxResidentFrames);
}

GifPlayer::OpenResult GifPlayer::decodeLoaded(int maxWidth, int maxHeight,
    bool firstFrameOnly, int maxResidentFrames) {
  if (maxWidth <= 0 || maxHeight <= 0 || data_.size() < 6 || std::memcmp(data_.data(), "GIF8", 4) != 0) {
    data_.clear();
    return OpenResult::kMissing;
  }
  if (!gif_.begin(data_.data(), data_.size(), maxWidth, maxHeight)) {
    data_.clear();
    return OpenResult::kMissing;
  }
  w_ = gif_.width();
  h_ = gif_.height();

  const PreDecode pd = preDecode(firstFrameOnly, maxResidentFrames);
  if (pd == PreDecode::kDone) {
    gif_ = media::MicroGif{};
    data_.clear();
    if (frameCount_ == 0) {
      close();
      return OpenResult::kMissing;
    }
    frames_.shrinkToFit();
    delays_.shrinkToFit();
  } else if (pd == PreDecode::kStream) {
    streaming_ = true;
    streamFirstFrame_ = false;
    streamInitialPending_ = true;
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
// GIF is too long to cache: retain just its validated first frame and stream subsequent ones.
GifPlayer::PreDecode GifPlayer::preDecode(bool firstFrameOnly, int maxResidentFrames) {
  const size_t framePixels = static_cast<size_t>(w_) * h_;
  const int budgetFrames = kPreDecodeBudgetBytes / (framePixels * sizeof(uint32_t));
  // A still-image request must retain its first frame even when that one frame exceeds the
  // animation cache budget; falling back to streaming would accidentally animate it.
  const int cachedFrames = maxResidentFrames > 0 && maxResidentFrames < budgetFrames
                               ? maxResidentFrames : budgetFrames;
  const int maxFrames = firstFrameOnly || cachedFrames < 1 ? 1 : cachedFrames;
  const int found = gif_.countFrames(maxFrames);
  if (!firstFrameOnly && found > maxFrames) {
    if (!frames_.resize(framePixels)) return PreDecode::kOom;
    Canvas first(w_, h_, frames_.data());
    const auto step = gif_.nextFrame(first, initialDelayMs_, true);
    if (step == media::MicroGif::Step::kOom) return PreDecode::kOom;
    if (step != media::MicroGif::Step::kFrame) return PreDecode::kDone;
    initialDelayMs_ = playbackDelayMs(initialDelayMs_);
    return PreDecode::kStream;
  }
  const int slots = found < maxFrames ? found : maxFrames;
  const size_t cachePixels = static_cast<size_t>(slots) * framePixels;
  if (!frames_.resize(cachePixels, cachePixels) || !delays_.resize(slots, slots))
    return PreDecode::kOom;
  // GIF frames are deltas, so each slot starts as a copy of the frame before it.
  for (; frameCount_ < slots; ++frameCount_) {
    uint32_t* slot = frames_.data() + static_cast<size_t>(frameCount_) * framePixels;
    Canvas frame(w_, h_, slot);
    if (frameCount_ == 0) frame.clear(0x000000u);
    else std::memcpy(slot, slot - framePixels, framePixels * sizeof(uint32_t));
    int delayMs = 0;
    const media::MicroGif::Step st = gif_.nextFrame(frame, delayMs);
    if (st == media::MicroGif::Step::kOom) return PreDecode::kOom;
    if (st != media::MicroGif::Step::kFrame) break;
    delays_[frameCount_] = static_cast<uint16_t>(playbackDelayMs(delayMs) / 10);
  }
  // A frame that failed to decode ends the cache early; the slots after it go back to the heap.
  frames_.resize(static_cast<size_t>(frameCount_) * framePixels);
  frames_.shrinkToFit();
  delays_.resize(frameCount_);
  delays_.shrinkToFit();
  return PreDecode::kDone;
}

bool GifPlayer::takeStaticFrame(media::PodBuffer<uint32_t>& out) {
  if (!active_ || frameCount_ != 1) return false;
  out = std::move(frames_);
  delays_.clear();
  frameCount_ = 0;
  cur_ = 0;
  active_ = false;
  return true;
}

bool GifPlayer::takeInitialFrame(media::PodBuffer<uint32_t>& out) {
  if (!active_ || !streamInitialPending_ || frames_.empty()) return false;
  out = std::move(frames_);
  return true;
}

GifPlayer::Frame GifPlayer::takeFrame(media::PodBuffer<uint32_t>& out) {
  if (takeStaticFrame(out)) return Frame::kStill;
  if (takeInitialFrame(out)) return Frame::kPlaying;
  if (!out.resize(static_cast<std::size_t>(w_) * h_)) return Frame::kOom;
  Canvas(w_, h_, out.data()).clear();
  return Frame::kPlaying;
}

void GifPlayer::close() {
  streaming_ = false;
  gif_ = media::MicroGif{};
  data_.clear();
  frames_.clear();
  delays_.clear();
  frameCount_ = 0;
  cur_ = 0;
  w_ = 0;
  h_ = 0;
  streamFirstFrame_ = true;
  streamInitialPending_ = false;
  initialDelayMs_ = 0;
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
    nextFrameMs_ = nowMs + static_cast<int>(delays_[cur_]) * 10;
    cur_ = (cur_ + 1) % frameCount_;
    return;
  }
  if (!streaming_) return;
  if (streamInitialPending_) {
    if (!frames_.empty()) {
      blitFrame(dst, 0);
      frames_.clear();
    }
    streamInitialPending_ = false;
    nextFrameMs_ = nowMs + initialDelayMs_;
    return;
  }
  int delayMs = 0;
  // Looping in place: on the trailer, rewind and decode the first frame in the same call so the
  // animation never shows a blank tick.
  media::MicroGif::Step st = gif_.nextFrame(dst, delayMs, streamFirstFrame_);
  if (st == media::MicroGif::Step::kEnd) {
    gif_.rewind();
    streamFirstFrame_ = true;
    st = gif_.nextFrame(dst, delayMs, true);
  }
  if (st == media::MicroGif::Step::kOom) {
    // MicroGif leaves both the image and the pending frame intact for a later retry.
    nextFrameMs_ = nowMs + 1000;
    return;
  }
  streamFirstFrame_ = false;
  if (st != media::MicroGif::Step::kFrame) {
    gif_.rewind();
    streamFirstFrame_ = true;
    nextFrameMs_ = nowMs + 1000;
    return;
  }
  nextFrameMs_ = nowMs + playbackDelayMs(delayMs);
}

}
