#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/remote/RemoteWire.h"

namespace awtrix {
namespace remote {

struct OfferEntry {
  std::string name;
  uint32_t address = 0;
  uint16_t port = 0;
  int64_t seenMs = 0;
};

// The script hosts that offered themselves on the discovery port lately. A host is known by its
// name and address; a new offer from it refreshes the entry, and the oldest one makes room when
// the list is full.
class OfferList {
 public:
  static constexpr std::size_t kMax = 4;
  static constexpr int64_t kTtlMs = 90000;

  void offer(const wire::Offer& offer, uint32_t fromAddress, int64_t nowMs);
  std::vector<OfferEntry> current(int64_t nowMs) const;

 private:
  std::vector<OfferEntry> entries_;
};

}
}
