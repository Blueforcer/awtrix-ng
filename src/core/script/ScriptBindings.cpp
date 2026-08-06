#include "core/script/ScriptBindings.h"

#include <string.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include <vector>

#include "AppConfig.h"
#include "berry.h"
#include "core/Settings.h"
#include "core/StrCase.h"
#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"
#include "core/apps/IApp.h"
#include "core/effects/EffectRegistry.h"
#include "core/effects/IEffect.h"
#include "core/render/Canvas.h"
#include "core/render/Color.h"
#include "core/render/Gfx2d.h"
#include "core/render/PaletteStore.h"
#include "core/render/ScrollText.h"
#include "core/render/TextEncoding.h"
#include "core/render/TextRenderer.h"
#include "core/script/BerryVM.h"
#include "core/script/HttpBodyFilter.h"
#include "core/script/HttpHeaders.h"
#include "core/script/Prelude.h"
#include "core/script/Regex.h"
#include "core/script/ScriptServices.h"
#include "core/script/ScrollBank.h"
#include "core/script/SharedState.h"

namespace awtrix::script {
namespace {

// What the bindings may touch during one entry into the VM. BindingScope fills it going in and
// clears it coming out, so a binding reached outside a scope sees nullptrs and does nothing.
struct Ctx {
  Canvas* canvas = nullptr;
  const RenderCtx* rctx = nullptr;
  std::string name;
  std::string storeFlush;
  std::string storeOwner;
  bool storeDirty = false;
  const GfxFont* font = nullptr;
  ScrollBank* scroll = nullptr;
};

Ctx g_ctx;
const ScriptServices* g_svc = nullptr;

// Handed back by the clock and text-measuring bindings when there is nothing to answer with
// -- no RenderCtx, no font. Scripts can tell it from a real reading; 0 would be a lie.
constexpr int kNoClock = -1;

// Berry arguments are 1-based, and a missing one is not an error here: every arg* helper
// answers a default instead, which is what lets bindings take optional trailing arguments.
int argInt(bvm* vm, int i) {
  if (i > be_top(vm)) return 0;
  if (be_isint(vm, i)) return static_cast<int>(be_toint(vm, i));
  if (be_isreal(vm, i)) return static_cast<int>(be_toreal(vm, i));
  if (be_isbool(vm, i)) return be_tobool(vm, i) ? 1 : 0;
  return 0;
}

uint32_t argColor(bvm* vm, int i) { return static_cast<uint32_t>(argInt(vm, i)); }

uint32_t argColorOr(bvm* vm, int i, uint32_t fallback) {
  return be_top(vm) >= i ? argColor(vm, i) : fallback;
}

float argRealOr(bvm* vm, int i, float fallback) {
  if (be_top(vm) < i) return fallback;
  if (be_isreal(vm, i)) return static_cast<float>(be_toreal(vm, i));
  if (be_isint(vm, i)) return static_cast<float>(be_toint(vm, i));
  return fallback;
}

bool argBoolOr(bvm* vm, int i, bool fallback) {
  if (be_top(vm) < i) return fallback;
  if (be_isbool(vm, i)) return be_tobool(vm, i);
  return argInt(vm, i) != 0;
}

int absIndex(bvm* vm, int idx) { return idx < 0 ? be_top(vm) + idx + 1 : idx; }

// A Berry list is an instance wrapping its storage in a member named ".p" -- there is no
// public accessor, so every list-reading helper here goes through that member. Answers the stack
// index of that storage, left for the caller to pop, or 0 for anything that is not a list.
int pushListStorage(bvm* vm, int idx) {
  const int at = absIndex(vm, idx);
  if (at <= 0 || at > be_top(vm) || !be_isinstance(vm, at)) return 0;
  if (!be_getmember(vm, at, ".p") || !be_islist(vm, -1)) {
    be_pop(vm, 1);
    return 0;
  }
  return be_top(vm);
}

std::vector<int> readIntList(bvm* vm, int idx, std::size_t cap) {
  std::vector<int> out;
  const int raw = pushListStorage(vm, idx);
  if (!raw) return out;
  const int n = be_data_size(vm, raw);
  for (int k = 0; k < n && out.size() < cap; ++k) {
    be_pushint(vm, k);
    be_getindex(vm, raw);
    int v = 0;
    if (be_isint(vm, -1))
      v = static_cast<int>(be_toint(vm, -1));
    else if (be_isreal(vm, -1))
      v = static_cast<int>(be_toreal(vm, -1));
    out.push_back(v);
    be_pop(vm, 2);
  }
  be_pop(vm, 1);
  return out;
}

std::vector<int> argIntList(bvm* vm, int i) {
  if (be_top(vm) < i) return {};
  return readIntList(vm, i, render::kMaxChartPoints);
}

std::string g_textRun;
std::vector<uint32_t> g_textRunColors;

bool isTextArg(bvm* vm, int i) {
  if (be_top(vm) < i) return false;
  if (be_isstring(vm, i)) return true;
  const int raw = pushListStorage(vm, i);
  if (raw) be_pop(vm, 1);
  return raw != 0;
}

bool readTextFragment(bvm* vm, int idx, std::string& out, uint32_t& color) {
  const int at = absIndex(vm, idx);
  if (at <= 0 || at > be_top(vm)) return false;
  if (be_isstring(vm, at)) {
    out = be_tostring(vm, at);
    return true;
  }
  const int raw = pushListStorage(vm, at);
  if (!raw) return false;
  const int n = be_data_size(vm, raw);
  bool got = false;
  if (n >= 1) {
    be_pushint(vm, 0);
    be_getindex(vm, raw);
    if (be_isstring(vm, -1)) {
      out = be_tostring(vm, -1);
      got = true;
    }
    be_pop(vm, 2);
  }
  if (got && n >= 2) {
    be_pushint(vm, 1);
    be_getindex(vm, raw);
    if (be_isint(vm, -1))
      color = static_cast<uint32_t>(be_toint(vm, -1));
    else if (be_isreal(vm, -1))
      color = static_cast<uint32_t>(be_toreal(vm, -1));
    be_pop(vm, 2);
  }
  be_pop(vm, 1);
  return got;
}

bool readTextArg(bvm* vm, int i, const GfxFont& font, uint32_t flat, std::string& out,
                 std::vector<uint32_t>* glyphColors) {
  out.clear();
  if (glyphColors) glyphColors->clear();
  if (be_top(vm) < i) return false;
  if (be_isstring(vm, i)) {
    out = be_tostring(vm, i);
    return true;
  }
  const int raw = pushListStorage(vm, i);
  if (!raw) return false;
  const int n = be_data_size(vm, raw);
  for (int k = 0; k < n; ++k) {
    be_pushint(vm, k);
    be_getindex(vm, raw);
    std::string part;
    uint32_t color = flat;
    const bool got = readTextFragment(vm, -1, part, color);
    be_pop(vm, 2);
    if (!got || part.empty()) continue;
    if (glyphColors) glyphColors->insert(glyphColors->end(), text::glyphCount(font, part), color);
    out += part;
  }
  be_pop(vm, 1);
  return true;
}

// Reads either palette shape a script may write: bare colours spread evenly, or [colour, pos]
// pairs with pos a percentage 0..100. Mixing them is refused; `placed` reports which it got.
bool readStopList(bvm* vm, int idx, std::vector<render::PaletteStop>& out, bool& placed) {
  out.clear();
  placed = false;
  std::size_t bare = 0;
  const int raw = pushListStorage(vm, idx);
  if (!raw) return false;
  const int n = be_data_size(vm, raw);
  for (int k = 0; k < n && out.size() < 16; ++k) {
    be_pushint(vm, k);
    be_getindex(vm, raw);
    render::PaletteStop stop{0u, 0};
    if (be_isinstance(vm, -1)) {
      const std::vector<int> pair = readIntList(vm, -1, 2);
      if (pair.size() != 2 || pair[1] < 0 || pair[1] > 100) {
        be_pop(vm, 3);
        return false;
      }
      stop.color = static_cast<uint32_t>(pair[0]);
      stop.pos = static_cast<uint8_t>(pair[1]);
      placed = true;
    } else if (be_isint(vm, -1) || be_isreal(vm, -1)) {
      stop.color = static_cast<uint32_t>(be_isint(vm, -1) ? be_toint(vm, -1)
                                                          : static_cast<int>(be_toreal(vm, -1)));
      ++bare;
    } else {
      be_pop(vm, 3);
      return false;
    }
    out.push_back(stop);
    be_pop(vm, 2);
  }
  be_pop(vm, 1);
  if (out.empty() || (placed && bare != 0)) {
    out.clear();
    return false;
  }
  return true;
}

std::shared_ptr<const render::Palette> paletteFromArg(bvm* vm, int idx) {
  std::vector<render::PaletteStop> stops;
  bool placed = false;
  if (!readStopList(vm, idx, stops, placed)) return nullptr;
  if (!placed) {
    std::vector<uint32_t> plain;
    plain.reserve(stops.size());
    for (const render::PaletteStop& s : stops) plain.push_back(s.color);
    return render::paletteFromStopList(plain.data(), plain.size());
  }
  std::stable_sort(stops.begin(), stops.end(),
                   [](const render::PaletteStop& a, const render::PaletteStop& b) {
                     return a.pos < b.pos;
                   });
  return render::paletteFromPositionedStopList(stops.data(), stops.size());
}

// Leaves map[key] alone on the stack and returns true, or pushes nothing and returns false --
// the caller owns the pop. The be_remove pair drops the map and key from under the value.
bool pushMapValue(bvm* vm, int mapIdx, const char* key) {
  if (be_top(vm) < mapIdx || !be_isinstance(vm, mapIdx)) return false;
  if (!be_getmember(vm, mapIdx, ".p")) {
    be_pop(vm, 1);
    return false;
  }
  if (!be_ismap(vm, -1)) {
    be_pop(vm, 1);
    return false;
  }
  be_pushstring(vm, key);
  const bool has = be_getindex(vm, -2);
  if (!has || be_isnil(vm, -1)) {
    be_pop(vm, 3);
    return false;
  }
  be_remove(vm, -2);
  be_remove(vm, -2);
  return true;
}

EffectSettings argEffectSettings(bvm* vm, int i) {
  EffectSettings s;
  if (be_top(vm) < i || !be_isinstance(vm, i)) return s;
  if (pushMapValue(vm, i, "speed")) {
    if (be_isreal(vm, -1))
      s.speed = static_cast<float>(be_toreal(vm, -1));
    else if (be_isint(vm, -1))
      s.speed = static_cast<float>(be_toint(vm, -1));
    s.hasSpeed = true;
    be_pop(vm, 1);
  }
  if (pushMapValue(vm, i, "palette")) {
    if (be_isstring(vm, -1)) {
      s.ramp.pal = render::paletteByName(be_tostring(vm, -1));
    } else if (be_isinstance(vm, -1)) {
      s.ramp.pal = paletteFromArg(vm, -1);
    }
    be_pop(vm, 1);
  }
  if (pushMapValue(vm, i, "blend")) {
    s.ramp.blend = be_tobool(vm, -1);
    be_pop(vm, 1);
  }
  return s;
}

int64_t nowMs() { return (g_svc && g_svc->monotonicMs) ? g_svc->monotonicMs() : 0; }

int64_t scriptFrame() { return nowMs() / 24; }

bool canDraw(bvm* vm, int argc) { return g_ctx.canvas != nullptr && be_top(vm) >= argc; }

uint32_t deviceTextColor() {
  const Settings* s = (g_svc && g_svc->settings) ? g_svc->settings() : nullptr;
  return s ? s->textColor : 0xFFFFFFu;
}

const GfxFont* activeFont() {
  if (g_ctx.font) return g_ctx.font;
  if (g_ctx.rctx && g_ctx.rctx->font) return g_ctx.rctx->font;
  return g_svc ? g_svc->fonts[0] : nullptr;
}

const GfxFont* fontSlot(int slot) {
  if (slot < 0 || slot >= kFontCount) return nullptr;
  if (g_ctx.rctx && g_ctx.rctx->fonts[slot]) return g_ctx.rctx->fonts[slot];
  return g_svc ? g_svc->fonts[slot] : nullptr;
}

// Selects the font for the rest of this VM entry only -- BindingScope resets it, so a draw()
// that switches to "large" starts the next frame back on the rotation's font.
int b_font(bvm* vm) {
  if (be_top(vm) < 1 || !be_isstring(vm, 1)) be_return_nil(vm);
  const std::string name = be_tostring(vm, 1);
  const int slot = name == "large" ? 1 : (name == "small" ? 0 : -1);
  if (const GfxFont* f = fontSlot(slot)) g_ctx.font = f;
  be_return_nil(vm);
}


const Canvas* panel() {
  if (g_ctx.canvas) return g_ctx.canvas;
  return g_svc ? g_svc->panel : nullptr;
}

int b_width(bvm* vm) {
  const Canvas* c = panel();
  be_pushint(vm, c ? c->width() : 0);
  be_return(vm);
}

int b_height(bvm* vm) {
  const Canvas* c = panel();
  be_pushint(vm, c ? c->height() : 0);
  be_return(vm);
}


int b_clear(bvm* vm) {
  if (g_ctx.canvas) g_ctx.canvas->clear(be_top(vm) >= 1 ? argColor(vm, 1) : 0x000000u);
  be_return_nil(vm);
}

int b_pixel(bvm* vm) {
  if (canDraw(vm, 3)) g_ctx.canvas->setPixel(argInt(vm, 1), argInt(vm, 2), argColor(vm, 3));
  be_return_nil(vm);
}

int b_line(bvm* vm) {
  if (canDraw(vm, 5))
    g_ctx.canvas->drawLine(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argInt(vm, 4),
                           argColor(vm, 5));
  be_return_nil(vm);
}

int b_rect(bvm* vm) {
  if (canDraw(vm, 5))
    g_ctx.canvas->drawRect(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argInt(vm, 4),
                           argColor(vm, 5));
  be_return_nil(vm);
}

int b_fill_rect(bvm* vm) {
  if (canDraw(vm, 5))
    g_ctx.canvas->fillRect(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argInt(vm, 4),
                           argColor(vm, 5));
  be_return_nil(vm);
}

int b_circle(bvm* vm) {
  if (canDraw(vm, 4))
    g_ctx.canvas->drawCircle(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argColor(vm, 4));
  be_return_nil(vm);
}

int b_fill_circle(bvm* vm) {
  if (canDraw(vm, 4))
    g_ctx.canvas->fillCircle(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3), argColor(vm, 4));
  be_return_nil(vm);
}

// Answers the pen advance in pixels so a script can chain runs; 0 when nothing was drawn.
int b_text(bvm* vm) {
  int adv = 0;
  const GfxFont* font = activeFont();
  if (canDraw(vm, 3) && font && isTextArg(vm, 3)) {
    text::TextPaint paint;
    paint.flat = argColorOr(vm, 4, deviceTextColor());
    if (readTextArg(vm, 3, *font, paint.flat, g_textRun, &g_textRunColors)) {
      if (!g_textRunColors.empty()) {
        paint.glyphColors = g_textRunColors.data();
        paint.glyphCount = g_textRunColors.size();
      }
      adv = text::drawRun(*g_ctx.canvas, *font, argInt(vm, 1), argInt(vm, 2), g_textRun, paint);
    }
  }
  be_pushint(vm, adv);
  be_return(vm);
}

int b_text_width(bvm* vm) {
  const GfxFont* font = activeFont();
  if (!font) {
    be_pushint(vm, kNoClock);
    be_return(vm);
  }
  int w = 0;
  if (readTextArg(vm, 1, *font, 0u, g_textRun, nullptr)) w = text::width(*font, g_textRun);
  be_pushint(vm, w);
  be_return(vm);
}

int b_text_ink_width(bvm* vm) {
  const GfxFont* font = activeFont();
  if (!font) {
    be_pushint(vm, kNoClock);
    be_return(vm);
  }
  int w = 0;
  if (readTextArg(vm, 1, *font, 0u, g_textRun, nullptr))
    w = text::measure(*font, g_textRun).inkWidth();
  be_pushint(vm, w);
  be_return(vm);
}

int b_icon(bvm* vm) {
  bool ok = false;
  if (canDraw(vm, 3) && g_svc && g_svc->icon && be_isstring(vm, 1)) {
    const int64_t nowMs = (g_svc && g_svc->monotonicMs) ? g_svc->monotonicMs() : 0;
    ok = g_svc->icon->draw(*g_ctx.canvas, be_tostring(vm, 1), argInt(vm, 2), argInt(vm, 3), nowMs);
  }
  be_pushbool(vm, ok);
  be_return(vm);
}


int b_rgb(bvm* vm) {
  be_pushint(vm, static_cast<bint>(color::fromRgb(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3))));
  be_return(vm);
}

int b_hsv(bvm* vm) {
  be_pushint(vm, static_cast<bint>(color::fromHsv(argInt(vm, 1), argInt(vm, 2), argInt(vm, 3))));
  be_return(vm);
}


bool argRamp(bvm* vm, int i, render::ColorRamp& out) {
  if (be_top(vm) < i) return false;
  if (be_isstring(vm, i)) {
    out.pal = render::paletteByName(be_tostring(vm, i));
    return static_cast<bool>(out.pal);
  }
  if (be_isinstance(vm, i)) {
    out.pal = paletteFromArg(vm, i);
    return static_cast<bool>(out.pal);
  }
  return false;
}

render::ColorSource argPaintOr(bvm* vm, int i, uint32_t dflt, render::ColorRamp& scratch) {
  if (argRamp(vm, i, scratch)) return render::ColorSource(dflt, &scratch);
  return render::ColorSource(argColorOr(vm, i, dflt));
}

int b_ramp_text(bvm* vm) {
  int adv = 0;
  render::ColorRamp ramp;
  if (canDraw(vm, 4) && activeFont() && be_isstring(vm, 3) &&
      argRamp(vm, 4, ramp)) {
    const std::string str = be_tostring(vm, 3);
    const int span = be_top(vm) >= 5 ? argInt(vm, 5) : 0;
    ramp.spanPx = static_cast<uint16_t>(span < 0 ? 0 : (span > 0xFFFF ? 0xFFFF : span));
    ramp.speed = argRealOr(vm, 6, 0.0f);
    text::TextPaint paint;
    paint.ramp = &ramp;
    paint.rampOriginPx = ramp.originAt(nowMs(), text::width(*activeFont(), str));
    adv = text::drawRun(*g_ctx.canvas, *activeFont(), argInt(vm, 1), argInt(vm, 2), str, paint);
  }
  be_pushint(vm, adv);
  be_return(vm);
}


bool mapString(bvm* vm, int i, const char* key, std::string& out) {
  if (!pushMapValue(vm, i, key)) return false;
  const bool got = be_isstring(vm, -1);
  if (got) out = be_tostring(vm, -1);
  be_pop(vm, 1);
  return got;
}

bool mapCount(bvm* vm, int i, const char* key, int& out) {
  if (!pushMapValue(vm, i, key)) return false;
  bool got = false;
  if (be_isint(vm, -1)) {
    const int v = static_cast<int>(be_toint(vm, -1));
    if (v >= 0) {
      out = v;
      got = true;
    }
  }
  be_pop(vm, 1);
  return got;
}

ScrollSpec argScrollSpec(bvm* vm, int i, int& repeat) {
  ScrollSpec spec;
  if (be_top(vm) < i || !be_isinstance(vm, i)) return spec;

  std::string word;
  if (mapString(vm, i, "mode", word)) spec.hasMode = scroll::parseMode(word.c_str(), spec.mode);
  if (mapString(vm, i, "direction", word))
    spec.hasDirection = scroll::parseDirection(word.c_str(), spec.direction);
  if (mapString(vm, i, "entry", word)) spec.hasEntry = scroll::parseEntry(word.c_str(), spec.entry);
  if (mapString(vm, i, "whenFits", word))
    spec.hasWhenFits = scroll::parseWhenFits(word.c_str(), spec.whenFits);

  spec.hasSpeed = mapCount(vm, i, "speed", spec.speed);
  spec.hasGap = mapCount(vm, i, "gap", spec.gap);
  spec.hasHoldMs = mapCount(vm, i, "holdMs", spec.holdMs);
  mapCount(vm, i, "repeat", repeat);
  return spec;
}

// Two call shapes, told apart by where the string sits: a full-width line at the standard
// baseline, or an explicit x/y/width box. Returns how many times the text has scrolled past.
int b_scroll_text(bvm* vm) {
  const GfxFont* font = activeFont();
  const Settings* settings = (g_svc && g_svc->settings) ? g_svc->settings() : nullptr;
  const ScrollDefaults defaults = settings ? settings->scrollDefaults : ScrollDefaults{};
  const int argc = be_top(vm);
  int cycles = 0;
  int textArg = 0;
  ScrollRun run;

  if (g_ctx.canvas && g_ctx.scroll && font) {
    if (isTextArg(vm, 1)) {
      textArg = 1;
      run.y = render::kTextBaseline;
      run.width = g_ctx.canvas->width();
      run.color = argColorOr(vm, 2, deviceTextColor());
      run.spec = argScrollSpec(vm, 3, run.repeat);
    } else if (argc >= 5 && isTextArg(vm, 4)) {
      textArg = 4;
      run.x = argInt(vm, 1);
      run.y = argInt(vm, 2);
      run.width = argInt(vm, 3);
      run.color = argColor(vm, 5);
      run.spec = argScrollSpec(vm, 6, run.repeat);
    }
  }

  if (textArg && run.width > 0 &&
      readTextArg(vm, textArg, *font, run.color, g_textRun, &g_textRunColors)) {
    if (!g_textRunColors.empty()) {
      run.glyphColors = g_textRunColors.data();
      run.glyphCount = g_textRunColors.size();
    }
    cycles = g_ctx.scroll->draw(*g_ctx.canvas, *font, g_textRun, run, defaults, nowMs());
  }
  be_pushint(vm, cycles);
  be_return(vm);
}

int b_progress(bvm* vm) {
  render::ColorRamp ramp;
  if (g_ctx.canvas && be_top(vm) >= 1)
    render::drawProgress(*g_ctx.canvas, argInt(vm, 1), argPaintOr(vm, 2, 0x00FF00u, ramp),
                         argColorOr(vm, 3, 0xFFFFFFu), 0);
  be_return_nil(vm);
}

int b_bar_chart(bvm* vm) {
  render::ColorRamp ramp;
  if (g_ctx.canvas)
    render::drawBars(*g_ctx.canvas, argIntList(vm, 1), argPaintOr(vm, 2, 0xFFFFFFu, ramp),
                     argBoolOr(vm, 3, true), 0);
  be_return_nil(vm);
}

int b_line_chart(bvm* vm) {
  render::ColorRamp ramp;
  if (g_ctx.canvas)
    render::drawLineChart(*g_ctx.canvas, argIntList(vm, 1), argPaintOr(vm, 2, 0xFFFFFFu, ramp),
                          argBoolOr(vm, 3, true), 0);
  be_return_nil(vm);
}

int b_render_from(bvm* vm, const EffectRegistry* reg) {
  bool ok = false;
  if (g_ctx.canvas && reg && be_top(vm) >= 1 && be_isstring(vm, 1)) {
    IEffect* fx = reg->find(be_tostring(vm, 1));
    if (fx) {
      fx->setSettings(argEffectSettings(vm, 2));
      fx->render(*g_ctx.canvas, fx->animationStep(nowMs()));
      ok = true;
    }
  }
  be_pushbool(vm, ok);
  be_return(vm);
}

int b_effect(bvm* vm) { return b_render_from(vm, g_svc ? g_svc->effects : nullptr); }
int b_overlay(bvm* vm) { return b_render_from(vm, g_svc ? g_svc->overlays : nullptr); }


const RuntimeState* runtime() {
  if (g_ctx.rctx && g_ctx.rctx->runtime) return g_ctx.rctx->runtime;
  return (g_svc && g_svc->runtime) ? g_svc->runtime() : nullptr;
}

void pushSensor(bvm* vm, bool present, float value) {
  if (present) be_pushreal(vm, static_cast<breal>(value));
  else be_pushnil(vm);
}

int b_temperature(bvm* vm) {
  const RuntimeState* rt = runtime();
  pushSensor(vm, rt && rt->hasTemperature, rt ? rt->temperatureC : 0.0f);
  be_return(vm);
}

int b_humidity(bvm* vm) {
  const RuntimeState* rt = runtime();
  pushSensor(vm, rt && rt->hasHumidity, rt ? rt->humidity : 0.0f);
  be_return(vm);
}

int b_pressure(bvm* vm) {
  const RuntimeState* rt = runtime();
  pushSensor(vm, rt && rt->hasPressure, rt ? rt->pressureHpa : 0.0f);
  be_return(vm);
}

int b_light(bvm* vm) {
  const RuntimeState* rt = runtime();
  pushSensor(vm, rt && rt->hasLightSensor, rt ? rt->lightLevel : 0.0f);
  be_return(vm);
}

int b_battery(bvm* vm) {
  const RuntimeState* rt = runtime();
  if (rt && rt->hasBattery) be_pushint(vm, static_cast<bint>(rt->batteryPercent));
  else be_pushnil(vm);
  be_return(vm);
}

int b_battery_volts(bvm* vm) {
  const RuntimeState* rt = runtime();
  pushSensor(vm, rt && rt->hasBattery, rt ? rt->batteryVoltage : 0.0f);
  be_return(vm);
}

int b_hour(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? g_ctx.rctx->hour : kNoClock);
  be_return(vm);
}

int b_minute(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? g_ctx.rctx->minute : kNoClock);
  be_return(vm);
}

int b_second(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? g_ctx.rctx->second : kNoClock);
  be_return(vm);
}

int b_epoch_ms(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? static_cast<bint>(g_ctx.rctx->epochMs) : kNoClock);
  be_return(vm);
}

int b_weekday(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? g_ctx.rctx->weekday : kNoClock);
  be_return(vm);
}

int b_day(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? g_ctx.rctx->mday : kNoClock);
  be_return(vm);
}

int b_month(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? g_ctx.rctx->month : kNoClock);
  be_return(vm);
}

int b_year(bvm* vm) {
  be_pushint(vm, g_ctx.rctx ? g_ctx.rctx->year : kNoClock);
  be_return(vm);
}

int b_now_ms(bvm* vm) {
  int64_t ms = (g_svc && g_svc->monotonicMs) ? g_svc->monotonicMs() : 0;
  be_pushint(vm, static_cast<bint>(ms));
  be_return(vm);
}

int b_version(bvm* vm) {
  be_pushstring(vm, AWTRIX_NG_VERSION);
  be_return(vm);
}


int b_http_request(bvm* vm) {
  bool ok = false;
  if (g_svc && g_svc->http && be_top(vm) >= 7 && be_isstring(vm, 2) && be_isstring(vm, 3) &&
      be_isstring(vm, 4) && be_isstring(vm, 5) && be_isstring(vm, 6)) {
    HttpRequest req;
    req.id = static_cast<uint32_t>(argInt(vm, 1));
    req.url = be_tostring(vm, 3);
    req.body = be_tostring(vm, 4);
    req.maxBytes = kMaxHttpBody;
    const std::string headerBlock = be_tostring(vm, 5);

    req.find = be_tostring(vm, 6);
    const int keep = argInt(vm, 7);
    req.keep = keep > 0 ? static_cast<std::size_t>(keep) : 0;

    if (normalizeMethod(be_tostring(vm, 2), req.method) &&
        req.body.size() <= kMaxHttpRequestBody && req.find.size() <= kMaxHttpFind &&
        keep >= 0 && parseHeaderBlock(headerBlock, req.headers))
      ok = g_svc->http->request(req);
    else if (g_svc->logDebug)
      g_svc->logDebug("script http: request rejected for " + req.url);
  }
  be_pushbool(vm, ok);
  be_return(vm);
}

int b_mqtt_publish(bvm* vm) {
  if (g_svc && g_svc->mqtt && be_top(vm) >= 2 && be_isstring(vm, 1) && be_isstring(vm, 2))
    g_svc->mqtt->publish(be_tostring(vm, 1), be_tostring(vm, 2));
  be_return_nil(vm);
}

int b_mqtt_subscribe(bvm* vm) {
  if (g_svc && g_svc->mqtt && be_top(vm) >= 1 && be_isstring(vm, 1))
    g_svc->mqtt->subscribe(be_tostring(vm, 1));
  be_return_nil(vm);
}

// Parks the serialised store for drainStoreFlush() to pick up; nothing reaches flash here.
// Only one write can be pending, and each one carries the app's whole map anyway.
int b_store_flush(bvm* vm) {
  if (be_top(vm) < 1 || !be_isstring(vm, 1)) be_return_nil(vm);

  const std::size_t len = static_cast<std::size_t>(be_strlen(vm, 1));
  if (len > kMaxStoreBytes) {
    if (g_svc && g_svc->log)
      g_svc->log("[script:" + g_ctx.name + "] store not saved: " + std::to_string(len) +
                 " bytes exceeds the " + std::to_string(kMaxStoreBytes) + " byte limit");
    be_return_nil(vm);
  }

  g_ctx.storeFlush = be_tostring(vm, 1);
  g_ctx.storeOwner = g_ctx.name;
  g_ctx.storeDirty = true;
  be_return_nil(vm);
}

SharedState* sharedState() { return g_svc ? g_svc->shared : nullptr; }

// Reads a shared-state address: "owner.key" names another app's value, a bare key one's own.
bool argQualified(bvm* vm, std::string& owner, std::string& key) {
  if (be_top(vm) < 1 || !be_isstring(vm, 1)) return false;
  SharedState::splitQualified(be_tostring(vm, 1), g_ctx.name, owner, key);
  return !owner.empty();
}

// Writes are always filed under the calling app -- the key argument is bare and cannot name
// an owner, so no script can write into another's namespace. A nil value erases the key.
int b_shared_set(bvm* vm) {
  bool ok = false;
  SharedState* st = sharedState();
  if (st && !g_ctx.name.empty() && be_top(vm) >= 1 && be_isstring(vm, 1)) {
    const std::string key = be_tostring(vm, 1);
    if (be_top(vm) < 2 || be_isnil(vm, 2)) {
      ok = SharedState::validKey(key);
      if (ok) st->erase(g_ctx.name, key);
    } else {
      SharedState::Value v;
      bool scalar = true;
      if (be_isbool(vm, 2))
        v = SharedState::Value::ofBool(be_tobool(vm, 2));
      else if (be_isint(vm, 2))
        v = SharedState::Value::ofInt(static_cast<int64_t>(be_toint(vm, 2)));
      else if (be_isreal(vm, 2))
        v = SharedState::Value::ofReal(static_cast<double>(be_toreal(vm, 2)));
      else if (be_isstring(vm, 2))
        v = SharedState::Value::ofStr(be_tostring(vm, 2));
      else
        scalar = false;
      if (scalar)
        ok = st->set(g_ctx.name, key, std::move(v), nowMs()) == SharedState::Status::Ok;
    }
  }
  be_pushbool(vm, ok);
  be_return(vm);
}

int b_shared_get(bvm* vm) {
  SharedState* st = sharedState();
  std::string owner, key;
  if (st && argQualified(vm, owner, key)) {
    if (const SharedState::Value* v = st->find(owner, key)) {
      switch (v->type) {
        case SharedState::Type::Int:
          be_pushint(vm, static_cast<bint>(v->i));
          break;
        case SharedState::Type::Real:
          be_pushreal(vm, static_cast<breal>(v->r));
          break;
        case SharedState::Type::Bool:
          be_pushbool(vm, v->i != 0);
          break;
        case SharedState::Type::Str:
          be_pushstring(vm, v->s.c_str());
          break;
      }
      be_return(vm);
    }
  }
  be_return_nil(vm);
}

int b_shared_age(bvm* vm) {
  SharedState* st = sharedState();
  std::string owner, key;
  if (st && argQualified(vm, owner, key)) {
    if (const SharedState::Value* v = st->find(owner, key)) {
      be_pushint(vm, static_cast<bint>(nowMs() - v->writtenMs));
      be_return(vm);
    }
  }
  be_return_nil(vm);
}

int b_shared_keys(bvm* vm) {
  SharedState* st = sharedState();
  std::string owner;
  if (be_top(vm) >= 1 && be_isstring(vm, 1)) owner = be_tostring(vm, 1);

  be_newobject(vm, "list");
  if (st) {
    for (const std::string& k : st->keys(owner)) {
      be_pushstring(vm, k.c_str());
      be_data_push(vm, -2);
      be_pop(vm, 1);
    }
  }
  be_pop(vm, 1);
  be_return(vm);
}

// One compiled pattern is cached across all scripts, which is what makes matchall() -- a
// fresh call per match -- affordable. Two scripts alternating patterns just recompile.
int b_re_search(bvm* vm) {
  static Regex cache;
  static std::string cachedPattern;
  static bool cachedOk = false;

  bool pushed = false;
  if (be_top(vm) >= 4 && be_isstring(vm, 1) && be_isstring(vm, 2)) {
    const std::string pattern(be_tostring(vm, 1), static_cast<size_t>(be_strlen(vm, 1)));
    const std::string text(be_tostring(vm, 2), static_cast<size_t>(be_strlen(vm, 2)));
    const int from = argInt(vm, 3);
    const bool anchored = argInt(vm, 4) != 0;

    if (!cachedOk || cachedPattern != pattern) {
      cachedPattern = pattern;
      cachedOk = cache.compile(pattern);
    }

    Regex::Span g[Regex::kMaxGroups];
    bool hit = false;
    if (cachedOk && from >= 0 && static_cast<size_t>(from) <= text.size()) {
      hit = anchored ? (from == 0 && cache.match(text, g, Regex::kMaxGroups))
                     : cache.searchFrom(text, static_cast<size_t>(from), g,
                                        Regex::kMaxGroups);
    }

    // Result is [endOffset, whole match, group1, ...]. The offset is only there so the
    // prelude's matchall() can advance; it strips it before a script ever sees the list.
    if (hit) {
      be_newobject(vm, "list");
      be_pushint(vm, g[0].end);
      be_data_push(vm, -2);
      be_pop(vm, 1);
      for (int i = 0; i < cache.groupCount(); ++i) {
        if (g[i].begin >= 0)
          be_pushnstring(vm, text.data() + g[i].begin,
                         static_cast<size_t>(g[i].end - g[i].begin));
        else
          be_pushnil(vm);
        be_data_push(vm, -2);
        be_pop(vm, 1);
      }
      be_pop(vm, 1);
      pushed = true;
    }
  }
  if (!pushed) be_pushnil(vm);
  be_return(vm);
}

int b_settings_get(bvm* vm) {
  SettingValue v;
  if (g_svc && g_svc->settings && be_top(vm) >= 1 && be_isstring(vm, 1)) {
    if (const Settings* s = g_svc->settings()) v = s->read(be_tostring(vm, 1));
  }
  switch (v.type) {
    case SettingValue::Type::Bool: be_pushbool(vm, v.b); break;
    case SettingValue::Type::Int: be_pushint(vm, static_cast<bint>(v.i)); break;
    case SettingValue::Type::Real: be_pushreal(vm, static_cast<breal>(v.f)); break;
    case SettingValue::Type::Text: be_pushstring(vm, v.s ? v.s : ""); break;
    case SettingValue::Type::None: be_pushnil(vm); break;
  }
  be_return(vm);
}

// Whether writing arg i would be a no-op. A script setting the same value every loop would
// otherwise queue a settings save every second, and settings live in flash.
bool settingUnchanged(const SettingValue& cur, bvm* vm, int i) {
  if (be_isnil(vm, i)) return !cur.has();
  switch (cur.type) {
    case SettingValue::Type::Bool:
      return be_isbool(vm, i) && cur.b == (be_tobool(vm, i) != 0);
    case SettingValue::Type::Int:
      return be_isint(vm, i) && cur.i == static_cast<long>(be_toint(vm, i));
    case SettingValue::Type::Real:
      return be_isreal(vm, i) && cur.f == static_cast<float>(be_toreal(vm, i));
    case SettingValue::Type::Text:
      return be_isstring(vm, i) && cur.s &&
             strcase::equalsIgnoreCase(cur.s, be_tostring(vm, i));
    case SettingValue::Type::None:
      return false;
  }
  return false;
}

bool encodeSetting(bvm* vm, int i, const char* key, std::string& json) {
  api::JsonWriter w(json);
  w.beginObject();
  w.key(key);
  if (be_isnil(vm, i)) w.null();
  else if (be_isbool(vm, i)) w.value(be_tobool(vm, i) != 0);
  else if (be_isint(vm, i)) w.value(static_cast<long long>(be_toint(vm, i)));
  else if (be_isreal(vm, i)) w.value(static_cast<double>(be_toreal(vm, i)));
  else if (be_isstring(vm, i)) w.value(be_tostring(vm, i));
  else return false;
  w.endObject();
  return true;
}

// Goes the long way round on purpose: the value is encoded as the same one-key JSON object
// PATCH /api/v1/settings takes, so validation and range rules stay in one place.
int b_settings_set(bvm* vm) {
  bool ok = false;
  const Settings* s = (g_svc && g_svc->settings) ? g_svc->settings() : nullptr;
  const char* key = (s && be_top(vm) >= 2 && be_isstring(vm, 1))
                        ? Settings::canonicalKey(be_tostring(vm, 1))
                        : nullptr;
  std::string json;
  if (key && encodeSetting(vm, 2, key, json)) {
    SettingsError err;
    if (Settings::validateRead(api::JsonReader(json), err)) {
      ok = settingUnchanged(s->read(key), vm, 2) ||
           (g_svc->setSettings && g_svc->setSettings(json));
    }
  }
  be_pushbool(vm, ok);
  be_return(vm);
}

int b_apply_case(bvm* vm) {
  if (be_top(vm) < 1 || !be_isstring(vm, 1)) be_return_nil(vm);
  const std::string in = be_tostring(vm, 1);
  const Settings* s = (g_svc && g_svc->settings) ? g_svc->settings() : nullptr;
  if (!s || !s->uppercase) {
    be_pushstring(vm, in.c_str());
    be_return(vm);
  }
  be_pushstring(vm, text::toUpperUtf8(in).c_str());
  be_return(vm);
}

int b_sound(bvm* vm) {
  bool ok = false;
  if (g_svc && g_svc->sound && be_top(vm) >= 1) {
    const int action = argInt(vm, 1);
    std::string payload;
    if (be_top(vm) >= 2 && be_isstring(vm, 2)) payload = be_tostring(vm, 2);
    if (action >= static_cast<int>(SoundAction::Play) &&
        action <= static_cast<int>(SoundAction::Stop))
      ok = g_svc->sound(static_cast<SoundAction>(action), payload);
  }
  be_pushbool(vm, ok);
  be_return(vm);
}

int b_notify(bvm* vm) {
  bool ok = false;
  if (g_svc && g_svc->notify && be_top(vm) >= 1 && be_isstring(vm, 1))
    ok = g_svc->notify(be_tostring(vm, 1));
  be_pushbool(vm, ok);
  be_return(vm);
}

int b_rotation_next(bvm* vm) {
  if (g_svc && g_svc->rotateNext) g_svc->rotateNext();
  be_return_nil(vm);
}

int b_rotation_prev(bvm* vm) {
  if (g_svc && g_svc->rotatePrevious) g_svc->rotatePrevious();
  be_return_nil(vm);
}

int b_rotation_show(bvm* vm) {
  bool ok = false;
  if (g_svc && g_svc->showApp && !g_ctx.name.empty()) ok = g_svc->showApp(g_ctx.name);
  be_pushbool(vm, ok);
  be_return(vm);
}

int b_rotation_hold(bvm* vm) {
  if (g_svc && g_svc->holdRotation && be_top(vm) >= 1)
    g_svc->holdRotation(be_tobool(vm, 1) != 0);
  be_return_nil(vm);
}

int b_log(bvm* vm) {
  if (g_svc && g_svc->log && be_top(vm) >= 1) {
    const char* msg = be_tostring(vm, 1);
    g_svc->log("[script:" + g_ctx.name + "] " + (msg ? msg : ""));
  }
  be_return_nil(vm);
}

int b_native_app(bvm* vm) {
  be_pushstring(vm, g_ctx.name.c_str());
  be_return(vm);
}

}

bool installBindings(BerryVM& vm, std::string& err) {
  bvm* b = vm.raw();
  if (!b) {
    err = "vm alloc failed";
    return false;
  }


  be_regfunc(b, "width", b_width);                  // width()
  be_regfunc(b, "height", b_height);                // height()
  be_regfunc(b, "clear", b_clear);                  // clear(color?)
  be_regfunc(b, "pixel", b_pixel);                  // pixel(x, y, color)
  be_regfunc(b, "line", b_line);                    // line(x0, y0, x1, y1, color)
  be_regfunc(b, "rect", b_rect);                    // rect(x, y, w, h, color)
  be_regfunc(b, "rect_fill", b_fill_rect);          // rect_fill(x, y, w, h, color)
  be_regfunc(b, "circle", b_circle);                // circle(cx, cy, r, color)
  be_regfunc(b, "circle_fill", b_fill_circle);      // circle_fill(cx, cy, r, color)
  be_regfunc(b, "text", b_text);                    // text(x, y, txt, color?)
  be_regfunc(b, "text_width", b_text_width);        // text_width(txt)
  be_regfunc(b, "text_ink_width", b_text_ink_width);  // text_ink_width(txt)
  be_regfunc(b, "font", b_font);                    // font(name)
  be_regfunc(b, "icon", b_icon);                    // icon(name, x, y)

  be_regfunc(b, "rgb", b_rgb);                      // rgb(r, g, b)
  be_regfunc(b, "hsv", b_hsv);                      // hsv(h, s, v)
  be_regfunc(b, "ramp_text", b_ramp_text);          // ramp_text(x, y, str, palette, span?, speed?)
  be_regfunc(b, "scroll_text", b_scroll_text);      // scroll_text(txt, color?, opts?)
  be_regfunc(b, "progress", b_progress);            // progress(pct, paint?, bg?)
  be_regfunc(b, "bar_chart", b_bar_chart);          // bar_chart(list, paint?, autoscale?)
  be_regfunc(b, "line_chart", b_line_chart);        // line_chart(list, paint?, autoscale?)
  be_regfunc(b, "effect", b_effect);                // effect(name, settings?)
  be_regfunc(b, "overlay", b_overlay);              // overlay(name, settings?)

  be_regfunc(b, "hour", b_hour);                    // hour()
  be_regfunc(b, "minute", b_minute);                // minute()
  be_regfunc(b, "second", b_second);                // second()
  be_regfunc(b, "epoch_ms", b_epoch_ms);            // epoch_ms()
  be_regfunc(b, "weekday", b_weekday);              // weekday()
  be_regfunc(b, "day", b_day);                      // day()
  be_regfunc(b, "month", b_month);                  // month()
  be_regfunc(b, "year", b_year);                    // year()
  be_regfunc(b, "now_ms", b_now_ms);                // now_ms()

  be_regfunc(b, "version", b_version);              // version()
  be_regfunc(b, "log", b_log);                      // log(value)

  // Everything below is raw plumbing the prelude wraps into the documented modules. The
  // leading underscore is what keeps these out of the generated script API reference.
  be_regfunc(b, "_native_app", b_native_app);
  be_regfunc(b, "_native_http_request", b_http_request);
  be_regfunc(b, "_native_mqtt_publish", b_mqtt_publish);
  be_regfunc(b, "_native_mqtt_subscribe", b_mqtt_subscribe);
  be_regfunc(b, "_native_store_flush", b_store_flush);
  be_regfunc(b, "_native_notify", b_notify);
  be_regfunc(b, "_native_settings_get", b_settings_get);
  be_regfunc(b, "_native_settings_set", b_settings_set);
  be_regfunc(b, "_native_apply_case", b_apply_case);
  be_regfunc(b, "_native_sound", b_sound);
  be_regfunc(b, "_native_rotation_next", b_rotation_next);
  be_regfunc(b, "_native_rotation_prev", b_rotation_prev);
  be_regfunc(b, "_native_rotation_show", b_rotation_show);
  be_regfunc(b, "_native_temperature", b_temperature);
  be_regfunc(b, "_native_humidity", b_humidity);
  be_regfunc(b, "_native_pressure", b_pressure);
  be_regfunc(b, "_native_light", b_light);
  be_regfunc(b, "_native_battery", b_battery);
  be_regfunc(b, "_native_battery_volts", b_battery_volts);
  be_regfunc(b, "_native_rotation_hold", b_rotation_hold);
  be_regfunc(b, "_native_shared_set", b_shared_set);
  be_regfunc(b, "_native_shared_get", b_shared_get);
  be_regfunc(b, "_native_shared_age", b_shared_age);
  be_regfunc(b, "_native_shared_keys", b_shared_keys);
  be_regfunc(b, "_native_re_search", b_re_search);

  // Last, because the prelude's top level calls into the names registered above.
  if (!vm.loadSolidifiedPrelude()) {
    err = "prelude: " + vm.lastError();
    return false;
  }
  return true;
}

void setServices(const ScriptServices* s) { g_svc = s; }

const ScriptServices* services() { return g_svc; }

BindingScope::BindingScope(Canvas* canvas, const RenderCtx* ctx, const std::string& name,
                           ScrollBank* scroll) {
  g_ctx.canvas = canvas;
  g_ctx.rctx = ctx;
  g_ctx.name = name;
  g_ctx.font = nullptr;
  g_ctx.scroll = scroll;
}

// Note what is NOT cleared: a pending store write outlives the scope on purpose, because the
// host drains it after the call returns and needs to know which script it came from.
BindingScope::~BindingScope() {
  g_ctx.canvas = nullptr;
  g_ctx.rctx = nullptr;
  g_ctx.name.clear();
  g_ctx.font = nullptr;
  g_ctx.scroll = nullptr;
}

bool BindingScope::storeFlushPending() { return g_ctx.storeDirty; }

BindingScope::StoreFlush BindingScope::takeStoreFlush() {
  if (!g_ctx.storeDirty) return StoreFlush{};
  g_ctx.storeDirty = false;
  StoreFlush out{std::move(g_ctx.storeOwner), std::move(g_ctx.storeFlush)};
  g_ctx.storeOwner.clear();
  g_ctx.storeFlush.clear();
  return out;
}

const std::string& BindingScope::currentScript() { return g_ctx.name; }

}
