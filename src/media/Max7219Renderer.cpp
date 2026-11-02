#ifdef AWTRIX_DISPLAY_MAX7219

#include "media/MatrixRenderer.h"

#include <LEDMatrixDriver.hpp>

#include "system/Log.h"
#include "system/DisplayProbe.h"

namespace awtrix {

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

}

#endif
