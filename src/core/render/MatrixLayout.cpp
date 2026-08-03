#include "core/render/MatrixLayout.h"

namespace awtrix {

namespace {

// Position within a single panel. A "run" is one wired row (or column) and "step" the position
// along it; on serpentine panels every odd run is reversed because the strip snakes back.
int runIndex(int cx, int cy, int w, int h, bool vertical, bool serpentine, bool bottomStart,
             bool rightStart) {
  if (vertical) {
    const int run = rightStart ? (w - 1 - cx) : cx;
    const int step = (serpentine && (run & 1)) ? (h - 1 - cy) : cy;
    return run * h + (bottomStart ? (h - 1 - step) : step);
  }
  const int run = bottomStart ? (h - 1 - cy) : cy;
  const int step = (serpentine && (run & 1)) ? (w - 1 - cx) : cx;
  return run * w + (rightStart ? (w - 1 - step) : step);
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool sameLayout(const MatrixLayout& a, const MatrixLayout& b) {
  return a.panelWidth == b.panelWidth && a.panels == b.panels && a.panelStart == b.panelStart &&
         a.panelWiring == b.panelWiring && a.panelSerpentine == b.panelSerpentine;
}

}

int MatrixLayout::xyToIndex(int x, int y) const {
  const int W = width(), H = height();
  if (mirror) x = W - 1 - x;
  if (rotate180) {
    x = W - 1 - x;
    y = H - 1 - y;
  }
  // The parity below is taken on the panel's place along the cable, not on the raw column, so
  // reversing the chain does not also change which panels count as rotated.
  const int panel = chainReversed() ? (panels - 1 - x / panelWidth) : (x / panelWidth);
  int cx = x % panelWidth, cy = y;
  // Every second panel along the cable is mounted upside down, so its own coordinates turn with it.
  if (panelChainSerpentine && (panel & 1)) {
    cx = panelWidth - 1 - cx;
    cy = H - 1 - cy;
  }
  const int local = runIndex(cx, cy, panelWidth, H, panelWiring == Wiring::Columns,
                             panelSerpentine, bottomStart(), rightStart());
  return panel * (panelWidth * H) + local;
}

MatrixLayout sanitizeMatrixLayout(MatrixLayout in, bool* changed) {
  MatrixLayout out = in;
  out.panelWidth = clampInt(in.panelWidth, 1, kMatrixWidthMax);
  out.panels = clampInt(in.panels, 1, kMatrixWidthMax);
  if (static_cast<int>(in.panelStart) >= kPanelStartCount) out.panelStart = PanelStart::TopLeft;
  if (static_cast<int>(in.panelWiring) >= kWiringCount) out.panelWiring = Wiring::Rows;

  // A width/panel-count pair that lands outside the supported range is unusable, so fall back to
  // the stock layout and keep only the orientation flags the user set.
  if (out.width() < kMatrixWidthMin || out.width() > kMatrixWidthMax) {
    MatrixLayout fallback;
    fallback.mirror = out.mirror;
    fallback.rotate180 = out.rotate180;
    out = fallback;
  }
  if (changed) *changed = !sameLayout(in, out);
  return out;
}

}
