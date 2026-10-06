#include "platform/linux/layout/LayoutJson.h"

#include <algorithm>
#include <cstdio>
#include "core/icons/IconSource.h"
#include "platform/linux/layout/LayoutError.h"
#include "core/JsonColor.h"
#include "core/payload/EffectSettingsJson.h"
#include "core/payload/PaletteJson.h"
#include "core/payload/PayloadParser.h"
#include "core/render/Color.h"

namespace awtrix::layout {
namespace {
enum PaintKey { kPalette, kPaletteBlend, kPaletteSpan, kPaletteSpeed, kPaintKeys };
enum RegionKey {
  kId, kBox, kText, kIcon, kChart, kProgress, kDraw, kFont, kAlign, kValign, kColor, kTextColor,
  kTrackColor, kScroll, kRepeat, kTextCase, kTextBlinkMs, kTextFadeMs, kRegionPalette,
  kRegionKeys = kRegionPalette + kPaintKeys
};
enum LayoutKey {
  kVersion, kBackgroundColor, kEffect, kOverlay, kEffectSpeed, kRegions, kLayoutPalette,
  kLayoutKeys = kLayoutPalette + kPaintKeys
};
enum ScrollKey { kScrollSpeed, kScrollGap, kScrollHoldMs, kScrollMode, kScrollDirection, kScrollEntry };
enum ChartKey { kChartValues, kChartType, kChartMin, kChartMax };

// Palette keys come last in both tables, in PaintKey order.
const char* const kRegionNames[kRegionKeys] = {
    "id", "box", "text", "icon", "chart", "progress", "draw", "font", "align", "valign", "color",
    "textColor", "trackColor", "scroll", "repeat", "textCase", "textBlinkMs", "textFadeMs",
    "palette", "paletteBlend", "paletteSpan", "paletteSpeed"};
const char* const kLayoutNames[kLayoutKeys] = {
    "version", "backgroundColor", "effect", "overlay", "effectSpeed", "regions",
    "palette", "paletteBlend", "paletteSpan", "paletteSpeed"};
const char* const kScrollNames[] = {"speed", "gap", "holdMs", "mode", "direction", "entry", "whenFits"};
const char* const kChartNames[] = {"values", "type", "min", "max"};
const char* const kChartTypes[] = {"line", "bar"};
const char* const kFragmentNames[] = {"text", "color"};

bool fail(DispatchDetail& e, const DetailPath& field, const char* message) {
  if (e.message == "out of memory") return false;
  return layoutFailure(e, field, message);
}
bool integer(api::JsonReader r, int& value, int minimum, int maximum) {
  long long n = 0;
  if (!r.isInteger() || !r.asLong(n) || n < minimum || n > maximum) return false;
  value = static_cast<int>(n); return true;
}
bool string(api::JsonReader r, CheckedString& value, std::size_t maximum, DispatchDetail& error) {
  std::size_t size = 0;
  value.clear();
  if (!r.copyString(nullptr, maximum, size)) return false;
  if (!value.resize(size)) return memoryFailure(error);
  return r.copyString(value.data(), size, size);
}
int keyOf(std::string_view key, const char* const* names, int count) {
  for (int i = 0; i < count; ++i) if (key == names[i]) return i;
  return -1;
}
// A string of at most 15 bytes, NUL-terminated.
bool word(api::JsonReader r, char (&out)[16]) {
  std::size_t size = 0;
  if (!r.isString() || !r.copyString(out, sizeof(out) - 1, size)) return false;
  out[size] = '\0';
  return true;
}
int choice(api::JsonReader r, const char* const* names, int count) {
  char value[16];
  return word(r, value) ? keyOf(value, names, count) : -1;
}
bool once(uint32_t& seen, int key) {
  const uint32_t bit = 1u << key;
  if (seen & bit) return false;
  seen |= bit;
  return true;
}
__attribute__((noinline)) bool nativeScroll(api::JsonReader r, ScrollSpec& out, DispatchDetail& e, const DetailPath& path) {
  if (r.isNull()) return true;
  char value[16];
  if (r.isString()) {
    if (!word(r, value) || !scroll::parseMode(value, out.mode))
      return fail(e, path, "unknown value");
    out.hasMode = true; return true;
  }
  if (!r.enterObject()) return fail(e, path, "expected mode or object");
  uint32_t seen = 0;
  while (r.nextMember()) {
    const std::string_view name(r.key());
    const auto field = path + "." + name;
    const int key = keyOf(name, kScrollNames, 7);
    if (key < 0) return fail(e, field, "unknown field");
    if (!once(seen, key)) return fail(e, field, "duplicate field");
    if (key <= kScrollHoldMs) {
      int count = 0;
      if (!integer(r, count, 0, key == kScrollGap ? 32767 : 1000000))
        return fail(e, field, "out of range");
      if (key == kScrollSpeed) { out.speed = count; out.hasSpeed = true; }
      else if (key == kScrollGap) { out.gap = count; out.hasGap = true; }
      else { out.holdMs = count; out.hasHoldMs = true; }
    } else {
      if (!word(r, value)) return fail(e, field, "unknown value");
      bool valid = false;
      if (key == kScrollMode) valid = out.hasMode = scroll::parseMode(value, out.mode);
      else if (key == kScrollDirection) valid = out.hasDirection = scroll::parseDirection(value, out.direction);
      else if (key == kScrollEntry) valid = out.hasEntry = scroll::parseEntry(value, out.entry);
      else valid = out.hasWhenFits = scroll::parseWhenFits(value, out.whenFits);
      if (!valid) return fail(e, field, "unknown value");
    }
    if (!r.skipValue()) return false;
  }
  return true;
}
bool nativeColor(api::JsonReader r, uint32_t& out) {
  // At most 7 characters; colour parsing uses inline storage.
  if (r.isString()) {
    char value[8]; std::size_t size = 0;
    return r.copyString(value, sizeof(value), size) &&
        (size == 3 || size == 4 || size == 6 || size == 7) &&
        color::tryFromHex(std::string(value, size), out);
  }
  if (r.isArray()) {
    auto values = r;
    values.enterArray();
    while (values.nextElement()) {
      if (values.isString()) {
        char tag[3]; std::size_t size = 0;
        if (!values.copyString(tag, sizeof(tag), size) || size != 3 ||
            std::string_view(tag, size) != "HSV") return false;
      }
      if (!values.skipValue()) return false;
    }
  }
  return color::readColor(r, out);
}
// The string "palette" takes the color from the region's or the layout's palette.
bool nativePaint(api::JsonReader r, uint32_t& out, bool& palette) {
  palette = color::isPaletteWord(r);
  return palette || nativeColor(r, out);
}
bool paint(int key, api::JsonReader r, Paint& out, bool& tuned, DispatchDetail& e,
           const DetailPath& field) {
  if (key == kPalette) {
    const bool read = r.isString() ? string(r, out.name, 32, e) && !out.name.empty()
                                   : payload::readPaletteStops(r, out.palette);
    if (!read) return fail(e, field, "unknown palette");
    out.set = true;
    return true;
  }
  tuned = true;
  if (key == kPaletteBlend)
    return (r.isBool() && r.asBool(out.blend)) || fail(e, field, "expected true or false");
  if (key == kPaletteSpan) {
    int span = 0;
    if (!integer(r, span, 0, 65535)) return fail(e, field, "expected 0..65535");
    out.spanPx = static_cast<uint16_t>(span);
    return true;
  }
  double speed = 0;
  if (!r.isNumber() || !r.asDouble(speed) || !(speed >= 0 && speed <= payload::kSpeedMax))
    return fail(e, field, "expected 0..10");
  out.speed = static_cast<float>(speed);
  return true;
}
__attribute__((noinline)) bool fragments(api::JsonReader r, Region& out, DispatchDetail& e, const DetailPath& path) {
  auto a = r;
  a.enterArray();
  std::size_t index = 0;
  while (a.nextElement()) {
    char at[16];
    std::snprintf(at, sizeof(at), "[%u]", static_cast<unsigned>(index++));
    const DetailPath item = path + at;
    auto o = a;
    if (!o.enterObject()) return fail(e, item, "expected an object");
    text::TextRun run{0, text::kFlatColor};
    uint32_t seen = 0;
    while (o.nextMember()) {
      const std::string_view name(o.key());
      const int key = keyOf(name, kFragmentNames, 2);
      if (key < 0) return fail(e, item + "." + name, "unknown field");
      if (!once(seen, key)) return fail(e, item + "." + name, "duplicate field");
      if (key == 0) {
        std::size_t size = 0;
        const std::size_t start = out.text.size();
        if (!o.copyString(nullptr, 8192 - std::min<std::size_t>(start, 8192), size))
          return fail(e, path, "at most 8192 bytes");
        if (!out.text.resize(start + size)) return memoryFailure(e);
        if (!o.copyString(out.text.data() + start, size, size)) return false;
        run.bytes = static_cast<uint32_t>(size);
      } else if (!nativeColor(o, run.color)) {
        return fail(e, item + ".color", "invalid color");
      }
      if (!o.skipValue()) return false;
    }
    if (!(seen & 1u)) return fail(e, item + ".text", "required");
    if (!out.fragments.push_back(run)) return memoryFailure(e);
    if (!a.skipValue()) return false;
  }
  if (out.fragments.empty()) return fail(e, path, "empty");
  return true;
}
__attribute__((noinline)) bool drawing(api::JsonReader r, Region& out, DispatchDetail& e, const DetailPath& path) {
  DispatchDetail why;
  if (!payload::readDrawArray(r, out.draw, &why)) {
    if (why.message == "out of memory") return memoryFailure(e);
    const std::string_view at(why.field);
    return fail(e, path + at.substr(std::min<std::size_t>(4, at.size())), why.message.c_str());
  }
  return !out.draw.commands.empty() || fail(e, path, "empty");
}
bool box(api::JsonReader r, Box& out) {
  if (!r.enterArray()) return false;
  int v[4] = {};
  int n = 0;
  while (r.nextElement()) {
    if (n >= 4 || !integer(r, v[n], n < 2 ? 0 : 1, 32767)) return false;
    ++n;
    if (!r.skipValue()) return false;
  }
  if (n != 4) return false;
  out = {v[0], v[1], v[2], v[3]}; return true;
}
__attribute__((noinline)) bool chart(api::JsonReader r, Region& out, DispatchDetail& e, const DetailPath& path) {
  if (!r.enterObject()) return fail(e, path, "expected an object");
  bool values = false, minimum = false, maximum = false;
  uint32_t seen = 0;
  while (r.nextMember()) {
    const std::string_view name(r.key());
    const DetailPath field = path + "." + name;
    const int key = keyOf(name, kChartNames, 4);
    if (key < 0) return fail(e, field, "unknown field");
    if (!once(seen, key)) return fail(e, field, "duplicate field");
    if (key == kChartValues) {
      auto a = r;
      if (!a.enterArray()) return fail(e, field, "expected an array");
      while (a.nextElement()) {
        int v = 0;
        if (out.values.size() >= 128 || !integer(a, v, -1000000000, 1000000000))
          return fail(e, field, "at most 128 integers");
        if (!out.values.push_back(v)) return memoryFailure(e);
        if (!a.skipValue()) return false;
      }
      values = !out.values.empty();
    } else if (key == kChartType) {
      const int type = choice(r, kChartTypes, 2);
      if (type < 0) return fail(e, field, "expected line or bar");
      out.bars = type == 1;
    } else {
      if (!integer(r, key == kChartMin ? out.minimum : out.maximum, -1000000000, 1000000000))
        return fail(e, field, "expected an integer");
      (key == kChartMin ? minimum : maximum) = true;
    }
    if (!r.skipValue()) return false;
  }
  if (!values) return fail(e, path + ".values", "required");
  if (minimum != maximum || (minimum && out.minimum >= out.maximum))
    return fail(e, path, "set min and max, min < max");
  out.autoscale = !minimum;
  return true;
}
__attribute__((noinline)) bool region(api::JsonReader r, Region& out, DispatchDetail& e, const DetailPath& path) {
  if (!r.enterObject()) return fail(e, path, "expected an object");
  int content = 0;
  bool hasBox = false, textOnly = false, track = false, tuned = false, font = false;
  uint32_t seen = 0;
  while (r.nextMember()) {
    const std::string_view name(r.key());
    const DetailPath field = path + "." + name;
    const int key = keyOf(name, kRegionNames, kRegionKeys);
    if (key < 0) return fail(e, field, "unknown field");
    // textColor is the pushed-app name for the same field.
    if (!once(seen, key == kTextColor ? kColor : key)) return fail(e, field, "duplicate field");
    switch (key) {
      case kId:
        if (!string(r, out.id, 64, e) || out.id.empty())
          return fail(e, field, "expected 1..64 bytes");
        break;
      case kBox:
        if (!box(r, out.box)) return fail(e, field, "expected [x, y, width, height]");
        hasBox = true;
        break;
      case kText:
        if (r.isArray()) {
          if (!fragments(r, out, e, field)) return false;
        } else if (!string(r, out.text, 8192, e)) {
          return fail(e, field, "at most 8192 bytes");
        }
        out.kind = Kind::Text; ++content;
        break;
      case kIcon:
        if (!string(r, out.asset, 8192, e) || !icons::valid(out.asset.view()))
          return fail(e, field, icons::kInvalidMessage);
        out.kind = Kind::Image; ++content;
        break;
      case kChart:
        if (!chart(r, out, e, field)) return false;
        out.kind = Kind::Chart; ++content;
        break;
      case kProgress:
        if (!integer(r, out.progress, 0, 100)) return fail(e, field, "expected 0..100");
        out.kind = Kind::Progress; ++content;
        break;
      case kDraw:
        if (!drawing(r, out, e, field)) return false;
        out.kind = Kind::Draw; ++content;
        break;
      case kFont:
        if (!string(r, out.font, 96, e) || out.font.empty()) return fail(e, field, "expected a name");
        font = true;
        break;
      case kAlign:
      case kValign: {
        const int align = choice(r, kAlignNames, 3);
        if (align < 0) return fail(e, field, "expected start, center or end");
        (key == kAlign ? out.align : out.valign) = static_cast<Align>(align);
        break;
      }
      case kColor:
      case kTextColor:
        if (!nativePaint(r, out.color, out.usesPalette)) return fail(e, field, "invalid color");
        out.hasColor = !out.usesPalette;
        break;
      case kTrackColor:
        if (!nativeColor(r, out.trackColor)) return fail(e, field, "invalid color");
        track = true;
        break;
      case kScroll:
        if (!nativeScroll(r, out.scroll, e, field)) return false;
        textOnly = true;
        break;
      case kRepeat:
        if (!integer(r, out.repeat, 0, 1000000)) return fail(e, field, "expected 0..1000000");
        textOnly = true;
        break;
      case kTextCase: {
        const int textCase = choice(r, kTextCaseNames, 3);
        if (textCase < 0) return fail(e, field, "expected inherit, upper or asTyped");
        out.textCase = static_cast<TextCase>(textCase);
        textOnly = true;
        break;
      }
      case kTextBlinkMs:
      case kTextFadeMs:
        if (!integer(r, key == kTextBlinkMs ? out.blinkMs : out.fadeMs, 0, 1000000))
          return fail(e, field, "expected 0..1000000");
        textOnly = true;
        break;
      default:
        if (!paint(key - kRegionPalette, r, out.palette, tuned, e, field)) return false;
    }
    if (!r.skipValue()) return false;
  }
  if (content != 1) return fail(e, path, "needs one of text, icon, chart, progress, draw");
  if (out.id.empty() || !hasBox) return fail(e, path, "needs id and box");
  if (textOnly && out.kind != Kind::Text)
    return fail(e, path, "text options without text");
  if (font && out.kind != Kind::Text && out.kind != Kind::Draw)
    return fail(e, path, "font only for text and draw");
  if (track && out.kind != Kind::Progress) return fail(e, path, "trackColor only for progress");
  if ((out.palette.set || tuned || out.usesPalette) &&
      (out.kind == Kind::Image || out.kind == Kind::Draw))
    return fail(e, path, "palette only for text, chart, progress");
  if (tuned && !out.palette.set)
    return fail(e, path, "needs palette");
  return true;
}
}

bool parse(api::JsonReader r, LayoutSpec& out, DispatchDetail& error) {
  error.clear();
  LayoutSpec next;
  if (!r.enterObject()) return fail(error, "layout", "expected an object");
  bool version = false, regions = false, tuned = false;
  uint32_t seen = 0;
  while (r.nextMember()) {
    const std::string_view name(r.key());
    const DetailPath field = DetailPath("layout.") + name;
    const int key = keyOf(name, kLayoutNames, kLayoutKeys);
    if (key < 0) return fail(error, field, "unknown field");
    if (!once(seen, key)) return fail(error, field, "duplicate field");
    switch (key) {
      case kVersion: {
        int v = 0;
        if (!integer(r, v, 1, 1)) return fail(error, field, "must be 1");
        version = true;
        break;
      }
      case kBackgroundColor:
        if (!nativeColor(r, next.background)) return fail(error, field, "invalid color");
        next.hasBackground = true;
        break;
      case kEffect:
      case kOverlay: {
        auto& effect = key == kEffect ? next.effect : next.overlay;
        if (!string(r, effect, 32, error) || effect.empty()) return fail(error, field, "expected a name");
        break;
      }
      case kEffectSpeed: {
        double speed = 0;
        if (!r.isNumber() || !r.asDouble(speed) ||
            !(speed >= payload::kSpeedMin && speed <= payload::kSpeedMax))
          return fail(error, field, "expected 0.1..10");
        next.effectSpeed = static_cast<float>(speed);
        next.hasEffectSpeed = true;
        break;
      }
      case kRegions: {
        regions = true;
        auto a = r;
        if (!a.enterArray()) return fail(error, field, "expected an array");
        while (a.nextElement()) {
          Region item;
          char index[24];
          std::snprintf(index, sizeof(index), "layout.regions[%u]", static_cast<unsigned>(next.regions.size()));
          const DetailPath path(index);
          if (next.regions.size() >= 16) return fail(error, path, "at most 16 regions");
          if (!region(a, item, error, path)) return false;
          if (!next.regions.push_back(std::move(item))) return memoryFailure(error);
          if (!a.skipValue()) return false;
        }
        break;
      }
      default:
        if (!paint(key - kLayoutPalette, r, next.palette, tuned, error, field)) return false;
    }
    if (!r.skipValue()) return false;
  }
  if (!version) return fail(error, "layout.version", "required");
  if (!regions) return fail(error, "layout.regions", "required");
  if (next.regions.empty()) return fail(error, "layout.regions", "empty");
  if (tuned && !next.palette.set)
    return fail(error, "layout", "needs palette");
  if (next.hasEffectSpeed && next.effect.empty())
    return fail(error, "layout.effectSpeed", "needs effect");
  if (next.hasBackground && !next.effect.empty())
    return fail(error, "layout.backgroundColor", "not with effect");
  if (!next.valid()) return memoryFailure(error);
  out = std::move(next);
  return true;
}
}
