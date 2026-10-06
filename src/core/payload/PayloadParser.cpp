#include "core/payload/PayloadParser.h"

#include <cctype>
#include <limits>
#include <type_traits>
#include <cstring>

#include "core/JsonColor.h"
#include "core/api/JsonText.h"
#include "core/StrCase.h"
#include "core/icons/IconSource.h"
#include "core/payload/Base64.h"
#include "core/payload/EffectSettingsJson.h"
#include "core/payload/PaletteJson.h"
#include "core/render/MatrixLayout.h"
#include "core/sound/SoundSpec.h"

namespace awtrix {
namespace payload {

namespace {
const KeyHandler* keyHandlers = nullptr;
const KeyHandler* keyHandler(std::string_view key) {
  if (keyHandlers)
    for (auto* handler = keyHandlers; handler->key; ++handler)
      if (key == handler->key) return handler;
  return nullptr;
}

const char* const kIconModeNames[] = {"fixed", "pushOnce", "push"};
const char* const kLifetimeExpiryNames[] = {"remove", "mark"};

// The full set of accepted keys. Unknown keys are rejected rather than ignored, and the
// notification-only keys below are refused on a plain app.
const char* const kAppKeys[] = {
    "text", "textCase", "font", "textInFront", "textAlign", "textCenter", "textColor",
    "textBlinkMs", "textFadeMs", "textOffsetX",
    "backgroundColor", "icon", "icons", "iconMode", "iconOffsetX", "iconGap",
    "durationMs", "scroll", "repeat", "lifetimeMs", "lifetimeExpiry",
    "palette", "paletteBlend", "paletteSpan", "paletteSpeed",
    "barChart", "lineChart", "chartAutoscale", "chartColor",
    "progress", "progressColor", "progressTrackColor",
    "effect", "effectSpeed", "overlay", "draw",
};

const char* const kNotificationKeys[] = {
    "name", "hold", "stack", "wakeup", "sound",
};

bool keyAllowed(const char* k, bool isNotification) {
  for (const char* a : kAppKeys)
    if (std::strcmp(k, a) == 0) return true;
  if (keyHandler(k)) return true;
  if (!isNotification) return false;
  for (const char* n : kNotificationKeys)
    if (std::strcmp(k, n) == 0) return true;
  return false;
}

// argc is the number of arguments after the command name, numeric how many of those are
// coordinates, and takesColor whether one extra trailing color argument is allowed.
struct DrawSpec {
  const char* name;
  DrawKind kind;
  int argc;
  int numeric;
  bool takesColor;
};

const DrawSpec kDrawSpecs[] = {
    {"pixel",      DrawKind::Pixel,      2, 2, true},
    {"line",       DrawKind::Line,       4, 4, true},
    {"rect",       DrawKind::Rect,       4, 4, true},
    {"rectFill",   DrawKind::FillRect,   4, 4, true},
    {"circle",     DrawKind::Circle,     3, 3, true},
    {"circleFill", DrawKind::FillCircle, 3, 3, true},
    {"text",       DrawKind::Text,       3, 2, true},
    {"bitmap",     DrawKind::Bitmap,     5, 4, false},
};

bool drawError(DispatchDetail* err, std::size_t index, const char* why) {
  if (err) {
    err->field = "draw[" + std::to_string(index) + "]";
    err->message = why;
  }
  return false;
}

std::string readText(api::JsonReader r) {
  std::string_view sv;
  if (r.stringView(sv)) return std::string(sv);
  std::string decoded;
  if (!r.appendString(decoded)) return std::string();
  return decoded;
}

bool readColorAt(api::JsonReader r, const char* field, uint32_t& out, DispatchDetail* err) {
  if (color::readColor(r, out)) return true;
  if (err) {
    err->field = field;
    err->message = "invalid color";
  }
  return false;
}

// The string "palette" means "take the color from the app palette" instead of naming a color.
bool readPaintAt(api::JsonReader r, const char* field, uint32_t& out, bool& usesPalette,
                 DispatchDetail* err) {
  usesPalette = color::isPaletteWord(r);
  return usesPalette || readColorAt(r, field, out, err);
}

bool readPaletteAt(api::JsonReader r, render::ColorRamp& out, DispatchDetail* err) {
  if (readPalette(r, out)) return true;
  if (err) {
    err->field = "palette";
    err->message = "unknown palette";
  }
  return false;
}

template <typename E, std::size_t N>
bool readEnumAt(api::JsonReader r, const char* field, const char* const (&names)[N], E& out,
                DispatchDetail* err) {
  std::string s;
  if (r.isString() && r.appendString(s)) {
    for (std::size_t i = 0; i < N; ++i)
      if (s == names[i]) {
        out = static_cast<E>(i);
        return true;
      }
  }
  if (err) {
    err->field = field;
    err->message = enumNameChoices(names, static_cast<int>(N));
  }
  return false;
}

bool readIntAt(api::JsonReader r, int& out) {
  long long v = 0;
  if (!r.isNumber() || !r.asLong(v)) return false;
  out = static_cast<int>(v);
  return true;
}

// Counts on a copy of the cursor first so the vector allocates once. Entries past cap are
// dropped and anything that is not a number becomes 0.
void readIntArrayCur(api::JsonReader r, std::vector<int>& out, std::size_t cap) {
  if (!r.isArray()) return;
  api::JsonReader counter = r;
  std::size_t n = 0;
  if (counter.enterArray()) {
    while (counter.nextElement()) {
      ++n;
      if (!counter.skipValue()) break;
    }
  }
  out.reserve(n < cap ? n : cap);
  if (!r.enterArray()) return;
  while (r.nextElement()) {
    if (out.size() >= cap) break;
    long long v = 0;
    out.push_back(r.isNumber() && r.asLong(v) ? static_cast<int>(v) : 0);
    if (!r.skipValue()) break;
  }
}

// Base64 form is raw RGB, three bytes per pixel; the array form takes any accepted color value.
bool readBitmapData(api::JsonReader r, std::size_t index, render::DrawProgram& p,
                    DispatchDetail* err) {
  if (r.isString()) {
    std::size_t size = 0;
    if (!r.copyString(nullptr, std::numeric_limits<std::size_t>::max(), size))
      return drawError(err, index, "invalid base64");
    if (size == 0) return true;
    const std::size_t first = p.data.size();
    const std::size_t slots = size / 4 + (size % 4 != 0);
    if (slots > std::numeric_limits<std::size_t>::max() - first ||
        !p.data.resize(first + slots)) return drawError(err, index, "out of memory");
    char* b64 = reinterpret_cast<char*>(p.data.data() + first);
    if (!r.copyString(b64, size, size) || !base64::valid(b64, size))
      return drawError(err, index, "invalid base64");
    const std::size_t pixels = base64::unpadded(b64, size) / 4;
    // Four base64 characters become one RGB word in the same checked storage.
    for (std::size_t i = 0; i < pixels; ++i) {
      uint32_t rgb = 0;
      for (std::size_t j = 0; j < 4; ++j)
        rgb = (rgb << 6) | static_cast<uint32_t>(base64::sextet(b64[4 * i + j]));
      p.data[first + i] = rgb;
    }
    p.data.resize(first + pixels);
    return true;
  }
  if (r.isArray()) {
    if (!r.enterArray()) return drawError(err, index, "expected base64 or colors");
    while (r.nextElement()) {
      uint32_t c = 0u;
      if (!color::readColor(r, c))
        return drawError(err, index, "invalid color");
      if (!p.data.push_back(c)) return drawError(err, index, "out of memory");
      if (!r.skipValue()) break;
    }
    return true;
  }
  return drawError(err, index, "expected base64 or colors");
}

// Enough slots for the longest command: bitmap takes a name, x, y, w, h and the pixel data.
constexpr int kMaxDrawSlots = 7;

// Shape is ["pixels", color, x, y, x, y, ...]. A null color means inherit the app text color.
bool readPixels(api::JsonReader arr, std::size_t index, render::DrawCommand& cmd,
                render::DrawProgram& p, DispatchDetail* err) {
  api::JsonReader counter = arr;
  std::size_t n = 0;
  if (counter.enterArray()) {
    while (counter.nextElement()) {
      ++n;
      if (!counter.skipValue()) break;
    }
  }
  if (n < 4) return drawError(err, index, "expected color and x, y pairs");
  if ((n - 2) % 2 != 0)
    return drawError(err, index, "expected color and x, y pairs");
  if (!p.data.reserve(p.data.size() + n - 2)) return drawError(err, index, "out of memory");
  if (!arr.enterArray()) return drawError(err, index, "expected color and x, y pairs");
  std::size_t i = 0;
  while (arr.nextElement()) {
    if (i == 1) {
      if (arr.isNull()) {
        cmd.inheritColor = true;
      } else if (!color::readColor(arr, cmd.color)) {
        return drawError(err, index, "invalid color");
      }
    } else if (i >= 2) {
      int v = 0;
      if (!readIntAt(arr, v))
        return drawError(err, index, "expected numbers");
      p.data.push_back(static_cast<uint32_t>(v));
    }
    ++i;
    if (!arr.skipValue()) break;
  }
  return true;
}

bool readDrawCommand(api::JsonReader r, std::size_t index, render::DrawProgram& p,
                     DispatchDetail* err) {
  if (!r.isArray()) return drawError(err, index, "expected [name, ...]");

  api::JsonReader slots[kMaxDrawSlots];
  std::size_t count = 0;
  {
    api::JsonReader arr = r;
    if (!arr.enterArray())
      return drawError(err, index, "expected [name, ...]");
    while (arr.nextElement()) {
      if (count < kMaxDrawSlots) slots[count] = arr;
      ++count;
      if (!arr.skipValue()) break;
    }
  }
  if (count == 0 || !slots[0].isString())
    return drawError(err, index, "expected [name, ...]");
  char buffer[16];
  std::size_t length = 0;
  const std::string_view name =
      slots[0].copyString(buffer, sizeof(buffer), length) ? std::string_view(buffer, length) : "";

  render::DrawCommand cmd;
  cmd.first = static_cast<uint32_t>(p.data.size());
  if (name == "pixels") {
    cmd.kind = DrawKind::Pixels;
    if (!readPixels(r, index, cmd, p, err)) return false;
    cmd.count = static_cast<uint32_t>(p.data.size()) - cmd.first;
    return p.commands.push_back(cmd) || drawError(err, index, "out of memory");
  }

  const DrawSpec* spec = nullptr;
  for (const DrawSpec& d : kDrawSpecs)
    if (name == d.name) { spec = &d; break; }
  if (spec == nullptr)
    return drawError(err, index, "unknown command");

  // The trailing color is optional on every shape that takes one; leaving it out inherits the
  // app text color.
  const std::size_t bare = static_cast<std::size_t>(spec->argc) + 1;
  const std::size_t withColor = bare + (spec->takesColor ? 1 : 0);
  if (count != bare && count != withColor)
    return drawError(err, index, "wrong number of values");

  cmd.kind = spec->kind;
  const bool hasColor = spec->takesColor && count == withColor;
  cmd.inheritColor = spec->takesColor && !hasColor;

  // Coordinates map straight onto x, y, a, b: x2/y2 of a line, width/height, or the radius.
  int n[4] = {0, 0, 0, 0};
  for (int i = 0; i < spec->numeric; ++i)
    if (!readIntAt(slots[1 + i], n[i]))
      return drawError(err, index, "expected numbers");
  cmd.x = n[0]; cmd.y = n[1]; cmd.a = n[2]; cmd.b = n[3];

  if (spec->kind == DrawKind::Text) {
    if (!slots[3].isString()) return drawError(err, index, "expected text");
    std::size_t size = 0;
    slots[3].copyString(nullptr, std::numeric_limits<std::size_t>::max(), size);
    cmd.first = static_cast<uint32_t>(p.text.size());
    if (!p.text.resize(cmd.first + size)) return drawError(err, index, "out of memory");
    slots[3].copyString(p.text.data() + cmd.first, size, size);
    cmd.count = static_cast<uint32_t>(size);
  } else if (spec->kind == DrawKind::Bitmap) {
    if (!readBitmapData(slots[5], index, p, err)) return false;
    cmd.count = static_cast<uint32_t>(p.data.size()) - cmd.first;
  }

  if (hasColor && !color::readColor(slots[count - 1], cmd.color))
    return drawError(err, index, "invalid color");
  return p.commands.push_back(cmd) || drawError(err, index, "out of memory");
}

}

bool readDrawArray(api::JsonReader r, render::DrawProgram& out, DispatchDetail* err) {
  if (!r.isArray()) {
    if (err) { err->field = "draw"; err->message = "expected an array"; }
    return false;
  }
  api::JsonReader counter = r;
  std::size_t n = 0;
  if (counter.enterArray()) {
    while (counter.nextElement()) {
      ++n;
      if (!counter.skipValue()) break;
    }
  }
  if (!out.commands.reserve(out.commands.size() + n)) return drawError(err, 0, "out of memory");
  if (!r.enterArray()) return false;
  std::size_t index = 0;
  while (r.nextElement()) {
    if (!readDrawCommand(r, index, out, err)) return false;
    ++index;
    if (!r.skipValue()) break;
  }
  return true;
}

void takeBool(api::JsonReader r, bool& dst) {
  bool b = false;
  if (r.isBool() && r.asBool(b)) dst = b;
}

template <typename T>
void takeNum(api::JsonReader r, T& dst) {
  long long v = 0;
  if (r.isNumber() && r.asLong(v)) dst = static_cast<T>(v);
}

// NotMine tells readAppSpec to try the next group of keys; Failed means err has been filled in.
enum class Take : uint8_t { NotMine, Ok, Failed };

// The fragments are joined into s.text; each becomes one coloured run over it.
bool readTextFragments(api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  api::JsonReader counter = r;
  std::size_t n = 0;
  if (counter.enterArray()) {
    while (counter.nextElement()) {
      ++n;
      if (!counter.skipValue()) break;
    }
  }
  s.text.clear();
  s.fragments.clear();
  s.fragments.reserve(n);

  api::JsonReader frags = r;
  std::size_t fi = 0;
  if (!frags.enterArray()) return true;
  while (frags.nextElement()) {
    text::TextRun run{0, 0xFFFFFFu};
    const std::size_t start = s.text.size();
    bool hasColor = false;
    api::JsonReader fragColor;
    api::JsonReader frag = frags;
    if (frag.enterObject()) {
      while (frag.nextMember()) {
        if (frag.keyEquals("text")) {
          s.text.resize(start);
          frag.appendString(s.text);
        } else if (frag.keyEquals("color")) {
          hasColor = true;
          fragColor = frag;
        }
        if (!frag.skipValue()) break;
      }
    }
    if (hasColor) {
      const std::string field = "text[" + std::to_string(fi) + "].color";
      if (!readColorAt(fragColor, field.c_str(), run.color, err)) return false;
    }
    run.bytes = static_cast<uint32_t>(s.text.size() - start);
    s.fragments.push_back(run);
    ++fi;
    if (!frags.skipValue()) break;
  }
  return true;
}

// "text" is either a plain string or an array of {text, color} fragments for per-run coloring.
Take takeTextMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  if (k == "text") {
    if (r.isArray()) return readTextFragments(r, s, err) ? Take::Ok : Take::Failed;
    if (r.isString()) {
      s.text = readText(r);
      s.fragments.clear();
    }
    return Take::Ok;
  }
  if (k == "textCase")
    return readEnumAt(r, "textCase", kTextCaseNames, s.textCase, err) ? Take::Ok : Take::Failed;
  if (k == "font") {
    s.font.clear();
    if (r.isString() && r.appendString(s.font) && !s.font.empty()) return Take::Ok;
    if (err) *err = {"font", "expected a name"};
    return Take::Failed;
  }
  if (k == "textColor") {
    bool usesPalette = false;
    if (!readPaintAt(r, "textColor", s.textColor, usesPalette, err)) return Take::Failed;
    s.hasTextColor = !usesPalette;
    if (usesPalette) s.extrasMut().textUsesPalette = true;
    return Take::Ok;
  }
  if (k == "textInFront") { takeBool(r, s.textInFront); return Take::Ok; }
  if (k == "textAlign")
    return readEnumAt(r, "textAlign", kAlignNames, s.textAlign, err) ? Take::Ok : Take::Failed;
  if (k == "textCenter") {
    bool center = false;
    if (r.isBool() && r.asBool(center)) s.textAlign = center ? Align::Center : Align::Start;
    return Take::Ok;
  }
  if (k == "textBlinkMs") { takeNum(r, s.textBlinkMs); return Take::Ok; }
  if (k == "textFadeMs") { takeNum(r, s.textFadeMs); return Take::Ok; }
  if (k == "textOffsetX") { takeNum(r, s.textOffsetX); return Take::Ok; }
  return Take::NotMine;
}

Take takePaletteMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  if (k == "palette")
    return readPaletteAt(r, s.extrasMut().palette, err) ? Take::Ok : Take::Failed;
  if (k == "paletteBlend") {
    bool b = true;
    if (r.isBool() && r.asBool(b)) s.extrasMut().palette.blend = b;
    return Take::Ok;
  }
  if (k == "paletteSpan") {
    long long v = 0;
    if (r.isNumber() && r.asLong(v)) {
      if (v < 0) v = 0;
      if (v > 0xFFFF) v = 0xFFFF;
      s.extrasMut().palette.spanPx = static_cast<uint16_t>(v);
    }
    return Take::Ok;
  }
  if (k == "paletteSpeed") {
    double d = 0.0;
    if (r.isNumber() && r.asDouble(d)) {
      float sp = static_cast<float>(d);
      if (sp < 0.0f) sp = 0.0f;
      else if (sp > kSpeedMax) sp = kSpeedMax;
      s.extrasMut().palette.speed = sp;
    }
    return Take::Ok;
  }
  return Take::NotMine;
}

bool readPlacedIcons(api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  auto fail = [&](const std::string& field, const std::string& message) {
    if (err) *err = {field, message};
    return false;
  };
  if (!r.isArray()) return fail("icons", "expected an array");
  api::JsonReader count = r;
  count.enterArray();
  std::size_t size = 0;
  while (count.nextElement()) {
    if (++size > kMaxPlacedIcons)
      return fail("icons", "at most " + std::to_string(kMaxPlacedIcons) + " icons");
    if (!count.skipValue())
      return fail("icons[" + std::to_string(size - 1) + "]", "invalid icon object");
  }

  std::vector<PlacedIconSpec> icons;
  icons.reserve(size);
  r.enterArray();
  while (r.nextElement()) {
    const std::string field = "icons[" + std::to_string(icons.size()) + "]";
    api::JsonReader item = r;
    if (!item.enterObject()) return fail(field, "expected an object");
    PlacedIconSpec icon;
    while (item.nextMember()) {
      const std::string key(item.key());
      const std::string member = field + "." + key;
      if (key == "icon") {
        if (!item.isString()) return fail(member, "expected an icon");
        icon.icon.clear();
        if (!item.appendString(icon.icon) || icon.icon.empty())
          return fail(member, "expected an icon");
        if (!icons::valid(icon.icon)) return fail(member, icons::kInvalidMessage);
      } else if (key == "x" || key == "y") {
        long long value = 0;
        if (!item.isInteger() || !item.asLong(value) || value < -65535 || value > 65535)
          return fail(member, "must be -65535..65535");
        (key == "x" ? icon.x : icon.y) = static_cast<int>(value);
      } else {
        return fail(member, "unknown field");
      }
      if (!item.skipValue()) return fail(member, "invalid value");
    }
    if (icon.icon.empty()) return fail(field + ".icon", "expected an icon");
    icons.push_back(std::move(icon));
    if (!r.skipValue()) return fail(field, "invalid icon object");
  }
  if (!icons.empty() || !s.extras().icons.empty()) s.extrasMut().icons = std::move(icons);
  return true;
}

Take takeIconMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  if (k == "icons") return readPlacedIcons(r, s, err) ? Take::Ok : Take::Failed;
  if (k == "icon") {
    if (!r.isString()) return Take::Ok;
    std::string icon;
    r.appendString(icon);
    if (!icon.empty() && !icons::valid(icon)) {
      if (err) *err = {"icon", icons::kInvalidMessage};
      return Take::Failed;
    }
    s.icon = std::move(icon);
    return Take::Ok;
  }
  if (k == "iconMode")
    return readEnumAt(r, "iconMode", kIconModeNames, s.iconMode, err) ? Take::Ok : Take::Failed;
  if (k == "iconOffsetX") { takeNum(r, s.iconOffsetX); return Take::Ok; }
  if (k == "iconGap") {
    long long v = 0;
    if (!r.isInteger() || !r.asLong(v) || v < 0 || v > kMatrixWidthMax) {
      if (err)
        *err = {"iconGap", "must be 0.." + std::to_string(kMatrixWidthMax)};
      return Take::Failed;
    }
    s.iconGap = static_cast<int>(v);
    return Take::Ok;
  }
  if (k == "backgroundColor") {
    if (!readColorAt(r, "backgroundColor", s.backgroundColor, err)) return Take::Failed;
    s.hasBackgroundColor = true;
    return Take::Ok;
  }
  return Take::NotMine;
}

Take takeTimingMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  // Zeroed first, so a present but non-numeric value clears the field instead of keeping the
  // default.
  if (k == "durationMs") {
    s.durationMs = 0;
    takeNum(r, s.durationMs);
    return Take::Ok;
  }
  if (k == "repeat") {
    s.repeat = 0;
    takeNum(r, s.repeat);
    return Take::Ok;
  }
  if (k == "lifetimeMs") {
    s.lifetimeMs = 0;
    takeNum(r, s.lifetimeMs);
    return Take::Ok;
  }
  if (k == "lifetimeExpiry")
    return readEnumAt(r, "lifetimeExpiry", kLifetimeExpiryNames, s.lifetimeExpiry, err)
               ? Take::Ok
               : Take::Failed;
  if (k == "scroll") {
    scroll::Error se;
    if (!scroll::read(r, s.scroll, se)) {
      if (err) {
        err->field = se.field;
        err->message = se.message;
      }
      return Take::Failed;
    }
    return Take::Ok;
  }
  return Take::NotMine;
}

Take takeChartMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  if (k == "barChart") {
    readIntArrayCur(r, s.extrasMut().barChart, 16);
    return Take::Ok;
  }
  if (k == "lineChart") {
    readIntArrayCur(r, s.extrasMut().lineChart, 16);
    return Take::Ok;
  }
  if (k == "chartAutoscale") {
    bool b = true;
    bool v = false;
    if (r.isBool() && r.asBool(v)) b = v;
    s.extrasMut().chartAutoscale = b;
    return Take::Ok;
  }
  if (k == "chartColor") {
    AppSpecExtras& x = s.extrasMut();
    if (!readPaintAt(r, "chartColor", x.chartColor, x.chartUsesPalette, err)) return Take::Failed;
    x.hasChartColor = !x.chartUsesPalette;
    return Take::Ok;
  }
  if (k == "progress") {
    int p = -1;
    long long v = 0;
    if (r.isNumber() && r.asLong(v)) p = static_cast<int>(v);
    s.extrasMut().progress = p;
    return Take::Ok;
  }
  if (k == "progressColor") {
    AppSpecExtras& x = s.extrasMut();
    if (!readPaintAt(r, "progressColor", x.progressColor, x.progressUsesPalette, err))
      return Take::Failed;
    return Take::Ok;
  }
  if (k == "progressTrackColor")
    return readColorAt(r, "progressTrackColor", s.extrasMut().progressTrackColor, err)
               ? Take::Ok
               : Take::Failed;
  return Take::NotMine;
}

Take takeEffectMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  if (k == "effect") {
    if (r.isString()) r.appendString(s.effect);
    return Take::Ok;
  }
  if (k == "effectSpeed") {
    double d = 0.0;
    if (r.isNumber() && r.asDouble(d)) {
      float sp = static_cast<float>(d);
      if (sp < kSpeedMin) sp = kSpeedMin;
      else if (sp > kSpeedMax) sp = kSpeedMax;
      AppSpecExtras& x = s.extrasMut();
      x.effectSpeed = sp;
      x.hasEffectSpeed = true;
    }
    return Take::Ok;
  }
  if (k == "overlay") {
    if (r.isString()) {
      r.appendString(s.overlay);
      for (char& ch : s.overlay)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return Take::Ok;
  }
  if (k == "draw")
    return readDrawArray(r, s.extrasMut().draw, err) ? Take::Ok : Take::Failed;
  return Take::NotMine;
}

Take takeNotificationMember(const std::string& k, api::JsonReader r, AppSpec& s,
                            DispatchDetail* err) {
  if (k == "name") {
    if (r.isString()) r.appendString(s.name);
    return Take::Ok;
  }
  if (k == "hold") { takeBool(r, s.hold); return Take::Ok; }
  if (k == "stack") { takeBool(r, s.stack); return Take::Ok; }
  if (k == "wakeup") { takeBool(r, s.wakeup); return Take::Ok; }
  // Checked when the notification arrives and kept as sent. An empty name or null means no sound,
  // as for icon and effect.
  if (k == "sound") {
    if (r.isNull() || r.valueText() == "\"\"") {
      s.sound.clear();
      return Take::Ok;
    }
    const std::string_view text = r.valueText();
    sound::Choices choices;
    DispatchDetail detail;
    if (!sound::parse(text, sound::Origin::Notification, choices, detail)) {
      if (err) *err = detail;
      return Take::Failed;
    }
    s.sound.assign(text.data(), text.size());
    return Take::Ok;
  }
  return Take::NotMine;
}

void setKeyHandlers(const KeyHandler* handlers) { keyHandlers = handlers; }

bool readAppSpec(api::JsonReader root, bool isNotification, AppSpec& s, DispatchDetail* err) {
  s.isNotification = isNotification;
  if (!root.isObject()) return true;

  DispatchDetail detail;
  if (keyHandlers) {
    for (auto* handler = keyHandlers; handler->key; ++handler) {
      if (api::present(api::memberValue(root, handler->key)) && handler->validate &&
          !handler->validate(root, isNotification, detail)) {
        if (err) *err = std::move(detail);
        return false;
      }
    }
  }
  // Check every key before applying anything, so a payload with one bad key leaves the spec
  // untouched.
  {
    api::JsonReader keys = root;
    if (!keys.enterObject()) return true;
    while (keys.nextMember()) {
      const std::string k(keys.key());
      if (!keyAllowed(k.c_str(), isNotification)) {
        if (err) {
          err->field = k;
          err->message = "unknown field";
        }
        return false;
      }
      if (!keys.skipValue()) return false;
    }
  }

  api::JsonReader r = root;
  if (!r.enterObject()) return true;
  while (r.nextMember()) {
    const std::string k(r.key());

    if (const auto* handler = keyHandler(k)) {
      if (!handler->read(handler->context, r, s, detail)) {
        if (err) *err = std::move(detail);
        return false;
      }
      if (!r.skipValue()) return false;
      continue;
    }

    Take t = takeTextMember(k, r, s, err);
    if (t == Take::NotMine) t = takePaletteMember(k, r, s, err);
    if (t == Take::NotMine) t = takeIconMember(k, r, s, err);
    if (t == Take::NotMine) t = takeTimingMember(k, r, s, err);
    if (t == Take::NotMine) t = takeChartMember(k, r, s, err);
    if (t == Take::NotMine) t = takeEffectMember(k, r, s, err);
    if (t == Take::NotMine && isNotification) t = takeNotificationMember(k, r, s, err);
    if (t == Take::Failed) return false;

    if (!r.skipValue()) return false;
  }
  return true;
}

bool parse(const std::string& json, bool isNotification, AppSpec& out, int* arrayElements,
           JsonParse* why, DispatchDetail* err) {
  if (arrayElements) *arrayElements = 0;
  if (why) *why = JsonParse::Ok;

  if (!api::isWellFormed(json)) {
    if (why) *why = JsonParse::Malformed;
    return false;
  }

  api::JsonReader r{std::string_view(json)};
  if (r.isObject()) return readAppSpec(r, isNotification, out, err);
  // A top-level array is accepted for compatibility with the original AWTRIX API, but only its
  // first object is used. The element count goes back to the caller so it can say so.
  if (r.isArray()) {
    api::JsonReader arr = r;
    int n = 0;
    bool firstIsObject = false;
    api::JsonReader first;
    if (arr.enterArray()) {
      while (arr.nextElement()) {
        if (n == 0) {
          first = arr;
          firstIsObject = arr.isObject();
        }
        ++n;
        if (!arr.skipValue()) break;
      }
    }
    if (arrayElements) *arrayElements = n;
    if (n > 0 && firstIsObject) return readAppSpec(first, isNotification, out, err);
  }
  return true;
}

}
}
