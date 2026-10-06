#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/DeviceCapabilities.h"
#include "core/PlatformDescriptor.h"
#include "core/SocProfileJson.h"
#include "core/Transitions.h"
#include "core/api/JsonWriter.h"
#include "core/render/FontCatalog.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {
namespace api {

using CapabilityMembersFn = std::function<void(JsonWriter&)>;

inline std::string capabilitiesJson(const std::vector<std::string>& effects,
                                    const std::vector<std::string>& paletteEffects,
                                    const std::vector<std::string>& overlays,
                                    const sound::Caps& audio,
                                    const PlatformDescriptor* platform = nullptr,
                                    const FontCatalog* fonts = nullptr,
                                    const CapabilityMembersFn& platformMembers = nullptr) {
  std::string out;
  JsonWriter writer(out);
  const auto list = [&](const char* key, const std::vector<std::string>& names) {
    writer.key(key).beginArray();
    for (const auto& name : names) writer.value(name);
    writer.endArray();
  };
  writer.beginObject();
  list("effects", effects);
  list("paletteEffects", paletteEffects);
  writer.key("transitions").raw(transitionsJson());
  list("overlays", overlays);
  writer.key("palettes").beginArray();
  for (const char* name : {"Cloud", "Lava", "Ocean", "Forest", "Stripe", "Party", "Heat", "Rainbow"})
    writer.value(name);
  writer.endArray();

  const DeviceCapabilities caps = DeviceCapabilities::from(audio, platform);
  constexpr std::size_t kAudioFlags = 7;
  constexpr std::size_t kAudioPrefix = sizeof("audio.") - 1;
  writer.key("audio").beginObject();
  for (std::size_t i = 0; i < kAudioFlags; ++i) {
    if (i + 1 == kAudioFlags) writer.member("url", audio.url);
    writer.member(DeviceCapabilities::kNames[i].data() + kAudioPrefix, caps.present[i]);
  }
  writer.member("clip", audio.clip).endObject();
  writer.member("microphone", caps.has("microphone")).member("scriptUpdates", true).key("gpio");
  if (platform && !platform->configurableGpio) writer.null();
  else writer.raw(pins::toJson(pins::activeProfile()));
  if (platform) {
    const auto& display = platform->display;
    const auto& limits = display.limits;
    const int requestedWidth = display.requestedWidth > 0 ? display.requestedWidth : display.width;
    writer.key("platform").beginObject().member("id", platform->id).endObject();
    writer.key("sensors").beginObject().member("light", caps.has("sensors.light")).endObject();
    writer.key("display").beginObject()
        .member("width", display.width).member("height", display.height)
        .member("configurable", display.configurable).member("requestedWidth", requestedWidth)
        .member("requestedHeight", display.height)
        .member("restartRequired", display.configurable && requestedWidth != display.width)
        .member("ready", display.ready).member("minWidth", limits.minWidth)
        .member("maxWidth", limits.maxWidth).member("minHeight", limits.minHeight)
        .member("maxHeight", limits.maxHeight).member("maxPixels", limits.maxPixels);
    if (display.estimatedWireTimeUs > 0)
      writer.member("estimatedWireTimeUs", display.estimatedWireTimeUs).member("wireTimeIsEstimate", true);
    writer.endObject();
  }
  if (fonts) {
    writer.key("fonts").beginArray();
    for (std::size_t i = 0; i < fonts->count; ++i) {
      const auto& entry = fonts->entries[i];
      if (!entry.font) continue;
      writer.beginObject().member("name", entry.name).member("ascent", entry.ascent)
          .member("descent", entry.descent).member("lineHeight", entry.lineHeight).endObject();
    }
    writer.endArray();
  }
  if (platformMembers) platformMembers(writer);
  writer.endObject();
  return out;
}

}
}
