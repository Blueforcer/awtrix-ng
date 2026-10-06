#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "core/PlatformDescriptor.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {

// Shared @needs flags plus the entry point's optional capabilities. Absent names are unavailable.
struct DeviceCapabilities {
  static constexpr std::array<std::string_view, 9> kNames = {
      "audio.mp3", "audio.rtttl", "audio.song", "audio.speech", "audio.track", "audio.radio",
      "audio.effect", "microphone", "sensors.light"};

  struct Extra {
    const char* name = nullptr;
    bool present = false;
  };

  std::array<bool, kNames.size()> present{};
  int panelWidth = 0;
  int panelHeight = 0;
  // Borrowed list, terminated by a null name; it must outlive every copy of these capabilities.
  const Extra* extra = nullptr;

  static DeviceCapabilities from(const sound::Caps& audio, const PlatformDescriptor* platform,
                                 const Extra* extra = nullptr) {
    DeviceCapabilities caps;
    caps.present = {audio.mp3,   audio.rtttl, audio.song,   audio.speech,
                    audio.track, audio.radio, audio.effect, platform && platform->microphonePcm,
                    platform && platform->lightSensor};
    caps.extra = extra;
    if (platform) {
      caps.panelWidth = platform->display.width;
      caps.panelHeight = platform->display.height;
    }
    return caps;
  }

  // A name the table does not know is a capability this firmware cannot provide.
  bool has(std::string_view name) const {
    for (std::size_t i = 0; i < kNames.size(); ++i)
      if (kNames[i] == name) return present[i];
    for (const Extra* item = extra; item && item->name; ++item)
      if (name == item->name) return item->present;
    return false;
  }

  // An @display of 0 x 0 fits any panel.
  bool fits(int width, int height) const {
    return panelWidth >= width && panelHeight >= height;
  }
};

}
