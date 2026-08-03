#pragma once

#include "core/apps/ClockText.h"
#include "core/apps/IApp.h"
#include "core/apps/builtin/ContentFrame.h"
#include "core/apps/builtin/WeekdayBar.h"
#include "core/render/TextRenderer.h"

namespace awtrix {

class DateApp : public IApp {
 public:
  const std::string& id() const override { return id_; }

  void render(Canvas& c, const RenderCtx& ctx) override {
    const Settings& s = *ctx.settings;
    const std::string str = buildDateText(s, ctx.weekday, ctx.mday, ctx.month, ctx.year);
    const uint32_t col = s.dateColor.valueOr(s.textColor);
    const ContentFrame f = contentFrame(c, text::measure(*ctx.font, str).inkWidth());
    text::drawCenteredIn(c, *ctx.font, str, 6, col, f.x, f.width);
    if (s.weekdayBar.show)
      drawWeekdayBar(c, s.weekdayBar, ctx.weekday, f.x, f.width, 3, c.height() - 1);
  }

 private:
  std::string id_ = "Date";
};

}
