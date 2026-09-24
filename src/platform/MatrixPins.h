#pragma once
#include "core/SocProfile.h"

// FastLED instantiates one driver per supported data pin.
#if defined(AWTRIX_SOC_ESP32S3)
#define AWTRIX_MATRIX_PIN_LIST(X) AWTRIX_MATRIX_PINS_ESP32S3(X)
#define AWTRIX_MATRIX_FALLBACK_PIN 21
#else
#define AWTRIX_MATRIX_PIN_LIST(X) AWTRIX_MATRIX_PINS_ESP32(X)
#define AWTRIX_MATRIX_FALLBACK_PIN 32
#endif
