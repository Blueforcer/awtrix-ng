#pragma once

#include "core/render/DisplayProfile.h"

namespace awtrix {

// Filled in by the entry point.
struct PlatformDescriptor {
  const char* id = "";
  DisplayProfile display;
  bool configurableGpio = true;
  bool lightSensor = false;
  // A PCM input can feed visualization, independently of audio output capabilities.
  bool microphonePcm = false;
};

}
