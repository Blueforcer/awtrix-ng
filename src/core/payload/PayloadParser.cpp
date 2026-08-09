#include "core/payload/PayloadParser.h"

#include <cctype>
#include <type_traits>
#include <cstring>

#include "core/JsonColor.h"
#include "core/StrCase.h"
#include "core/payload/Base64.h"
#include "core/payload/EffectSettingsJson.h"
#include "core/payload/PaletteJson.h"
#include "core/sound/Rtttl.h"

namespace awtrix {
namespace payload {

namespace {

const char* const kTextCaseNames[] = {"inherit", "upper", "asTyped"};
const char* const kFontNames[] = {"small", "large"};
const char* const kIconModeNames[] = {"fixed", "pushOnce", "push"};
const char* const kLifetimeExpiryNames[] = {"remove", "mark"};

// The full set of accepted keys. Unknown keys are rejected rather than ignored, and the
// notification-only keys below are refused on a plain app.
const char* const kAppKeys[] = {
    "text", "textCase", "font", "textInFront", "textCenter", "textColor",
    "textBlinkMs", "textFadeMs", "textOffsetX",
    "backgroundColor", "icon", "iconMode", "iconOffsetX",
    "durationMs", "scroll", "repeat", "lifetimeMs", "lifetimeExpiry",
    "palette", "paletteBlend", "paletteSpan", "paletteSpeed",
    "barChart", "lineChart", "chartAutoscale", "chartColor",
    "progress", "progressColor", "progressTrackColor",
    "effect", "effectSpeed", "overlay", "draw",
};

const char* const kNotificationKeys[] = {
    "name", "hold", "stack", "wakeup", "sound", "soundRtttl", "soundLoop",
};

bool keyAllowed(const char* k, bool isNotification) {
  for (const char* a : kAppKeys)
    if (std::strcmp(k, a) == 0) return true;
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

bool drawError(DispatchDetail* err, std::size_t index, const std::string& why) {
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
    err->message = std::string("\"") + field + "\" is not a valid color";
  }
  return false;
}

// The string "palette" means "take the color from the app palette" instead of naming a color.
bool readPaintAt(api::JsonReader r, const char* field, uint32_t& out, bool& usesPalette,
                 DispatchDetail* err) {
  if (r.isString()) {
    std::string s;
    api::JsonReader v = r;
    if (v.appendString(s) && strcase::equalsIgnoreCase(s, "palette")) {
      usesPalette = true;
      return true;
    }
  }
  usesPalette = false;
  return readColorAt(r, field, out, err);
}

bool readPaletteAt(api::JsonReader r, render::ColorRamp& out, DispatchDetail* err) {
  if (readPalette(r, out)) return true;
  if (err) {
    err->field = "palette";
    err->message = "\"palette\" is not a known palette name or a list of colors";
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
    std::string msg = std::string("\"") + field + "\" must be one of";
    for (std::size_t i = 0; i < N; ++i) msg += std::string(i ? ", " : " ") + "\"" + names[i] + "\"";
    err->field = field;
    err->message = msg;
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
bool readBitmapData(api::JsonReader r, std::size_t index, DrawOp& op, DispatchDetail* err) {
  if (r.isString()) {
    std::string b64;
    std::vector<uint8_t> bytes;
    if (!r.appendString(b64) || !base64::decode(b64.c_str(), b64.size(), bytes))
      return drawError(err, index, "\"bitmap\" data is not valid base64");
    op.bitmap.reserve(bytes.size() / 3);
    for (std::size_t i = 0; i + 2 < bytes.size(); i += 3)
      op.bitmap.push_back((static_cast<uint32_t>(bytes[i]) << 16) |
                          (static_cast<uint32_t>(bytes[i + 1]) << 8) |
                          static_cast<uint32_t>(bytes[i + 2]));
    return true;
  }
  if (r.isArray()) {
    if (!r.enterArray()) return drawError(err, index, "\"bitmap\" data is not an array");
    while (r.nextElement()) {
      uint32_t c = 0u;
      if (!color::readColor(r, c))
        return drawError(err, index, "\"bitmap\" contains an invalid color");
      op.bitmap.push_back(c);
      if (!r.skipValue()) break;
    }
    return true;
  }
  return drawError(err, index, "\"bitmap\" data must be a base64 string or an array of colors");
}

// Enough slots for the longest command: bitmap takes a name, x, y, w, h and the pixel data.
constexpr int kMaxDrawSlots = 7;

// Shape is ["pixels", color, x, y, x, y, ...]. A null color means inherit the app text color.
bool readPixels(api::JsonReader arr, std::size_t index, DrawOp& op, DispatchDetail* err) {
  api::JsonReader counter = arr;
  std::size_t n = 0;
  if (counter.enterArray()) {
    while (counter.nextElement()) {
      ++n;
      if (!counter.skipValue()) break;
    }
  }
  if (n < 4) return drawError(err, index, "\"pixels\" needs a color and at least one x, y pair");
  if ((n - 2) % 2 != 0)
    return drawError(err, index, "\"pixels\" coordinates must come in x, y pairs");
  op.points.reserve(n - 2);
  if (!arr.enterArray()) return drawError(err, index, "\"pixels\" must be an array");
  std::size_t i = 0;
  while (arr.nextElement()) {
    if (i == 1) {
      if (arr.isNull()) {
        op.inheritColor = true;
      } else if (!color::readColor(arr, op.color)) {
        return drawError(err, index, "\"pixels\" color is not a valid color");
      }
    } else if (i >= 2) {
      int v = 0;
      if (!readIntAt(arr, v))
        return drawError(err, index, "\"pixels\" coordinates must be numbers");
      op.points.push_back(v);
    }
    ++i;
    if (!arr.skipValue()) break;
  }
  return true;
}

bool readDrawCommand(api::JsonReader r, std::size_t index, DrawOp& op, DispatchDetail* err) {
  if (!r.isArray()) return drawError(err, index, "each draw command must be an array, name first");

  api::JsonReader slots[kMaxDrawSlots];
  std::size_t count = 0;
  {
    api::JsonReader arr = r;
    if (!arr.enterArray())
      return drawError(err, index, "each draw command must be an array, name first");
    while (arr.nextElement()) {
      if (count < kMaxDrawSlots) slots[count] = arr;
      ++count;
      if (!arr.skipValue()) break;
    }
  }
  if (count == 0 || !slots[0].isString())
    return drawError(err, index, "the first entry must be the command name");
  std::string name;
  if (!slots[0].appendString(name))
    return drawError(err, index, "the first entry must be the command name");

  if (name == "pixels") {
    op.kind = DrawKind::Pixels;
    return readPixels(r, index, op, err);
  }

  const DrawSpec* spec = nullptr;
  for (const DrawSpec& d : kDrawSpecs)
    if (name == d.name) { spec = &d; break; }
  if (spec == nullptr)
    return drawError(err, index, std::string("unknown draw command \"") + name + "\"");

  // The trailing color is optional on every shape that takes one; leaving it out inherits the
  // app text color.
  const std::size_t bare = static_cast<std::size_t>(spec->argc) + 1;
  const std::size_t withColor = bare + (spec->takesColor ? 1 : 0);
  if (count != bare && count != withColor)
    return drawError(err, index, std::string("\"") + name + "\" has the wrong number of arguments");

  op.kind = spec->kind;
  const bool hasColor = spec->takesColor && count == withColor;
  op.inheritColor = spec->takesColor && !hasColor;

  int n[4] = {0, 0, 0, 0};
  for (int i = 0; i < spec->numeric; ++i)
    if (!readIntAt(slots[1 + i], n[i]))
      return drawError(err, index, std::string("\"") + name + "\" needs numeric coordinates");

  switch (spec->kind) {
    case DrawKind::Pixel:
      op.x = n[0]; op.y = n[1];
      break;
    case DrawKind::Line:
      op.x = n[0]; op.y = n[1]; op.x2 = n[2]; op.y2 = n[3];
      break;
    case DrawKind::Rect:
    case DrawKind::FillRect:
      op.x = n[0]; op.y = n[1]; op.w = n[2]; op.h = n[3];
      break;
    case DrawKind::Circle:
    case DrawKind::FillCircle:
      op.x = n[0]; op.y = n[1]; op.r = n[2];
      break;
    case DrawKind::Text: {
      op.x = n[0]; op.y = n[1];
      if (!slots[3].isString()) return drawError(err, index, "\"text\" needs a string");
      op.text = readText(slots[3]);
      break;
    }
    case DrawKind::Bitmap:
      op.x = n[0]; op.y = n[1]; op.w = n[2]; op.h = n[3];
      return readBitmapData(slots[5], index, op, err);
    default:
      return drawError(err, index, "unhandled draw command");
  }

  if (hasColor && !color::readColor(slots[count - 1], op.color))
    return drawError(err, index, std::string("\"") + name + "\" has an invalid color");
  return true;
}

bool readDrawArray(api::JsonReader r, std::vector<DrawOp>& out, DispatchDetail* err) {
  if (!r.isArray()) {
    if (err) { err->field = "draw"; err->message = "\"draw\" must be an array of commands"; }
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
  out.reserve(n);
  if (!r.enterArray()) return false;
  std::size_t index = 0;
  while (r.nextElement()) {
    DrawOp op;
    if (!readDrawCommand(r, index, op, err)) return false;
    out.push_back(std::move(op));
    ++index;
    if (!r.skipValue()) break;
  }
  return true;
}

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

bool readTextFragments(api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  api::JsonReader counter = r;
  std::size_t n = 0;
  if (counter.enterArray()) {
    while (counter.nextElement()) {
      ++n;
      if (!counter.skipValue()) break;
    }
  }
  s.fragments.reserve(n);

  api::JsonReader frags = r;
  std::size_t fi = 0;
  if (!frags.enterArray()) return true;
  while (frags.nextElement()) {
    TextFragment f;
    bool hasColor = false;
    api::JsonReader fragColor;
    api::JsonReader frag = frags;
    if (frag.enterObject()) {
      while (frag.nextMember()) {
        const std::string fk(frag.key());
        if (fk == "text") {
          f.text = readText(frag);
        } else if (fk == "color") {
          hasColor = true;
          fragColor = frag;
        }
        if (!frag.skipValue()) break;
      }
    }
    if (hasColor) {
      const std::string field = "text[" + std::to_string(fi) + "].color";
      if (!readColorAt(fragColor, field.c_str(), f.color, err)) return false;
    }
    s.fragments.push_back(std::move(f));
    ++fi;
    if (!frags.skipValue()) break;
  }
  return true;
}

// "text" is either a plain string or an array of {text, color} fragments for per-run coloring.
Take takeTextMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  if (k == "text") {
    if (r.isArray()) return readTextFragments(r, s, err) ? Take::Ok : Take::Failed;
    if (r.isString()) s.text = readText(r);
    return Take::Ok;
  }
  if (k == "textCase")
    return readEnumAt(r, "textCase", kTextCaseNames, s.textCase, err) ? Take::Ok : Take::Failed;
  if (k == "font")
    return readEnumAt(r, "font", kFontNames, s.font, err) ? Take::Ok : Take::Failed;
  if (k == "textColor") {
    bool usesPalette = false;
    if (!readPaintAt(r, "textColor", s.textColor, usesPalette, err)) return Take::Failed;
    s.hasTextColor = !usesPalette;
    if (usesPalette) s.extrasMut().textUsesPalette = true;
    return Take::Ok;
  }
  if (k == "textInFront") { takeBool(r, s.textInFront); return Take::Ok; }
  if (k == "textCenter") { takeBool(r, s.textCenter); return Take::Ok; }
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

Take takeIconMember(const std::string& k, api::JsonReader r, AppSpec& s, DispatchDetail* err) {
  if (k == "icon") {
    if (r.isString()) r.appendString(s.icon);
    return Take::Ok;
  }
  if (k == "iconMode")
    return readEnumAt(r, "iconMode", kIconModeNames, s.iconMode, err) ? Take::Ok : Take::Failed;
  if (k == "iconOffsetX") { takeNum(r, s.iconOffsetX); return Take::Ok; }
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
  if (k == "soundLoop") { takeBool(r, s.loopSound); return Take::Ok; }
  // Checked here rather than at the player: an unparsable melody used to reach the backend and
  // simply go quiet, with nothing said to whoever sent the notification.
  if (k == "soundRtttl") {
    if (!r.isString()) return Take::Ok;
    std::string melody;
    r.appendString(melody);
    const rtttl::Parse parsed = rtttl::parse(melody);
    if (!parsed.ok) {
      if (err) {
        err->field = "soundRtttl";
        err->message = parsed.describe();
      }
      return Take::Failed;
    }
    s.extrasMut().rtttl = melody;
    return Take::Ok;
  }
  // Old AWTRIX clients send the melody as a number; either form ends up as the file name.
  if (k == "sound") {
    if (r.isString()) {
      s.sound.clear();
      r.appendString(s.sound);
    } else if (r.isNumber() && r.isInteger()) {
      long long v = 0;
      if (r.asLong(v)) s.sound = std::to_string(v);
    }
    return Take::Ok;
  }
  return Take::NotMine;
}

bool readAppSpec(api::JsonReader root, bool isNotification, AppSpec& s, DispatchDetail* err) {
  s.isNotification = isNotification;
  if (!root.isObject()) return true;

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
          err->message = std::string("unknown key \"") + k + "\"";
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
