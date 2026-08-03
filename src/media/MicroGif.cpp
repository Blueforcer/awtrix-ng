#include "media/MicroGif.h"

#include <cstring>
#include <new>

#include "core/render/Color.h"

namespace awtrix {
namespace media {

namespace {

constexpr int kLzwSlots = 1024;
static_assert(MicroGif::kMaxW * MicroGif::kMaxH + 258 + 2 <= kLzwSlots,
              "LZW scratch is sized for the panel; grow kLzwSlots with it");

// Interlaced GIFs store rows out of order in four passes (every 8th from 0, every 8th from 4,
// every 4th from 2, every 2nd from 1). Maps decode order r to the row it belongs on.
int interlacedRow(int r, int h) {
  static const int kStart[4] = {0, 4, 2, 1};
  static const int kStep[4] = {8, 8, 4, 2};
  for (int p = 0; p < 4; ++p)
    for (int y = kStart[p]; y < h; y += kStep[p])
      if (r-- == 0) return y;
  return 0;
}

}

struct MicroGif::LzwScratch {
  uint16_t prefix[kLzwSlots];
  uint8_t suffix[kLzwSlots];
  uint8_t stack[kLzwSlots];
  uint8_t index[kMaxW * kMaxH];
  uint32_t localPal[256];
};

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

bool MicroGif::begin(const uint8_t* data, std::size_t len) {
  data_ = nullptr;
  w_ = h_ = 0;
  // 6-byte signature plus the 7-byte logical screen descriptor is the shortest legal header.
  if (!data || len < 13) return false;
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
  w_ = lw < kMaxW ? lw : kMaxW;
  h_ = lh < kMaxH ? lh : kMaxH;
  globalColors_ = 0;
  // Bit 7 of the packed field means a global color table follows, bits 0-2 hold its size as
  // log2(entries) - 1.
  if (packed & 0x80) {
    globalColors_ = 1 << ((packed & 7) + 1);
    if (pos_ + static_cast<std::size_t>(globalColors_) * 3 > len_) {
      data_ = nullptr;
      return false;
    }
    for (int i = 0; i < globalColors_; ++i) {
      palette_[i] = color::pack(data_[pos_], data_[pos_ + 1], data_[pos_ + 2]);
      pos_ += 3;
    }
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

MicroGif::Step MicroGif::nextFrame(Canvas& dst, int& delayMs) {
  delayMs = 0;
  if (!data_) return Step::kError;
  // Block dispatch: 0x3B trailer, 0x2C image descriptor, 0x21 extension. A truncated file is
  // treated as a clean end so half-written icons still animate what they have.
  for (;;) {
    const int b = readByte();
    if (b < 0 || b == 0x3B) return Step::kEnd;
    if (b == 0x2C) {
      const Step st = decodeImage(dst);
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

MicroGif::Step MicroGif::decodeImage(Canvas& dst) {
  const int fx = readWord();
  const int fy = readWord();
  const int fw = readWord();
  const int fh = readWord();
  const int packed = readByte();
  if (fx < 0 || fy < 0 || fw <= 0 || fh <= 0 || packed < 0) return Step::kError;
  if (fw > kMaxW || fh > kMaxH) return Step::kError;

  // ~5 KB of tables — far too much for the stack, and only ever one GIF decodes at a time, so
  // a single shared static beats allocating per frame.
  static LzwScratch s_scratch;
  LzwScratch* const scratch = &s_scratch;

  const uint32_t* pal = palette_;
  int colors = globalColors_;
  if (packed & 0x80) {
    colors = 1 << ((packed & 7) + 1);
    if (pos_ + static_cast<std::size_t>(colors) * 3 > len_) return Step::kError;
    for (int i = 0; i < colors; ++i) {
      scratch->localPal[i] = color::pack(data_[pos_], data_[pos_ + 1], data_[pos_ + 2]);
      pos_ += 3;
    }
    pal = scratch->localPal;
  }
  const bool interlaced = (packed & 0x40) != 0;

  // Disposal 2 is restore-to-background and 3 restore-to-previous; with no backbuffer to restore
  // from, both are approximated by blanking the area the last frame covered.
  if (prevDisposal_ == 2 || prevDisposal_ == 3)
    dst.fillRect(prevX_, prevY_, prevW_, prevH_, 0x000000u);

  const int minCodeSize = readByte();
  if (minCodeSize < 1 || minCodeSize > 8) return Step::kError;
  const int npix = fw * fh;
  if (!lzwDecode(minCodeSize, *scratch, scratch->index, npix)) return Step::kError;

  for (int r = 0; r < fh; ++r) {
    const int y = interlaced ? interlacedRow(r, fh) : r;
    const uint8_t* src = scratch->index + static_cast<std::size_t>(r) * fw;
    for (int x = 0; x < fw; ++x) {
      const int idx = src[x];
      if (idx == transparent_) continue;
      if (idx >= colors) continue;
      dst.setPixel(fx + x, fy + y, pal[idx]);
    }
  }

  prevDisposal_ = disposal_;
  prevX_ = fx;
  prevY_ = fy;
  prevW_ = fw;
  prevH_ = fh;
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
        if (sp - s.stack >= kLzwSlots) return false;
        *sp++ = s.suffix[c];
        c = s.prefix[c];
      }
      if (c >= clearCode) return false;
      first = c;
      if (sp - s.stack >= kLzwSlots) return false;
      *sp++ = static_cast<uint8_t>(c);

      if (prev >= 0 && nextSlot < kLzwSlots) {
        s.prefix[nextSlot] = static_cast<uint16_t>(prev);
        s.suffix[nextSlot] = static_cast<uint8_t>(first);
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
