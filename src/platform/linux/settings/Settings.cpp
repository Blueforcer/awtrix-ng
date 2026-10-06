#include "core/Settings.h"
#include "core/api/JsonCoerce.h"

namespace awtrix {
void restorePlatformSettings(Settings& settings, const api::JsonReader& reader) {
  if (api::present(api::memberValue(reader, "clockFace"))) return;
  if (settings.timeMode == 0 || settings.timeMode == 5) settings.clockFace = kClockFaceBig;
  else if (settings.timeMode == 3 || settings.timeMode == 4) settings.clockFace = kClockFaceRing;
  else settings.clockFace = kClockFaceSheet;
}
}
