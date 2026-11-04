#include "media/MatrixRenderer.h"

#define FASTLED_INTERNAL
#include <FastLED.h>
#include <esp_heap_caps.h>

#include "core/PinRules.h"
#include "system/Log.h"
#include "system/DisplayProbe.h"

#ifdef AWTRIX_DISPLAY_MAX7219
#include <LEDMatrixDriver.hpp>
#endif

namespace awtrix {

#ifdef AWTRIX_DISPLAY_MAX7219

// Monochrome build: a chain of MAX7219 8x8 modules on the default SPI bus (SCK 18, MOSI 23) with
// chip select on AWTRIX_MAX7219_CS_PIN. The canvas stays full colour; a pixel lights when its
// average channel clears a threshold, and brightness drives the chips' intensity register instead
// of scaling colours (scaled colours would just fall under the threshold).
#ifndef AWTRIX_MAX7219_CS_PIN
#define AWTRIX_MAX7219_CS_PIN 5
#endif

namespace {
LEDMatrixDriver* g_lmd = nullptr;
constexpr int kMonoThreshold = 32;

bool pixelOn(uint32_t c) {
  const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  return (r + g + b) / 3 > kMonoThreshold;
}
}

bool MatrixRenderer::begin(int /*pin*/, const MatrixLayout& layout, uint8_t brightness) {
  if (g_lmd) return ready();
  layout_ = layout;
  const int segments = layout_.width() / 8;
  g_lmd = new LEDMatrixDriver(segments, AWTRIX_MAX7219_CS_PIN);
  ledsAllocated_ = layout_.ledCount();
  logf("matrix: MAX7219 x%d, CS GPIO %d", segments, AWTRIX_MAX7219_CS_PIN);
  g_lmd->clear();
  setBrightness(brightness);
  g_lmd->display();
  return true;
}

void MatrixRenderer::setBrightness(uint8_t brightness) {
  static int applied = -1;
  if (!g_lmd || applied == brightness) return;
  applied = brightness;
  // Intensity 0 is still lit on a MAX7219, so only a real zero shuts the chips down.
  g_lmd->setEnabled(brightness > 0);
  g_lmd->setIntensity(static_cast<uint8_t>(brightness * 15 / 255));
}

int MatrixRenderer::xyToIndex(int x, int y) const { return layout_.xyToIndex(x, y); }

void MatrixRenderer::show(const Canvas& canvas) {
  if (!ready()) return;
  const int W = layout_.width(), H = layout_.height();
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      // The modules are addressed by canvas position; of the panel settings only the picture
      // orientation applies, the rest describes LED strip wiring.
      int px = layout_.mirror ? W - 1 - x : x, py = y;
      if (layout_.rotate180) {
        px = W - 1 - px;
        py = H - 1 - py;
      }
      g_lmd->setPixel(px, py, pixelOn(canvas.getPixel(x, y)));
    }
  }
  g_lmd->display();
  displayprobe::presented();
}

#else

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

#endif

}
