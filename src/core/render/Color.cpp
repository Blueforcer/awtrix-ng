#include "core/render/Color.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace awtrix {
namespace color {

namespace {
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool parseHexDigits(const std::string& s, size_t pos, size_t len, uint32_t& out) {
  uint32_t v = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = s[pos + i];
    uint32_t d;
    if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
    else return false;
    v = (v << 4) | d;
  }
  out = v;
  return true;
}
}

bool tryFromHex(const std::string& s, uint32_t& out) {
  size_t pos = 0;
  if (pos < s.size() && s[pos] == '#') ++pos;
  const size_t len = s.size() - pos;
  if (len == 6) {
    uint32_t v;
    if (!parseHexDigits(s, pos, 6, v)) return false;
    out = v & 0xFFFFFFu;
    return true;
  }
  if (len == 3) {
    uint32_t r, g, b;
    if (!(parseHexDigits(s, pos + 0, 1, r) && parseHexDigits(s, pos + 1, 1, g) &&
          parseHexDigits(s, pos + 2, 1, b)))
      return false;
    out = pack(static_cast<uint8_t>(r * 17), static_cast<uint8_t>(g * 17),
               static_cast<uint8_t>(b * 17));
    return true;
  }
  return false;
}

uint32_t fromHex(const std::string& s, uint32_t fallback) {
  uint32_t v;
  return tryFromHex(s, v) ? v : fallback;
}

std::string toHex(uint32_t c) {
  static const char* kDigits = "0123456789ABCDEF";
  std::string s("#000000");
  for (int i = 0; i < 6; ++i) s[6 - i] = kDigits[(c >> (4 * i)) & 0xF];
  return s;
}

uint32_t lerp(uint32_t a, uint32_t b, float t) {
  auto mix = [&](int sh) {
    const int va = (a >> sh) & 0xFF, vb = (b >> sh) & 0xFF;
    const int v = va + static_cast<int>((vb - va) * t);
    return static_cast<uint32_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
  };
  return (mix(16) << 16) | (mix(8) << 8) | mix(0);
}

uint32_t desaturate(uint32_t c, int pct) {
  if (pct >= 100) return c;
  if (pct < 0) pct = 0;
  const int r = red(c), g = green(c), b = blue(c);
  const int gray = (77 * r + 150 * g + 29 * b) >> 8;
  const auto mix = [&](int v) {
    return static_cast<uint8_t>(gray + (v - gray) * pct / 100);
  };
  return pack(mix(r), mix(g), mix(b));
}

uint32_t fromRgb(int r, int g, int b) {
  return pack(static_cast<uint8_t>(clampi(r, 0, 255)), static_cast<uint8_t>(clampi(g, 0, 255)),
              static_cast<uint8_t>(clampi(b, 0, 255)));
}

uint32_t fromHsv(int h, int s, int v) {
  h %= 360;
  if (h < 0) h += 360;
  const float S = clampi(s, 0, 100) / 100.0f;
  const float V = clampi(v, 0, 100) / 100.0f;
  const float C = V * S;
  const float X = C * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  const float m = V - C;
  float r = 0, g = 0, b = 0;
  if (h < 60)       { r = C; g = X; b = 0; }
  else if (h < 120) { r = X; g = C; b = 0; }
  else if (h < 180) { r = 0; g = C; b = X; }
  else if (h < 240) { r = 0; g = X; b = C; }
  else if (h < 300) { r = X; g = 0; b = C; }
  else              { r = C; g = 0; b = X; }
  return fromRgb(static_cast<int>(std::lround((r + m) * 255.0f)),
                 static_cast<int>(std::lround((g + m) * 255.0f)),
                 static_cast<int>(std::lround((b + m) * 255.0f)));
}

// Tanner Helland's blackbody curve fit; t is the colour temperature in hundreds of kelvin.
uint32_t fromKelvin(int k) {
  float t = clampi(k, 1000, 40000) / 100.0f;
  float r, g, b;
  if (t <= 66.0f) {
    r = 255.0f;
  } else {
    r = 329.698727446f * std::pow(t - 60.0f, -0.1332047592f);
  }
  if (t <= 66.0f) {
    g = 99.4708025861f * std::log(t) - 161.1195681661f;
  } else {
    g = 288.1221695283f * std::pow(t - 60.0f, -0.0755148492f);
  }
  if (t >= 66.0f) {
    b = 255.0f;
  } else if (t <= 19.0f) {
    b = 0.0f;
  } else {
    b = 138.5177312231f * std::log(t - 10.0f) - 305.0447927307f;
  }
  return fromRgb(static_cast<int>(std::lround(r)), static_cast<int>(std::lround(g)),
                 static_cast<int>(std::lround(b)));
}

}
}
