#include "platform/tc002/runtime/BootInfoWide.h"
#include "platform/tc002/contract/tc002_layout.h"

#include "core/render/Color.h"
#include "platform/tc002/runtime/TerminalFont.h"

namespace awtrix {
namespace render {

namespace {
constexpr int kDesignW = TC002_PANEL_WIDTH;
constexpr int kDesignH = TC002_PANEL_HEIGHT;
constexpr char kLabel[] = "VER";
constexpr int kVersionX = 16;
constexpr int kTopRowY = 1;
constexpr int kBottomRowY = 9;
constexpr int kMiddleRowY = 5;
}

bool drawBootInfoWide(Canvas& c, const BootInfo& info, int64_t startMs, int64_t nowMs) {
  const int64_t t = nowMs > startMs ? nowMs - startMs : 0;
  const int ox = (c.width() - kDesignW) / 2;
  const int oy = (c.height() - kDesignH) / 2;
  const bool hasAddress = !info.address.empty();
  bool showing = false;

  c.clear(color::kBlack);
  const ClipScope clip(c, ox, oy, kDesignW, kDesignH);
  if (!info.version.empty()) {
    const int w = kVersionX + terminal::textWidth(info.version);
    showing |= bootInfoLineShowing(kDesignW, w, t);
    const int x = ox + (w <= kDesignW ? 0 : bootInfoLineX(kDesignW, w, t));
    const int y = oy + (hasAddress ? kTopRowY : kMiddleRowY);
    terminal::drawText(c, x, y, kLabel, terminal::kPhosphorDim);
    terminal::drawText(c, x + kVersionX, y, info.version, terminal::kAmber);
  }
  if (hasAddress) {
    const int w = terminal::textWidth(info.address);
    showing |= bootInfoLineShowing(kDesignW, w, t);
    terminal::drawText(c, ox + bootInfoLineX(kDesignW, w, t), oy + kBottomRowY, info.address,
                       terminal::kPhosphor);
  }
  return showing;
}

}
}
