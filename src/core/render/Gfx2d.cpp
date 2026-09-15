#include "core/render/Gfx2d.h"

#include <algorithm>
#include <cstdlib>

#include "core/render/Color.h"

namespace awtrix {
namespace render {

namespace {

struct ChartRange {
  int min;
  int max;
  int span() const { return max - min; }
};

ChartRange chartRange(const std::vector<int>& data, std::size_t n, bool autoscale) {
  if (!autoscale) return {0, 8};
  // Seeding at 1/0 keeps zero on the chart and guarantees a non-zero span to divide by.
  int mx = 1, mn = 0;
  for (std::size_t i = 0; i < n; ++i) {
    mx = std::max(mx, data[i]);
    mn = std::min(mn, data[i]);
  }
  return {mn, mx};
}

std::size_t chartCount(const std::vector<int>& v) {
  return std::min(v.size(), kMaxChartPoints);
}

float chartT(int v, const ChartRange& r) {
  return static_cast<float>(v - r.min) / static_cast<float>(r.span());
}

}

void drawProgress(Canvas& c, int pct, const ColorSource& fill, uint32_t track, int x0) {
  if (pct < 0) return;
  x0 = std::max(x0, 0);
  const int w = c.width() - x0;
  if (w <= 0) return;
  const int prog = pct > 100 ? 100 : pct;
  const int y = c.height() - 1;
  const int filled = (prog * w) / 100;
  const float span = static_cast<float>(w > 1 ? w - 1 : 1);
  for (int x = 0; x < w; ++x)
    c.setPixel(x0 + x, y, x < filled ? fill.at(static_cast<float>(x) / span) : track);
}

void drawBars(Canvas& c, const std::vector<int>& values, const ColorSource& color, bool autoscale,
              int x0) {
  const int n = static_cast<int>(chartCount(values));
  if (n == 0) return;
  x0 = std::max(x0, 0);
  const int avail = c.width() - x0;
  if (avail <= 0) return;
  const ChartRange range = chartRange(values, n, autoscale);
  const int barW = std::max(1, (avail - (n - 1)) / n);
  const auto level = [&](int v) {
    const int l = ((v - range.min) * c.height()) / range.span();
    return std::min(std::max(l, 0), c.height());
  };
  // Bars grow away from wherever zero lands, so negative values hang below the baseline.
  const int zero = level(0);
  for (int i = 0; i < n; ++i) {
    const int l = level(values[i]);
    const int top = c.height() - std::max(l, zero);
    const int h = std::abs(l - zero);
    c.fillRect(x0 + i * (barW + 1), top, barW, h, color.at(chartT(values[i], range)));
  }
}

void drawLineChart(Canvas& c, const std::vector<int>& values, const ColorSource& color,
                   bool autoscale, int x0) {
  const int n = static_cast<int>(chartCount(values));
  if (n < 2) return;
  x0 = std::max(x0, 0);
  const int avail = c.width() - x0;
  if (avail <= 0) return;
  const ChartRange range = chartRange(values, n, autoscale);
  auto px = [&](int i) {
    int x = x0 + (i * (avail - 1)) / (n - 1);
    int h = ((values[i] - range.min) * (c.height() - 1)) / range.span();
    h = std::min(std::max(h, 0), c.height() - 1);
    return std::pair<int, int>(x, c.height() - 1 - h);
  };
  for (int i = 0; i + 1 < n; ++i) {
    auto a = px(i), b = px(i + 1);
    c.drawLine(a.first, a.second, b.first, b.second, color.at(chartT(values[i], range)));
  }
}

}
}
