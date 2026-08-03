#include "media/MatrixRenderer.h"

#define FASTLED_INTERNAL
#include <FastLED.h>

#include "core/PinRules.h"
#include "core/render/Color.h"
#include "system/Log.h"

namespace awtrix {

namespace {
CRGB* g_leds = nullptr;

// FastLED takes the data pin as a template argument, so every pin a board may use has to be
// instantiated at compile time. PinRules holds that list; the switch below dispatches into it.
template <int PIN>
void addLedsOnPin(int ledCount) { FastLED.addLeds<NEOPIXEL, PIN>(g_leds, ledCount); }
}

void MatrixRenderer::begin(int pin, const MatrixLayout& layout, uint8_t brightness) {
  layout_ = layout;
  ledsAllocated_ = layout_.ledCount();
  // FastLED keeps this pointer for good, so the buffer is allocated once and never resized —
  // a changed panel count needs a reboot, not another begin().
  if (!g_leds) g_leds = new CRGB[ledsAllocated_];
  switch (pin) {
#define X(p) \
  case p:    \
    addLedsOnPin<p>(layout_.ledCount()); \
    break;
    AWTRIX_MATRIX_PIN_LIST(X)
#undef X
    default:
      logf("matrix: no compiled driver for pin %d, falling back to GPIO %d", pin,
           AWTRIX_MATRIX_FALLBACK_PIN);
      addLedsOnPin<AWTRIX_MATRIX_FALLBACK_PIN>(layout_.ledCount());
      break;
  }
  FastLED.setBrightness(brightness);
  FastLED.clear(true);
}

void MatrixRenderer::setBrightness(uint8_t brightness) { FastLED.setBrightness(brightness); }

int MatrixRenderer::xyToIndex(int x, int y) const { return layout_.xyToIndex(x, y); }

void MatrixRenderer::show(const Canvas& canvas) {
  for (int y = 0; y < layout_.height(); ++y) {
    for (int x = 0; x < layout_.width(); ++x) {
      const int idx = xyToIndex(x, y);
      if (idx < 0 || idx >= layout_.ledCount() || idx >= ledsAllocated_) continue;
      const uint32_t c = grade_.applyPixel(canvas.getPixel(x, y));
      g_leds[idx] = CRGB(color::red(c), color::green(c), color::blue(c));
    }
  }
  FastLED.show();
}

}
