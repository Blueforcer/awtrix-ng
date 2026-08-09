#pragma once

#include <string>
#include <vector>

#include "core/SocProfileJson.h"
#include "core/Transitions.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {
namespace api {

inline std::string capabilitiesJson(const std::vector<std::string>& effects,
                                    const std::vector<std::string>& paletteEffects,
                                    const std::vector<std::string>& overlays,
                                    const sound::Caps& audio) {
  auto list = [](const std::vector<std::string>& names) {
    std::string out = "[";
    bool first = true;
    for (const auto& n : names) {
      if (!first) out += ',';
      out += '"' + n + '"';
      first = false;
    }
    out += ']';
    return out;
  };
  auto flag = [](bool v) { return std::string(v ? "true" : "false"); };
  return "{\"effects\":" + list(effects) + ",\"paletteEffects\":" + list(paletteEffects) +
         ",\"transitions\":" + transitionsJson() + ",\"overlays\":" + list(overlays) +
         ",\"palettes\":[\"Cloud\",\"Lava\",\"Ocean\",\"Forest\",\"Stripe\","
         "\"Party\",\"Heat\",\"Rainbow\"]"
         ",\"audio\":{\"buzzer\":" +
         flag(audio.buzzer) + ",\"track\":" + flag(audio.track) + ",\"mp3\":" + flag(audio.mp3) +
         ",\"radio\":" + flag(audio.radio) + "}" +
         ",\"gpio\":" + pins::toJson(pins::activeProfile()) + "}";
}

}
}
