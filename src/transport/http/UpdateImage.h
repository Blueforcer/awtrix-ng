#pragma once

#if !defined(AWTRIX_NATIVE) && !defined(AWTRIX_PLATFORM_RP2040)
#include <sdkconfig.h>
#endif

namespace awtrix {

#if defined(AWTRIX_NATIVE)
constexpr const char* kUpdateImageName = "";
#elif defined(AWTRIX_PLATFORM_RP2040) && defined(PICO_RP2350) && PICO_RP2350
constexpr const char* kUpdateImageName = "firmware-galactic-unicorn-2w.uf2";
#elif defined(AWTRIX_PLATFORM_RP2040)
constexpr const char* kUpdateImageName = "firmware-galactic-unicorn.uf2";
#elif defined(AWTRIX_SOC_ESP32S3)
#if defined(CONFIG_SPIRAM_MODE_QUAD)
constexpr const char* kUpdateImageName = "firmware-awtrix-ng-s3-quad.bin";
#else
constexpr const char* kUpdateImageName = "firmware-awtrix-ng-s3-octal.bin";
#endif
#else
constexpr const char* kUpdateImageName = "firmware-awtrix-ng.bin";
#endif

}
