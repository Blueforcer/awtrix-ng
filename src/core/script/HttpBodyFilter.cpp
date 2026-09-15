#include "core/script/HttpBodyFilter.h"

#include <algorithm>

#include "core/script/ScriptHeap.h"

namespace awtrix::script {

// Grows by at most kBodyGrowStepBytes at a time and asks whether the block it is about to take
// is there before taking it, so an answer that outgrows the device stops here instead of in the
// allocator.
bool HttpBodyFilter::makeRoomFor(std::size_t take) {
  const std::size_t needed = body_.size() + take;
  if (needed <= body_.capacity()) return true;

  const std::size_t step = std::min(body_.capacity(), kBodyGrowStepBytes);
  const std::size_t ceiling = body_.size() + want_;
  std::size_t target = std::max(needed, body_.capacity() + step);
  if (target > ceiling) target = ceiling;

  // Only the new block is weighed. The one already held is allocated, so the free heap this is
  // measured against has it subtracted already -- adding it back would charge it twice, and on a
  // board without PSRAM that arithmetic refuses a cap the device can plainly meet.
  if (target > heap::growthBudget()) {
    outOfRoom_ = true;
    want_ = 0;
    return false;
  }

  // Grown by hand rather than by reserve(): the library rounds any request under twice the
  // current size up to twice the current size, which is the block this is here to avoid asking
  // for. A buffer built empty takes the size it is given.
  std::string bigger;
  bigger.reserve(target);
  bigger.assign(body_);
  body_.swap(bigger);
  return true;
}

void HttpBodyFilter::begin(const std::string& find, std::size_t keep, std::size_t cap) {
  find_ = find;
  tail_.clear();
  body_.clear();
  outOfRoom_ = false;
  searching_ = !find_.empty();
  if (searching_) {
    if (keep == 0) keep = kDefaultHttpKeep;
    want_ = std::min(keep, cap);
  } else {
    want_ = cap;
  }
}

void HttpBodyFilter::feed(const char* data, std::size_t len) {
  if (done() || len == 0) return;

  if (searching_) {
    std::string scan = tail_ + std::string(data, len);
    const std::size_t pos = scan.find(find_);
    if (pos == std::string::npos) {
      // Hold back one byte less than the needle: that is the longest prefix of it that could
      // still be sitting at the end of what we have seen, waiting for the rest.
      const std::size_t keepTail = std::min(scan.size(), find_.size() - 1);
      tail_.assign(scan, scan.size() - keepTail, keepTail);
      return;
    }
    searching_ = false;
    tail_.clear();
    const std::size_t take = std::min(want_, scan.size() - pos);
    if (!makeRoomFor(take)) return;
    body_.append(scan, pos, take);
    want_ -= take;
    return;
  }

  const std::size_t take = std::min(want_, len);
  if (!makeRoomFor(take)) return;
  body_.append(data, take);
  want_ -= take;
}

}
