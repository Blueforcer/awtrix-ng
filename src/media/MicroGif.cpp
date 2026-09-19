#include "media/MicroGif.h"

#include <cstring>
#include <limits>

#include "core/render/Color.h"

namespace awtrix {
namespace media {

namespace {

// GIF codes have at most 12 bits, regardless of frame or panel size.
constexpr int kLzwMaxSlots = 4096;

bool pixelCountFits(int w, int h) {
  return w > 0 && h > 0 && w <= std::numeric_limits<int>::max() / h &&
         static_cast<std::size_t>(w) <=
             std::numeric_limits<std::size_t>::max() / sizeof(uint32_t) / h;
}

// Interlaced GIFs store rows out of order in four passes (every 8th from 0, every 8th from 4,
// every 4th from 2, every 2nd from 1). Maps decode order r to the row it belongs on.
int interlacedRow(int r, int h) {
  static const int kStart[4] = {0, 4, 2, 1};
  static const int kStep[4] = {8, 8, 4, 2};
  for (int p = 0; p < 4; ++p) {
    const int rows = h > kStart[p] ? (h - kStart[p] + kStep[p] - 1) / kStep[p] : 0;
    if (r < rows) return kStart[p] + r * kStep[p];
    r -= rows;
  }
  return 0;
}

}

struct MicroGif::LzwScratch {
  // One aligned allocation for the dictionary, expansion stack and frame indices. A frame can
  // create at most one dictionary entry per output pixel, so small icons need small tables too.
  int initial = 0;
  int slots = 0;
  int stackSize = 0;
  uint16_t* prefix = nullptr;
  uint8_t* suffix = nullptr;
  uint8_t* stack = nullptr;
  uint8_t* index = nullptr;

  bool allocate(int npix, int minCodeSize, ScratchClaim& claim) {
    initial = (1 << minCodeSize) + 2;
    slots = npix < kLzwMaxSlots - initial ? npix + initial : kLzwMaxSlots;
    // Literal codes do not need dictionary records. No expansion can exceed the pixels
    // already produced plus one, or the format's dictionary size.
    const int entries = slots - initial;
    stackSize = npix < kLzwMaxSlots ? npix : kLzwMaxSlots;
    const std::size_t bytes = static_cast<std::size_t>(entries) * 3 + stackSize + npix;
    prefix = claim.acquire((bytes + 1) / 2);
    if (!prefix) return false;
    suffix = reinterpret_cast<uint8_t*>(prefix + entries);
    stack = suffix + entries;
    index = stack + stackSize;
    return true;
  }
};

MicroGif::ScratchClaim* MicroGif::ScratchClaim::head_ = nullptr;
PodBuffer<uint16_t>& MicroGif::ScratchClaim::workspace() {
  static PodBuffer<uint16_t> buffer;
  return buffer;
}

MicroGif::ScratchClaim::ScratchClaim() {
  // Construct the pool before any containing global player finishes construction, so its
  // destructor always runs after those players (including across translation units).
  (void)workspace();
}

MicroGif::ScratchClaim::~ScratchClaim() { release(); }

MicroGif::ScratchClaim::ScratchClaim(ScratchClaim&& other) noexcept {
  *this = std::move(other);
}

MicroGif::ScratchClaim& MicroGif::ScratchClaim::operator=(ScratchClaim&& other) noexcept {
  if (this == &other) return *this;
  release();
  words_ = other.words_;
  prev_ = other.prev_;
  next_ = other.next_;
  if (words_) {
    if (prev_) prev_->next_ = this;
    else head_ = this;
    if (next_) next_->prev_ = this;
  }
  other.words_ = 0;
  other.prev_ = other.next_ = nullptr;
  return *this;
}

void MicroGif::ScratchClaim::release() {
  if (!words_) return;
  if (prev_) prev_->next_ = next_;
  else head_ = next_;
  if (next_) next_->prev_ = prev_;
  words_ = 0;
  prev_ = next_ = nullptr;
  std::size_t needed = 0;
  for (const ScratchClaim* claim = head_; claim; claim = claim->next_)
    if (claim->words_ > needed) needed = claim->words_;
  // Shrinking must not itself require more memory. The next decoder allocates the smaller
  // workspace on demand; closing the last player releases it entirely.
  if (workspace().size() > needed) workspace().clear();
}

uint16_t* MicroGif::ScratchClaim::acquire(std::size_t words) {
  if (!words_) {
    next_ = head_;
    if (head_) head_->prev_ = this;
    head_ = this;
  }
  if (words > words_) words_ = words;
  auto& buffer = workspace();
  if (buffer.size() < words) {
    buffer.clear();
    if (!buffer.resize(words)) return nullptr;
  }
  return buffer.data();
}

int MicroGif::readByte() {
  if (pos_ >= len_) return -1;
  return data_[pos_++];
}

int MicroGif::readWord() {
  const int b0 = readByte();
  const int b1 = readByte();
  if (b0 < 0 || b1 < 0) return -1;
  return (b1 << 8) | b0;
}

// GIF payloads are chains of length-prefixed sub-blocks ended by a zero-length one.
bool MicroGif::skipSubBlocks() {
  for (;;) {
    const int size = readByte();
    if (size < 0) return false;
    if (size == 0) return true;
    pos_ += static_cast<std::size_t>(size);
    if (pos_ > len_) return false;
  }
}

bool MicroGif::begin(const uint8_t* data, std::size_t len, int maxWidth, int maxHeight) {
  *this = MicroGif{};
  // 6-byte signature plus the 7-byte logical screen descriptor is the shortest legal header.
  if (!data || len < 13 || maxWidth <= 0 || maxHeight <= 0) return false;
  if (std::memcmp(data, "GIF87a", 6) != 0 && std::memcmp(data, "GIF89a", 6) != 0) return false;
  data_ = data;
  len_ = len;
  pos_ = 6;
  const int lw = readWord();
  const int lh = readWord();
  const int packed = readByte();
  bgIndex_ = readByte();
  readByte();
  if (lw <= 0 || lh <= 0 || packed < 0) {
    data_ = nullptr;
    return false;
  }
  // An oversized logical screen is clamped rather than rejected; only individual frames bigger
  // than the panel are refused later on.
  maxW_ = maxWidth < 65535 ? maxWidth : 65535;
  maxH_ = maxHeight < 65535 ? maxHeight : 65535;
  w_ = lw < maxW_ ? lw : maxW_;
  h_ = lh < maxH_ ? lh : maxH_;
  if (!pixelCountFits(w_, h_)) {
    data_ = nullptr;
    w_ = h_ = 0;
    return false;
  }
  globalColors_ = 0;
  // Bit 7 of the packed field means a global color table follows, bits 0-2 hold its size as
  // log2(entries) - 1.
  if (packed & 0x80) {
    globalColors_ = 1 << ((packed & 7) + 1);
    if (pos_ + static_cast<std::size_t>(globalColors_) * 3 > len_) {
      data_ = nullptr;
      return false;
    }
    palette_ = data_ + pos_;
    pos_ += static_cast<std::size_t>(globalColors_) * 3;
  }
  firstFramePos_ = pos_;
  rewind();
  return true;
}

void MicroGif::rewind() {
  pos_ = firstFramePos_;
  transparent_ = -1;
  disposal_ = 0;
  pendingDelayMs_ = 0;
  prevDisposal_ = 0;
  prevW_ = prevH_ = 0;
  restore_.resize(0);
}

bool MicroGif::exceedsFrameCount(int limit) const {
  std::size_t at = firstFramePos_;
  int count = 0;
  const auto skipBlocks = [&]() -> bool {
    while (at < len_) {
      const std::size_t n = data_[at++];
      if (!n) return true;
      if (n > len_ - at) return false;
      at += n;
    }
    return false;
  };
  if (!data_) return false;
  while (at < len_) {
    const int tag = data_[at++];
    if (tag == 0x21) {
      if (at == len_) return false;
      ++at;  // Extension label; all extensions then use length-prefixed blocks.
      if (!skipBlocks()) return false;
    } else if (tag == 0x2C) {
      if (len_ - at < 9) return false;
      const int fw = data_[at + 4] | (data_[at + 5] << 8);
      const int fh = data_[at + 6] | (data_[at + 7] << 8);
      const int packed = data_[at + 8];
      if (fw > maxW_ || fh > maxH_ || !pixelCountFits(fw, fh)) return false;
      at += 9;
      if (packed & 0x80) {
        const std::size_t bytes = (1u << ((packed & 7) + 1)) * 3;
        if (bytes > len_ - at) return false;
        at += bytes;
      }
      if (at == len_ || data_[at] < 1 || data_[at] > 8) return false;
      ++at;
      if (++count > limit) return true;
      if (!skipBlocks()) return false;
    } else {
      return false;
    }
  }
  return false;
}

// Graphic Control Extension: transparency index, disposal method and the frame delay, which the
// format stores in hundredths of a second.
bool MicroGif::parseGce() {
  const int size = readByte();
  if (size < 4) return false;
  const std::size_t body = pos_;
  const int packed = readByte();
  const int delay = readWord();
  const int tci = readByte();
  if (packed < 0 || delay < 0 || tci < 0) return false;
  transparent_ = (packed & 0x01) ? tci : -1;
  disposal_ = (packed >> 2) & 7;
  if (disposal_ > 3) disposal_ = 0;
  pendingDelayMs_ = delay * 10;
  pos_ = body + static_cast<std::size_t>(size);
  if (pos_ > len_) return false;
  return skipSubBlocks();
}

MicroGif::Step MicroGif::nextFrame(Canvas& dst, int& delayMs, bool clearFirst) {
  delayMs = 0;
  if (!data_) return Step::kError;
  // Block dispatch: 0x3B trailer, 0x2C image descriptor, 0x21 extension. A truncated file is
  // treated as a clean end so half-written icons still animate what they have.
  for (;;) {
    const int b = readByte();
    if (b < 0 || b == 0x3B) return Step::kEnd;
    if (b == 0x2C) {
      const std::size_t imagePos = pos_ - 1;
      const Step st = decodeImage(dst, clearFirst);
      if (st == Step::kOom) {
        pos_ = imagePos;
        return st;  // Retry this frame with its GCE intact; the destination is untouched.
      }
      if (st == Step::kFrame) delayMs = pendingDelayMs_;
      transparent_ = -1;
      disposal_ = 0;
      pendingDelayMs_ = 0;
      return st;
    }
    if (b == 0x21) {
      const int ext = readByte();
      if (ext < 0) return Step::kError;
      if (ext == 0xF9) {
        if (!parseGce()) return Step::kError;
      } else {
        if (!skipSubBlocks()) return Step::kError;
      }
      continue;
    }
    return Step::kError;
  }
}

MicroGif::Step MicroGif::decodeImage(Canvas& dst, bool clearFirst) {
  const int fx = readWord();
  const int fy = readWord();
  const int fw = readWord();
  const int fh = readWord();
  const int packed = readByte();
  if (fx < 0 || fy < 0 || fw <= 0 || fh <= 0 || packed < 0) return Step::kError;
  if (fw > maxW_ || fh > maxH_ || !pixelCountFits(fw, fh)) return Step::kError;

  const uint8_t* pal = palette_;
  int colors = globalColors_;
  if (packed & 0x80) {
    colors = 1 << ((packed & 7) + 1);
    if (pos_ + static_cast<std::size_t>(colors) * 3 > len_) return Step::kError;
    pal = data_ + pos_;
    pos_ += static_cast<std::size_t>(colors) * 3;
  }
  const bool interlaced = (packed & 0x40) != 0;

  const int minCodeSize = readByte();
  if (minCodeSize < 1 || minCodeSize > 8) return Step::kError;
  const int npix = fw * fh;
  LzwScratch scratch;
  if (!scratch.allocate(npix, minCodeSize, scratch_)) return Step::kOom;
  if (!lzwDecode(minCodeSize, scratch, scratch.index, npix)) return Step::kError;

  int visibleW = (w_ < dst.width() ? w_ : dst.width()) - fx;
  int visibleH = (h_ < dst.height() ? h_ : dst.height()) - fy;
  visibleW = visibleW < 0 ? 0 : (visibleW < fw ? visibleW : fw);
  visibleH = visibleH < 0 ? 0 : (visibleH < fh ? visibleH : fh);
  const bool haveRestore = restore_.size() == static_cast<std::size_t>(prevW_) * prevH_;
  // Reserve before changing the destination, preserving the old snapshot if allocation fails.
  if (disposal_ == 3 && !restore_.resize(static_cast<std::size_t>(visibleW) * visibleH))
    return Step::kOom;

  if (clearFirst) dst.fillRect(0, 0, w_, h_, 0x000000u);

  if (prevDisposal_ == 2) {
    dst.fillRect(prevX_, prevY_, prevW_, prevH_, 0x000000u);
  } else if (prevDisposal_ == 3) {
    if (haveRestore) {
      const uint32_t* saved = restore_.data();
      for (int y = 0; y < prevH_; ++y)
        for (int x = 0; x < prevW_; ++x)
          dst.setPixel(prevX_ + x, prevY_ + y, *saved++);
    }
  }
  prevDisposal_ = 0;

  // A restore-to-previous frame needs the destination pixels from before it is composited. Keep
  // only its rectangle, not a second logical-screen canvas.
  if (disposal_ == 3) {
    uint32_t* saved = restore_.data();
    for (int y = 0; y < visibleH; ++y)
      for (int x = 0; x < visibleW; ++x) *saved++ = dst.getPixel(fx + x, fy + y);
  } else restore_.resize(0);

  for (int r = 0; r < fh; ++r) {
    const int y = interlaced ? interlacedRow(r, fh) : r;
    if (y >= visibleH) continue;
    const uint8_t* src = scratch.index + static_cast<std::size_t>(r) * fw;
    for (int x = 0; x < visibleW; ++x) {
      const int idx = src[x];
      if (idx == transparent_) continue;
      if (idx >= colors) continue;
      const uint8_t* rgb = pal + idx * 3;
      dst.setPixel(fx + x, fy + y, color::pack(rgb[0], rgb[1], rgb[2]));
    }
  }

  prevDisposal_ = disposal_;
  prevX_ = fx;
  prevY_ = fy;
  prevW_ = visibleW;
  prevH_ = visibleH;
  return Step::kFrame;
}

bool MicroGif::lzwDecode(int minCodeSize, LzwScratch& s, uint8_t* out, int npix) {
  const int clearCode = 1 << minCodeSize;
  const int endCode = clearCode + 1;
  int codeSize = minCodeSize + 1;
  int nextSlot = endCode + 1;
  int maxCode = 1 << codeSize;
  int prev = -1;
  int first = 0;
  uint32_t bitBuf = 0;
  int bitCnt = 0;
  int blockLeft = 0;
  bool terminated = false;
  int produced = 0;

  // Pulls the next code byte across the sub-block boundaries; a zero-length block ends the
  // stream and latches terminated so nothing reads past it.
  const auto nextByte = [&]() -> int {
    if (terminated) return -1;
    while (blockLeft == 0) {
      if (pos_ >= len_) {
        terminated = true;
        return -1;
      }
      blockLeft = data_[pos_++];
      if (blockLeft == 0) {
        terminated = true;
        return -1;
      }
    }
    if (pos_ >= len_) {
      terminated = true;
      return -1;
    }
    --blockLeft;
    return data_[pos_++];
  };

  while (produced < npix) {
    while (bitCnt < codeSize) {
      const int b = nextByte();
      if (b < 0) goto stream_done;
      bitBuf |= static_cast<uint32_t>(b) << bitCnt;
      bitCnt += 8;
    }
    {
      const int code = static_cast<int>(bitBuf & static_cast<uint32_t>(maxCode - 1));
      bitBuf >>= codeSize;
      bitCnt -= codeSize;

      if (code == clearCode) {
        codeSize = minCodeSize + 1;
        maxCode = 1 << codeSize;
        nextSlot = endCode + 1;
        prev = -1;
        continue;
      }
      if (code == endCode) break;

      int c = code;
      uint8_t* sp = s.stack;
      // The KwKwK case: an encoder may emit the very code it is about to define. Only the next
      // slot is legal, and its expansion is the previous string plus its own first byte.
      if (c >= nextSlot) {
        if (c != nextSlot || prev < 0) return false;
        *sp++ = static_cast<uint8_t>(first);
        c = prev;
      }
      while (c >= endCode + 1) {
        if (c >= nextSlot || c >= s.slots || sp - s.stack >= s.stackSize) return false;
        *sp++ = s.suffix[c - s.initial];
        c = s.prefix[c - s.initial];
      }
      if (c >= clearCode) return false;
      first = c;
      if (sp - s.stack >= s.stackSize) return false;
      *sp++ = static_cast<uint8_t>(c);

      if (prev >= 0 && nextSlot < s.slots) {
        s.prefix[nextSlot - s.initial] = static_cast<uint16_t>(prev);
        s.suffix[nextSlot - s.initial] = static_cast<uint8_t>(first);
        ++nextSlot;
        if (nextSlot == maxCode && codeSize < 12) {
          ++codeSize;
          maxCode = 1 << codeSize;
        }
      }
      prev = code;

      while (sp > s.stack && produced < npix) out[produced++] = *--sp;
    }
  }

stream_done:
  // Truncated or corrupt streams still produce a frame — the missing tail becomes transparent
  // where the frame has a transparent index, background colour otherwise.
  if (produced < npix)
    std::memset(out + produced,
                static_cast<uint8_t>(transparent_ >= 0 ? transparent_ : bgIndex_),
                static_cast<std::size_t>(npix - produced));

  // The decoder usually stops before consuming every code, so walk out the remaining sub-blocks
  // to leave pos_ on the next block header for the following frame.
  if (!terminated) {
    pos_ += static_cast<std::size_t>(blockLeft);
    for (;;) {
      if (pos_ >= len_) break;
      const int size = data_[pos_++];
      if (size == 0) break;
      pos_ += static_cast<std::size_t>(size);
    }
  }
  if (pos_ > len_) pos_ = len_;
  return true;
}

}
}
