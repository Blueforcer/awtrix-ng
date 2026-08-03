#include "core/script/HttpBodyFilter.h"

#include <algorithm>

namespace awtrix::script {

void HttpBodyFilter::begin(const std::string& find, std::size_t keep, std::size_t cap) {
  find_ = find.substr(0, kMaxHttpFind);
  tail_.clear();
  body_.clear();
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
    body_.append(scan, pos, take);
    want_ -= take;
    return;
  }

  const std::size_t take = std::min(want_, len);
  body_.append(data, take);
  want_ -= take;
}

}
