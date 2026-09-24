#pragma once
#include "core/FeatureSet.h"

#ifndef AWTRIX_FEATURE_SCRIPTING
#define AWTRIX_FEATURE_SCRIPTING 1
#endif
#ifndef AWTRIX_FEATURE_MP3
#define AWTRIX_FEATURE_MP3 1
#endif
#ifndef AWTRIX_FEATURE_RADIO
#define AWTRIX_FEATURE_RADIO 1
#endif
#ifndef AWTRIX_FEATURE_OUTBOUND_TLS
#define AWTRIX_FEATURE_OUTBOUND_TLS 1
#endif
#ifndef AWTRIX_FEATURE_BROWSER_OTA
#define AWTRIX_FEATURE_BROWSER_OTA 1
#endif

namespace awtrix::platform {
inline constexpr FeatureSet buildFeatures() {
  return {bool(AWTRIX_FEATURE_SCRIPTING), bool(AWTRIX_FEATURE_MP3),
          bool(AWTRIX_FEATURE_RADIO), bool(AWTRIX_FEATURE_OUTBOUND_TLS),
          bool(AWTRIX_FEATURE_BROWSER_OTA)};
}
}
