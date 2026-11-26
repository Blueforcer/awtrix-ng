#include "core/remote/OfferList.h"

#include <algorithm>

namespace awtrix {
namespace remote {

void OfferList::offer(const wire::Offer& offer, uint32_t fromAddress, int64_t nowMs) {
  const std::string name(offer.name);
  for (OfferEntry& entry : entries_) {
    if (entry.name == name && entry.address == fromAddress) {
      entry.port = offer.port;
      entry.seenMs = nowMs;
      return;
    }
  }
  OfferEntry fresh{name, fromAddress, offer.port, nowMs};
  if (entries_.size() < kMax) {
    entries_.push_back(std::move(fresh));
    return;
  }
  auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                 [](const OfferEntry& a, const OfferEntry& b) { return a.seenMs < b.seenMs; });
  *oldest = std::move(fresh);
}

std::vector<OfferEntry> OfferList::current(int64_t nowMs) const {
  std::vector<OfferEntry> out;
  for (const OfferEntry& entry : entries_)
    if (nowMs - entry.seenMs <= kTtlMs) out.push_back(entry);
  return out;
}

}
}
