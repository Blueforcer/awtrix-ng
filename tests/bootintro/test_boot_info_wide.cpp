#include "../support.h"
#include "../../test/Visuals.h"
// drawBootInfoWide on the TC002's 52x16 panel: which addresses stand still, which scroll, where the
// version goes, and that the screen stays a pure function of the elapsed time inside its canvas.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/render/BootScreen.h"
#include "core/render/Canvas.h"
#include "platform/tc002/runtime/BootInfoWide.h"
#include "platform/tc002/runtime/TerminalFont.h"

using namespace awtrix;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

constexpr int kW = 52;
constexpr int kH = 16;
constexpr uint32_t kGuard = 0xDEADBEEFu;
constexpr std::size_t kGuardWords = 256;

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

test::InkBox label(const Canvas& canvas, const std::string& text) {
  Canvas mask(render::terminal::textWidth(text), render::terminal::kGlyphH);
  render::terminal::drawText(mask, 0, 0, text, 1);
  return test::findMask(canvas, mask, test::lit);
}

bool same(const Canvas& a, const Canvas& b) {
  return std::memcmp(a.data(), b.data(), a.size() * sizeof(uint32_t)) == 0;
}

void proportionalFont() {
  using render::terminal::textWidth;
  check(textWidth("") == 0, "empty text has no width");
  check(textWidth("dev") == textWidth("DEV"), "lower case reads as upper case");
  check(textWidth("192.168.255.255") <= kW, "every 192.168 address fits the panel");
}

void addressThatFits(const std::string& address) {
  const render::BootInfo info{"1.1.2", address};
  const int width = render::terminal::textWidth(address);
  Canvas c(kW, kH);
  check(render::drawBootInfoWide(c, info, 0, 0), address + ": showing at once");
  const auto version = label(c, info.version), message = label(c, address);
  check(version.found() && message.found(), address + ": version and complete address are readable");
  check(version.bottom < message.top, address + ": version is above the address");
  check(message.right - message.left + 1 == width, address + ": proportional address fits");
  check(std::abs(message.left - (c.width() - 1 - message.right)) <= 1, address + ": address is centered");
}

void addressThatScrolls() {
  const render::BootInfo info{"1.1.2", "192.168.178.123:8080"};
  check(render::terminal::textWidth(info.address) > kW, "fixture needs scrolling");
  Canvas first(kW, kH), later(kW, kH);
  check(render::drawBootInfoWide(first, info, 0, 700), "long address is showing");
  check(render::drawBootInfoWide(later, info, 0, 1100), "long address is still showing");
  check(!test::sameShape(first, later), "long address moves through the panel");
  check(label(first, info.version).found() && label(later, info.version).found(), "version remains readable while scrolling");
}

void versionOnly() {
  const render::BootInfo info{"1.1.2", ""};
  Canvas c(kW, kH);
  check(render::drawBootInfoWide(c, info, 0, 0), "version shows without an address");
  const auto text = label(c, info.version);
  check(text.found(), "complete version is readable");
  check(std::abs(text.top - (c.height() - 1 - text.bottom)) <= 1, "version is vertically centered");
}

void pureAndContained(int width, int height) {
  const std::string size = std::to_string(width) + "x" + std::to_string(height);
  const render::BootInfo info{"0.0.0-dev", "192.168.178.123:8080"};
  GuardedCanvas guarded(width, height);
  Canvas& a = guarded.canvas;
  Canvas b(width, height);
  const Canvas::ClipRect clip = a.clipRect();
  bool pure = true;
  for (int64_t t = 0; t < 6000; t += 37) {
    render::drawBootInfoWide(a, info, 0, t);
    for (std::size_t i = 0; i < b.size(); ++i) b.data()[i] = static_cast<uint32_t>(i * 2654435761u) & 0xFFFFFFu;
    render::drawBootInfoWide(b, info, 987654321, 987654321 + t);
    pure &= same(a, b);
  }
  check(pure, size + ": a frame depends only on nowMs - startMs, not on what the canvas held");
  check(guarded.guardsIntact(), size + ": nothing is written outside the canvas");
  const Canvas::ClipRect after = a.clipRect();
  check(after.left == clip.left && after.top == clip.top && after.right == clip.right && after.bottom == clip.bottom,
        size + ": the canvas clip is left as it was");
}

}

int main() {
  proportionalFont();
  addressThatFits("192.168.178.123");
  addressThatFits("192.168.255.255");
  addressThatFits("10.0.0.7");
  addressThatScrolls();
  versionOnly();
  pureAndContained(52, 16);
  pureAndContained(32, 8);
  pureAndContained(64, 32);
  pureAndContained(128, 32);
  if (failures) return 1;
  std::puts("boot info wide: ok");
  return 0;
}
