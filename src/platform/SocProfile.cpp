#include "core/SocProfile.h"

namespace awtrix::pins {
// Build selection belongs at the platform boundary, not in portable policy.
const SocProfile& activeProfile() {
#if defined(AWTRIX_SOC_ESP32S3)
  return esp32s3Profile();
#elif defined(AWTRIX_SOC_RP2040)
  return rp2040Profile();
#else
  return esp32Profile();
#endif
}
}
