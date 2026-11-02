#include "../../support.h"

#include <cstdint>
#include <vector>

#include "platform/tc002/runtime/SampleHistory.h"

using awtrix::tc002::roundUpToDmaHalf;
using awtrix::tc002::SampleHistory;
using awtrix::test::check;

namespace {

constexpr uint32_t kCapacity = 96;

std::vector<int16_t> counting(int16_t from, int count) {
  std::vector<int16_t> samples;
  for (int i = 0; i < count; ++i) samples.push_back(static_cast<int16_t>(from + i));
  return samples;
}

void rounding() {
  check(roundUpToDmaHalf(0) == 0 && roundUpToDmaHalf(1) == 24 && roundUpToDmaHalf(24) == 24 &&
            roundUpToDmaHalf(25) == 48,
        "rounds up to whole DMA halves");
}

void holdsTheNewestSamples() {
  SampleHistory history(kCapacity);
  history.open();
  check(history.next() == 0 && history.backTo(0) == 0, "starts empty at position 0");

  history.append(0, counting(100, 48));
  check(history.next() == 48, "the next position follows the block");
  check(history.backTo(0) == 48, "all of it is held");
  check(history.backTo(48) == 0, "a mark at the end reaches back to nothing");
  check(history.backTo(47) == 24, "a mark reaches back in whole DMA halves");
  check(history.read(48, 3) == counting(100, 3), "the oldest samples first");
  check(history.read(2, 2) == counting(146, 2), "and up to the newest");
}

void wrapsAndCaps() {
  SampleHistory history(kCapacity);
  history.open();
  history.append(0, counting(0, 90));
  history.append(90, counting(90, 30));
  check(history.next() == 120, "the position does not stop at the capacity");
  check(history.backTo(0) == kCapacity, "a mark older than the history reaches only what is held");
  check(history.read(kCapacity, kCapacity) == counting(24, kCapacity), "a read crosses the end of the ring in order");
}

void futureMarkAndGap() {
  SampleHistory history(kCapacity);
  history.open();
  history.append(0, counting(0, 48));
  check(history.backTo(1000) == 0, "a mark ahead of the stream reaches nothing");
  history.append(100, counting(100, 24));
  check(history.next() == 124, "a gap moves the position");
  check(history.backTo(0) == 24, "and forgets what came before it");
  check(history.read(24, 24) == counting(100, 24), "only the block after the gap is left");
}

void positionWraps() {
  SampleHistory history(kCapacity);
  history.open();
  history.append(0, {});
  const uint32_t nearEnd = UINT32_MAX - 23;
  history.append(nearEnd, counting(0, 24));
  check(history.next() == 0, "the position wraps at 2^32");
  history.append(0, counting(24, 24));
  check(history.next() == 24, "a block after the wrap is no gap");
  check(history.backTo(nearEnd) == 48, "a mark before the wrap still reaches back");
  check(history.read(48, 48) == counting(0, 48), "across the wrap in order");
}

void releaseAndReopen() {
  SampleHistory history(kCapacity);
  history.open();
  history.append(0, counting(0, 48));
  history.release();
  check(history.next() == 48 && history.backTo(0) == 0, "a release keeps the position, and holds nothing");
  history.open();
  check(history.next() == 0 && history.backTo(0) == 0, "a new stream starts over");
  history.append(0, counting(7, 24));
  check(history.read(24, 24) == counting(7, 24), "and works");
}

}

int main() {
  rounding();
  holdsTheNewestSamples();
  wrapsAndCaps();
  futureMarkAndGap();
  positionWraps();
  releaseAndReopen();
  return awtrix::test::finish("tc002-sample-history");
}
