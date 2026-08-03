#include "core/JsonColor.h"

#include <cstring>
#include <string>

#include "core/render/Color.h"

namespace awtrix {
namespace color {

// Accepts "#RGB"/"#RRGGBB", [r,g,b], ["HSV",h,s,v] and a packed integer; always yields 0xRRGGBB.
// The leading "HSV" tag is what tells the two array forms apart.
bool readColor(api::JsonReader r, uint32_t& out) {
  if (r.isString()) {
    std::string s;
    if (!r.appendString(s)) return false;
    return tryFromHex(s, out);
  }
  if (r.isArray()) {
    bool isHsv = false;
    long long n[3] = {0, 0, 0};
    int components = 0;
    bool allInts = true;
    bool first = true;
    if (!r.enterArray()) return false;
    while (r.nextElement()) {
      if (first && r.isString()) {
        std::string tag;
        if (r.appendString(tag) && tag == "HSV") {
          isHsv = true;
          first = false;
          if (!r.skipValue()) return false;
          continue;
        }
      }
      first = false;
      if (components < 3) {
        long long v = 0;
        if (r.isNumber() && r.isInteger() && r.asLong(v)) n[components] = v;
        else allInts = false;
      }
      ++components;
      if (!r.skipValue()) return false;
    }
    if (components < 3 || !allInts) return false;
    out = isHsv ? fromHsv(static_cast<int>(n[0]), static_cast<int>(n[1]), static_cast<int>(n[2]))
                : fromRgb(static_cast<int>(n[0]), static_cast<int>(n[1]), static_cast<int>(n[2]));
    return true;
  }
  if (r.isNumber() && r.isInteger()) {
    long long v = 0;
    if (!r.asLong(v)) return false;
    out = v < 0 ? 0u : (static_cast<uint32_t>(v) & 0xFFFFFFu);
    return true;
  }
  return false;
}

}
}
