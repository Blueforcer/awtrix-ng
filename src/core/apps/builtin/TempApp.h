#pragma once

#include "core/apps/IApp.h"
#include "core/apps/SensorFormat.h"
#include "core/apps/builtin/BuiltinIcons.h"
#include "core/apps/builtin/ContentFrame.h"
#include "core/render/TextRenderer.h"

namespace awtrix {

class TempApp : public IApp {
 public:
  const std::string& id() const override { return id_; }
  void render(Canvas& c, const RenderCtx& ctx) override {
    const Settings& s = *ctx.settings;
    const std::string str =
        formatTemperature(ctx.runtime->temperatureC, s.useCelsius, ctx.runtime->tempDecimals);
    const uint32_t col = s.temperatureColor.valueOr(s.textColor);
    const int ink = text::measure(*ctx.font, str).inkWidth();
    const ContentFrame f = contentFrame(c, builtinicon::kSize + ink);
    builtinicon::draw(c, builtinicon::kThermometer, f.x, 0);
    text::drawCenteredIn(c, *ctx.font, str, 6, col, f.x + builtinicon::kSize,
                         f.width - builtinicon::kSize);
  }

 private:
  std::string id_ = "Temperature";
};

}
