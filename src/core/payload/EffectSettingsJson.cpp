#include "core/payload/EffectSettingsJson.h"

#include <string>

#include "core/api/JsonCoerce.h"
#include "core/payload/PaletteJson.h"

namespace awtrix {
namespace payload {

// Lenient on types: "speed" takes a number, a bool or a numeric string. Unknown keys are ignored.
bool readEffectSettings(api::JsonReader r, EffectSettings& out) {
  bool sawBlend = false;
  bool blend = true;
  if (!r.enterObject()) return true;
  while (r.nextMember()) {
    const std::string key(r.key());
    if (key == "speed") {
      double d = 0.0;
      if (r.isNumber()) {
        r.asDouble(d);
      } else if (r.isBool()) {
        bool b = false;
        r.asBool(b);
        d = b ? 1.0 : 0.0;
      } else if (r.isString()) {
        api::stringAsNumber(r, d);
      }
      float sp = static_cast<float>(d);
      if (sp < kSpeedMin) sp = kSpeedMin;
      else if (sp > kSpeedMax) sp = kSpeedMax;
      out.speed = sp;
      out.hasSpeed = true;
    } else if (key == "palette") {
      if (!readPalette(r, out.ramp)) return false;
    } else if (key == "blend") {
      sawBlend = true;
      if (r.isBool()) {
        r.asBool(blend);
      } else if (r.isNumber()) {
        double d = 0.0;
        blend = r.asDouble(d) && d != 0.0;
      } else {
        blend = false;
      }
    }
    if (!r.skipValue()) return true;
  }

  if (sawBlend) out.ramp.blend = blend;
  return true;
}

}
}
