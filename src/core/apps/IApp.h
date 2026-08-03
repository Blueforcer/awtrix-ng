#pragma once

#include <cstdint>

#include <string>

#include "core/RuntimeState.h"
#include "core/Settings.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {

// Everything an app may read for one frame. The broken-down date fields are already local time;
// epochMs stays -1 until the clock has been set.
struct RenderCtx {
  const Settings* settings = nullptr;
  const RuntimeState* runtime = nullptr;
  const GfxFont* font = nullptr;
  const GfxFont* fonts[kFontCount] = {nullptr, nullptr};
  int64_t nowMs = 0;
  int hour = 0, minute = 0, second = 0, weekday = 0, mday = 1, month = 1, year = 2024;
  int64_t epochMs = -1;
};

// Built-in apps are long-lived and stateless: render() is called every frame while the app is on
// screen, and there is no create, start or stop hook to go with it.
class IApp {
 public:
  virtual ~IApp() = default;
  virtual const std::string& id() const = 0;
  virtual void render(Canvas& canvas, const RenderCtx& ctx) = 0;
};

}
