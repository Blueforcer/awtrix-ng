#include "../support.h"
// Whatever drawBootIntroWide draws, it has to be a pure function of the elapsed time that stays
// inside its canvas and holds a lit final frame from kBootIntroWideMs on.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "platform/tc002/runtime/BootIntroWide.h"
#include "core/render/Canvas.h"
#include "media/AwtrixFontAdapter.h"

using namespace awtrix;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

uint64_t hashOf(const Canvas& c) {
  uint64_t hash = 1469598103934665603ull;
  for (std::size_t i = 0; i < c.size(); ++i)
    for (int shift = 0; shift < 24; shift += 8) {
      hash ^= (c.data()[i] >> shift) & 0xFFu;
      hash *= 1099511628211ull;
    }
  return hash;
}

int lit(const Canvas& c) {
  int count = 0;
  for (std::size_t i = 0; i < c.size(); ++i) count += c.data()[i] != 0;
  return count;
}

constexpr uint32_t kGuard = 0xDEADBEEFu;
constexpr std::size_t kGuardWords = 256;

// A canvas that borrows the middle of a larger buffer, so a write past either end is caught.
struct GuardedCanvas {
  std::vector<uint32_t> buffer;
  Canvas canvas;
  GuardedCanvas(int width, int height)
      : buffer(static_cast<std::size_t>(width) * height + 2 * kGuardWords, kGuard),
        canvas(width, height, buffer.data() + kGuardWords) {}
  bool guardsIntact() const {
    for (std::size_t i = 0; i < kGuardWords; ++i)
      if (buffer[i] != kGuard || buffer[buffer.size() - 1 - i] != kGuard) return false;
    return true;
  }
};

void wholeRun(int width, int height, bool panel) {
  const std::string size = std::to_string(width) + "x" + std::to_string(height);
  const int64_t endMs = render::kBootIntroWideMs + 600;
  GuardedCanvas guarded(width, height);
  Canvas& a = guarded.canvas;
  Canvas b(width, height);
  const Canvas::ClipRect clip = a.clipRect();
  std::vector<int64_t> samples{0, 1, render::kBootIntroWideMs - 1,
                               render::kBootIntroWideMs, render::kBootIntroWideMs + 1, endMs};
  for (int64_t t = 0; t <= endMs; t += 24) samples.push_back(t);
  std::sort(samples.begin(), samples.end());
  samples.erase(std::unique(samples.begin(), samples.end()), samples.end());
  std::map<int64_t, uint64_t> hashes;
  std::set<uint64_t> distinct;
  bool offsetInvariant = true;
  for (int64_t t : samples) {
    render::drawBootIntroWide(a, awtrixFont(), 0, t);
    render::drawBootIntroWide(b, awtrixFont(), 987654321, 987654321 + t);
    if (std::memcmp(a.data(), b.data(), a.size() * sizeof(uint32_t)) != 0) offsetInvariant = false;
    hashes[t] = hashOf(a);
    distinct.insert(hashes[t]);
  }
  check(offsetInvariant, size + ": a frame depends only on nowMs - startMs");
  check(guarded.guardsIntact(), size + ": nothing is written outside the canvas");
  const Canvas::ClipRect after = a.clipRect();
  check(after.left == clip.left && after.top == clip.top && after.right == clip.right &&
            after.bottom == clip.bottom,
        size + ": the canvas clip is left as it was");

  bool repeatable = true;
  for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
    const int64_t t = *it;
    for (std::size_t i = 0; i < a.size(); ++i) a.data()[i] = static_cast<uint32_t>(i * 2654435761u) & 0xFFFFFFu;
    render::drawBootIntroWide(a, awtrixFont(), 5000, 5000 + t);
    if (hashOf(a) != hashes.at(t)) repeatable = false;
  }
  check(repeatable, size + ": frames repeat in any order and ignore what the canvas held");

  const uint64_t last = hashes.at(render::kBootIntroWideMs);
  bool holds = true;
  for (int64_t t : samples)
    if (t >= render::kBootIntroWideMs) holds &= hashes.at(t) == last;
  render::drawBootIntroWide(a, awtrixFont(), 0, 3600000);
  holds &= hashOf(a) == last;
  check(holds, size + ": the final frame holds from kBootIntroWideMs on");
  if (!panel) return;

  render::drawBootIntroWide(a, awtrixFont(), 0, render::kBootIntroWideMs);
  check(lit(a) > 0, size + ": the final frame shows a logo");
  check(distinct.size() > 1, size + ": the intro animates (" + std::to_string(distinct.size()) + " distinct frames)");
  check(hashes[0] != last, size + ": the first frame differs from the final one");
  std::printf("%s: %zu distinct frames, final frame %d lit pixels, hash %016llx\n", size.c_str(), distinct.size(),
              lit(a), static_cast<unsigned long long>(last));
}

}

int main() {
  wholeRun(52, 16, true);
  wholeRun(32, 8, false);
  wholeRun(64, 32, false);
  wholeRun(128, 32, false);
  if (failures) return 1;
  std::puts("boot intro wide: ok");
  return 0;
}
