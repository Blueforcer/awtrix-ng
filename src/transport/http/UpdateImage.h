#pragma once

#if !defined(AWTRIX_NATIVE)
#include <sdkconfig.h>
#endif

namespace awtrix {

#if defined(AWTRIX_NATIVE)
constexpr const char* kUpdateImageName = "";
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
