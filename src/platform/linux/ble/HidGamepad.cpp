#include "platform/linux/ble/HidGamepad.h"

#include <algorithm>
#include <map>
#include <vector>

namespace awtrix::ble {
namespace {

constexpr uint32_t kX = 0x10030, kY = 0x10031, kZ = 0x10032, kRx = 0x10033, kRy = 0x10034, kRz = 0x10035;
constexpr uint32_t kHat = 0x10039, kAccelerator = 0x200C4, kBrake = 0x200C5;
constexpr uint32_t kJoystick = 0x10004, kGamepad = 0x10005;
constexpr uint32_t kButtonPage = 9;
constexpr uint32_t kApplication = 1;

struct Report {
  int bits = 0;
  HidField hat, x, y, z, rz, rx, ry, accelerator, brake, buttons;
};

int32_t signExtend(uint32_t v, std::size_t bytes) {
  if (bytes == 1) return static_cast<int8_t>(v);
  if (bytes == 2) return static_cast<int16_t>(v);
  return static_cast<int32_t>(v);
}

bool raw(const Bytes& report, const HidField& f, uint32_t& out) {
  if (!f.valid()) return false;
  const int bits = std::min(f.bits, 32);
  const std::size_t first = static_cast<std::size_t>(f.bit) / 8;
  const std::size_t last = static_cast<std::size_t>(f.bit + bits - 1) / 8;
  if (last >= report.size()) return false;
  uint64_t v = 0;
  for (std::size_t b = first; b <= last; ++b) v |= static_cast<uint64_t>(report[b]) << (8 * (b - first));
  v >>= f.bit % 8;
  out = static_cast<uint32_t>(bits == 32 ? v : v & ((uint64_t{1} << bits) - 1));
  return true;
}

// Any width and either signedness to one byte, the logical minimum at 0.
bool byteOf(const Bytes& report, const HidField& f, uint8_t& out) {
  uint32_t v = 0;
  if (!raw(report, f, v)) return false;
  const int bits = std::min(f.bits, 32);
  if (f.min < 0 && bits < 32) v ^= uint32_t{1} << (bits - 1);
  out = static_cast<uint8_t>(bits >= 8 ? v >> (bits - 8) : v << (8 - bits));
  return true;
}

}

bool parseHidReportMap(const Bytes& map, HidLayout& out) {
  uint32_t page = 0;
  int size = 0, count = 0, id = 0;
  int32_t logicalMin = 0;
  bool usageMin = false;
  std::vector<uint32_t> usages;
  // Per open collection: whether it lies inside a gamepad or joystick application collection.
  std::vector<bool> gamepad;
  std::map<int, Report> reports;
  std::size_t i = 0;
  while (i < map.size()) {
    const uint8_t prefix = map[i];
    if (prefix == 0xFE) {
      if (i + 1 >= map.size()) break;
      i += 3 + map[i + 1];
      continue;
    }
    std::size_t n = prefix & 3;
    if (n == 3) n = 4;
    if (i + 1 + n > map.size()) break;
    uint32_t d = 0;
    for (std::size_t k = 0; k < n; ++k) d |= static_cast<uint32_t>(map[i + 1 + k]) << (8 * k);
    i += 1 + n;
    switch (prefix & 0xFC) {
      case 0x04: page = d; break;
      case 0x14: logicalMin = signExtend(d, n); break;
      case 0x74: size = static_cast<int>(d); break;
      case 0x94: count = static_cast<int>(d); break;
      case 0x84: id = static_cast<int>(d); break;
      case 0x08: usages.push_back(n == 4 ? d : page << 16 | d); break;
      case 0x18: usageMin = true; break;
      case 0x80: {
        Report& r = reports[id];
        const bool counted = !gamepad.empty() && gamepad.back();
        if (counted && (d & 1) == 0 && size > 0 && count > 0) {
          if (page == kButtonPage && usageMin) {
            r.buttons = {r.bits, size * count, 0};
          } else {
            for (std::size_t k = 0; k < usages.size() && k < static_cast<std::size_t>(count); ++k) {
              const HidField f{r.bits + static_cast<int>(k) * size, size, logicalMin};
              switch (usages[k]) {
                case kHat: r.hat = f; break;
                case kX: r.x = f; break;
                case kY: r.y = f; break;
                case kZ: r.z = f; break;
                case kRz: r.rz = f; break;
                case kRx: r.rx = f; break;
                case kRy: r.ry = f; break;
                case kAccelerator: r.accelerator = f; break;
                case kBrake: r.brake = f; break;
                default: break;
              }
            }
          }
        }
        r.bits += size * count;
        usages.clear();
        usageMin = false;
        break;
      }
      case 0xA0: {
        const uint32_t usage = usages.empty() ? 0 : usages.back();
        const bool inside = !gamepad.empty() && gamepad.back();
        gamepad.push_back(inside || (d == kApplication && (usage == kGamepad || usage == kJoystick)));
        usages.clear();
        usageMin = false;
        break;
      }
      case 0xC0:
        if (!gamepad.empty()) gamepad.pop_back();
        usages.clear();
        usageMin = false;
        break;
      case 0x90:
      case 0xB0:
        usages.clear();
        usageMin = false;
        break;
      default: break;
    }
  }
  for (const auto& [reportId, r] : reports) {
    if (!r.buttons.valid()) continue;
    out = HidLayout{};
    out.length = static_cast<std::size_t>(r.bits + 7) / 8;
    out.reportId = reportId;
    out.buttons = r.buttons;
    out.hat = r.hat;
    out.lx = r.x;
    out.ly = r.y;
    out.rx = r.z.valid() ? r.z : r.rx;
    out.ry = r.rz.valid() ? r.rz : r.ry;
    out.lt = r.brake;
    out.rt = r.accelerator;
    return true;
  }
  return false;
}

bool readHidReport(const Bytes& report, const HidLayout& layout, HidControls& out) {
  if (report.size() != layout.length) return false;
  HidControls c;
  uint32_t v = 0;
  if (raw(report, layout.buttons, v)) c.buttons = v;
  if (raw(report, layout.hat, v)) {
    const int64_t hat = static_cast<int64_t>(v) - layout.hat.min;
    c.hat = hat >= 0 && hat <= 7 ? static_cast<int>(hat) : -1;
  }
  const HidField* axes[4] = {&layout.lx, &layout.ly, &layout.rx, &layout.ry};
  for (int k = 0; k < 4; ++k) byteOf(report, *axes[k], c.axes[k]);
  byteOf(report, layout.lt, c.triggers[0]);
  byteOf(report, layout.rt, c.triggers[1]);
  out = c;
  return true;
}

}
