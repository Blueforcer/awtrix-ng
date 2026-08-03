#include "core/render/Palette.h"

#include "core/StrCase.h"

namespace awtrix {
namespace render {

namespace {

constexpr uint32_t kBlue = 0x0000FFu;
constexpr uint32_t kDarkBlue = 0x00008Bu;
constexpr uint32_t kSkyBlue = 0x87CEEBu;
constexpr uint32_t kLightBlue = 0xADD8E6u;
constexpr uint32_t kWhite = 0xFFFFFFu;
constexpr uint32_t kBlack = 0x000000u;
constexpr uint32_t kMaroon = 0x800000u;
constexpr uint32_t kDarkRed = 0x8B0000u;
constexpr uint32_t kRed = 0xFF0000u;
constexpr uint32_t kOrange = 0xFFA500u;
constexpr uint32_t kMidnightBlue = 0x191970u;
constexpr uint32_t kNavy = 0x000080u;
constexpr uint32_t kMediumBlue = 0x0000CDu;
constexpr uint32_t kSeaGreen = 0x2E8B57u;
constexpr uint32_t kTeal = 0x008080u;
constexpr uint32_t kCadetBlue = 0x5F9EA0u;
constexpr uint32_t kDarkCyan = 0x008B8Bu;
constexpr uint32_t kCornflowerBlue = 0x6495EDu;
constexpr uint32_t kAquamarine = 0x7FFFD4u;
constexpr uint32_t kAqua = 0x00FFFFu;
constexpr uint32_t kLightSkyBlue = 0x87CEFAu;
constexpr uint32_t kDarkGreen = 0x006400u;
constexpr uint32_t kDarkOliveGreen = 0x556B2Fu;
constexpr uint32_t kGreen = 0x008000u;
constexpr uint32_t kForestGreen = 0x228B22u;
constexpr uint32_t kOliveDrab = 0x6B8E23u;
constexpr uint32_t kMediumAquamarine = 0x66CDAAu;
constexpr uint32_t kLimeGreen = 0x32CD32u;
constexpr uint32_t kYellowGreen = 0x9ACD32u;
constexpr uint32_t kLightGreen = 0x90EE90u;
constexpr uint32_t kLawnGreen = 0x7CFC00u;

constexpr Palette kCloud = {{kBlue, kDarkBlue, kDarkBlue, kDarkBlue, kDarkBlue, kDarkBlue,
                             kDarkBlue, kDarkBlue, kBlue, kDarkBlue, kSkyBlue, kSkyBlue,
                             kLightBlue, kWhite, kLightBlue, kSkyBlue}};
constexpr Palette kLava = {{kBlack, kMaroon, kBlack, kMaroon, kDarkRed, kDarkRed, kMaroon, kDarkRed,
                            kDarkRed, kDarkRed, kRed, kOrange, kWhite, kOrange, kRed, kDarkRed}};
constexpr Palette kOcean = {{kMidnightBlue, kDarkBlue, kMidnightBlue, kNavy, kDarkBlue, kMediumBlue,
                             kSeaGreen, kTeal, kCadetBlue, kBlue, kDarkCyan, kCornflowerBlue,
                             kAquamarine, kSeaGreen, kAqua, kLightSkyBlue}};
constexpr Palette kForest = {{kDarkGreen, kDarkGreen, kDarkOliveGreen, kDarkGreen, kGreen,
                              kForestGreen, kOliveDrab, kGreen, kSeaGreen, kMediumAquamarine,
                              kLimeGreen, kYellowGreen, kLightGreen, kLawnGreen, kMediumAquamarine,
                              kForestGreen}};
constexpr Palette kRainbow = {{0xFF0000, 0xD52A00, 0xAB5500, 0xAB7F00, 0xABAB00, 0x56D500, 0x00FF00,
                               0x00D52A, 0x00AB55, 0x0056AA, 0x0000FF, 0x2A00D5, 0x5500AB, 0x7F0081,
                               0xAB0055, 0xD5002B}};
constexpr Palette kStripe = {{0xFF0000, 0x000000, 0xAB5500, 0x000000, 0xABAB00, 0x000000, 0x00FF00,
                              0x000000, 0x00AB55, 0x000000, 0x0000FF, 0x000000, 0x5500AB, 0x000000,
                              0xAB0055, 0x000000}};
constexpr Palette kParty = {{0x5500AB, 0x84007C, 0xB5004B, 0xE5001B, 0xE81700, 0xB84700, 0xAB7700,
                             0xABAB00, 0xAB5500, 0xDD2200, 0xF2000E, 0xC2003E, 0x8F0071, 0x5F00A1,
                             0x2F00D0, 0x0007F9}};
constexpr Palette kHeat = {{0x000000, 0x330000, 0x660000, 0x990000, 0xCC0000, 0xFF0000, 0xFF3300,
                            0xFF6600, 0xFF9900, 0xFFCC00, 0xFFFF00, 0xFFFF33, 0xFFFF66, 0xFFFF99,
                            0xFFFFCC, 0xFFFFFF}};

inline uint8_t scale8(uint8_t i, uint8_t scale) {
  return static_cast<uint8_t>((static_cast<uint16_t>(i) * (static_cast<uint16_t>(scale) + 1)) >> 8);
}
inline uint8_t red(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
inline uint8_t green(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
inline uint8_t blue(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }

}

uint32_t colorFromPalette(const Palette& pal, uint8_t index, bool blend) {
  const uint8_t hi4 = static_cast<uint8_t>(index >> 4);
  const uint8_t lo4 = static_cast<uint8_t>(index & 0x0F);
  const uint32_t entry = pal.entries[hi4];

  uint8_t r = red(entry), g = green(entry), b = blue(entry);
  if (lo4 && blend) {
    const uint32_t next = pal.entries[hi4 == 15 ? 0 : hi4 + 1];
    const uint8_t f2 = static_cast<uint8_t>(lo4 << 4);
    const uint8_t f1 = static_cast<uint8_t>(255 - f2);
    r = static_cast<uint8_t>(scale8(r, f1) + scale8(red(next), f2));
    g = static_cast<uint8_t>(scale8(g, f1) + scale8(green(next), f2));
    b = static_cast<uint8_t>(scale8(b, f1) + scale8(blue(next), f2));
  }
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

const Palette* findStockPalette(const std::string& name) {
  using strcase::equalsIgnoreCase;
  if (equalsIgnoreCase(name, "Cloud")) return &kCloud;
  if (equalsIgnoreCase(name, "Lava")) return &kLava;
  if (equalsIgnoreCase(name, "Ocean")) return &kOcean;
  if (equalsIgnoreCase(name, "Forest")) return &kForest;
  if (equalsIgnoreCase(name, "Stripe")) return &kStripe;
  if (equalsIgnoreCase(name, "Party")) return &kParty;
  if (equalsIgnoreCase(name, "Heat")) return &kHeat;
  if (equalsIgnoreCase(name, "Rainbow")) return &kRainbow;
  return nullptr;
}

const Palette& namedPalette(const std::string& name) {
  const Palette* p = findStockPalette(name);
  return p ? *p : kRainbow;
}

const Palette& defaultPalette() { return kOcean; }

}
}
