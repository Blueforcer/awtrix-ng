#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/render/Canvas.h"
#include "core/render/ColorRamp.h"

namespace awtrix {
namespace render {

// Either a flat colour or a ramp sampled at t (0..1) along whatever is being drawn.
struct ColorSource {
  uint32_t flat = 0xFFFFFFu;
  const ColorRamp* ramp = nullptr;

  ColorSource(uint32_t c) : flat(c) {}
  ColorSource(uint32_t c, const ColorRamp* r) : flat(c), ramp(r) {}

  uint32_t at(float t) const { return ramp && ramp->valid() ? ramp->at(t) : flat; }
};

// Single row along the bottom of the canvas, from x0 to the right edge. pct above 100 is clamped,
// negative pct draws nothing at all.
void drawProgress(Canvas& c, int pct, const ColorSource& fill, uint32_t track, int x0);

void drawBars(Canvas& c, const std::vector<int>& values, const ColorSource& color, bool autoscale,
              int x0);

void drawLineChart(Canvas& c, const std::vector<int>& values, const ColorSource& color,
                   bool autoscale, int x0);

constexpr std::size_t kMaxChartPoints = 16;

}
}
