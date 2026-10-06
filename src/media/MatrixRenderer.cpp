#include "media/MatrixRenderer.h"

#define FASTLED_INTERNAL
#include <FastLED.h>
#include <esp_heap_caps.h>

#include "core/PinRules.h"
#include "system/Log.h"
#include "system/DisplayProbe.h"

namespace awtrix {

namespace {
CRGB* g_leds = nullptr;

// FastLED takes the data pin as a template argument, so every pin a board may use has to be
// instantiated at compile time. PinRules holds that list; the switch below dispatches into it.
template <int PIN>
void addLedsOnPin(int ledCount) { FastLED.addLeds<NEOPIXEL, PIN>(g_leds, ledCount); }
}

bool MatrixRenderer::begin(int pin, const MatrixLayout& layout, uint8_t brightness) {
  if (g_leds) return ready();
  layout_ = layout;
  // FastLED keeps this pointer for good, so the buffer is allocated once and never resized —
  // a changed panel count needs a reboot, not another begin().
  // Keep the driver's source in internal memory; its RMT implementation also needs an internal
  // RGB work buffer of the same size. No controller is registered until this allocation succeeds.
  const std::size_t bytes = static_cast<std::size_t>(layout_.ledCount()) * sizeof(CRGB);
  constexpr std::size_t kInternalReserve = 48u * 1024u;
  if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
      2u * bytes + kInternalReserve ||
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 2u * bytes)
    return false;
  g_leds = static_cast<CRGB*>(heap_caps_calloc(layout_.ledCount(), sizeof(CRGB),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!g_leds) return false;
  ledsAllocated_ = layout_.ledCount();
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
  // Brightness lives in the colour grade, so the driver's own global scale stays wide open;
  // applying it again here would dim the panel twice.
  FastLED.setBrightness(255);
  setBrightness(brightness);
  FastLED.clear(true);
  return true;
}

void MatrixRenderer::setBrightness(uint8_t brightness) {
  grade_.setBrightness(brightness);
}

int MatrixRenderer::xyToIndex(int x, int y) const { return layout_.xyToIndex(x, y); }

void MatrixRenderer::show(const Canvas& canvas) {
  if (!ready()) return;
  const uint32_t mappingStart = displayprobe::start();
  for (int y = 0; y < layout_.height(); ++y) {
    for (int x = 0; x < layout_.width(); ++x) {
      const int idx = xyToIndex(x, y);
      if (idx < 0 || idx >= layout_.ledCount() || idx >= ledsAllocated_) continue;
      const uint32_t c = grade_.applyPixel(canvas.getPixel(x, y));
      const render::DriverColor driver = render::colorForGrbDriver(c, layout_.panelColorOrder);
      g_leds[idx] = CRGB(driver.r, driver.g, driver.b);
    }
  }
  displayprobe::record(displayprobe::Phase::Mapping, mappingStart);
  const uint32_t outputStart = displayprobe::start();
  FastLED.show();
  displayprobe::record(displayprobe::Phase::Output, outputStart);
  displayprobe::presented();
}

}
