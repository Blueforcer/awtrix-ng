#pragma once

#include <cstddef>

// FastLED takes the data pin as a template argument, so every pin the matrix may use has to be
// instantiated at compile time. MatrixRenderer expands this list into a switch; keep them in sync.
#define AWTRIX_MATRIX_PINS_ESP32(X) \
  X(2) X(4) X(5) X(13) X(14) X(15) X(16) X(18) X(21) X(25) X(26) X(27) X(32) X(33)

#define AWTRIX_MATRIX_PINS_ESP32S3(X) \
  X(13) X(14) X(15) X(16) X(17) X(18) X(21) X(38) X(39) X(40) X(41) X(42) X(47)

#if defined(AWTRIX_SOC_ESP32S3)
#define AWTRIX_MATRIX_PIN_LIST(X) AWTRIX_MATRIX_PINS_ESP32S3(X)
#define AWTRIX_MATRIX_FALLBACK_PIN 21
#else
#define AWTRIX_MATRIX_PIN_LIST(X) AWTRIX_MATRIX_PINS_ESP32(X)
#define AWTRIX_MATRIX_FALLBACK_PIN 32
#endif

namespace awtrix {
namespace pins {

struct PinSet {
  int matrix = -1;
  int btnLeft = -1;
  int btnSelect = -1;
  int btnRight = -1;
  int battery = -1;
  int ldr = -1;
  int buzzer = -1;
  int i2cSda = -1;
  int i2cScl = -1;
  int dfRx = -1;
  int dfTx = -1;
  bool dfplayerEnabled = false;
  int i2sBclk = -1;
  int i2sLrclk = -1;
  int i2sDout = -1;
};

struct PinRange {
  int lo;
  int hi;
};

struct ReservedRange {
  int lo;
  int hi;
  const char* why;
};

struct RangeList {
  const PinRange* items;
  std::size_t count;

  bool contains(int pin) const {
    for (std::size_t i = 0; i < count; ++i)
      if (pin >= items[i].lo && pin <= items[i].hi) return true;
    return false;
  }
  bool empty() const { return count == 0; }
};

struct ReservedList {
  const ReservedRange* items;
  std::size_t count;

  const ReservedRange* find(int pin) const {
    for (std::size_t i = 0; i < count; ++i)
      if (pin >= items[i].lo && pin <= items[i].hi) return &items[i];
    return nullptr;
  }
};

struct PinList {
  const int* items;
  std::size_t count;

  bool contains(int pin) const {
    for (std::size_t i = 0; i < count; ++i)
      if (items[i] == pin) return true;
    return false;
  }
};

struct SocProfile {
  const char* id;
  const char* label;
  int gpioMax;
  RangeList missing;
  RangeList inputOnly;
  ReservedList reserved;
  RangeList adc1;
  RangeList strapping;
  RangeList rtc;
  PinList matrix;
  PinSet defaults;
};

namespace detail {

constexpr PinRange kEsp32InputOnly[] = {{34, 39}};
constexpr ReservedRange kEsp32Reserved[] = {{6, 11, "the SPI flash"}};
constexpr PinRange kEsp32Adc1[] = {{32, 39}};
constexpr PinRange kEsp32Strapping[] = {{0, 0}, {2, 2}, {5, 5}, {12, 12}, {15, 15}};
constexpr PinRange kEsp32Rtc[] = {{0, 0}, {2, 2}, {4, 4}, {12, 15}, {25, 27}, {32, 39}};
constexpr int kEsp32Matrix[] = {
#define X(p) p,
    AWTRIX_MATRIX_PINS_ESP32(X)
#undef X
};

constexpr PinRange kEsp32s3Missing[] = {{22, 25}};
constexpr ReservedRange kEsp32s3Reserved[] = {
    {19, 20, "the USB-JTAG interface"},
    {26, 37, "the SPI flash and PSRAM"},
    {43, 44, "the UART0 console"},
};
constexpr PinRange kEsp32s3Adc1[] = {{1, 10}};
constexpr PinRange kEsp32s3Strapping[] = {{0, 0}, {3, 3}, {45, 46}};
constexpr PinRange kEsp32s3Rtc[] = {{0, 21}};
constexpr int kEsp32s3Matrix[] = {
#define X(p) p,
    AWTRIX_MATRIX_PINS_ESP32S3(X)
#undef X
};

template <typename T, std::size_t N>
constexpr std::size_t countOf(const T (&)[N]) {
  return N;
}

}

inline const SocProfile& esp32Profile() {
  static const SocProfile p = {
      "esp32",
      "ESP32",
      39,
      {nullptr, 0},
      {detail::kEsp32InputOnly, detail::countOf(detail::kEsp32InputOnly)},
      {detail::kEsp32Reserved, detail::countOf(detail::kEsp32Reserved)},
      {detail::kEsp32Adc1, detail::countOf(detail::kEsp32Adc1)},
      {detail::kEsp32Strapping, detail::countOf(detail::kEsp32Strapping)},
      {detail::kEsp32Rtc, detail::countOf(detail::kEsp32Rtc)},
      {detail::kEsp32Matrix, detail::countOf(detail::kEsp32Matrix)},
      // Positional, in PinSet field order: matrix, btnLeft, btnSelect, btnRight, battery, ldr,
      // buzzer, i2cSda, i2cScl, dfRx, dfTx, dfplayerEnabled, i2sBclk, i2sLrclk, i2sDout.
      PinSet{32, 26, 27, 14, 34, 35, 15, 21, 22, 23, 18, false, -1, -1, -1},
  };
  return p;
}

inline const SocProfile& esp32s3Profile() {
  static const SocProfile p = {
      "esp32s3",
      "ESP32-S3",
      48,
      {detail::kEsp32s3Missing, detail::countOf(detail::kEsp32s3Missing)},
      {nullptr, 0},
      {detail::kEsp32s3Reserved, detail::countOf(detail::kEsp32s3Reserved)},
      {detail::kEsp32s3Adc1, detail::countOf(detail::kEsp32s3Adc1)},
      {detail::kEsp32s3Strapping, detail::countOf(detail::kEsp32s3Strapping)},
      {detail::kEsp32s3Rtc, detail::countOf(detail::kEsp32s3Rtc)},
      {detail::kEsp32s3Matrix, detail::countOf(detail::kEsp32s3Matrix)},
      PinSet{21, 11, 12, 13, 1, 2, 7, 8, 9, 17, 18, false, 5, 6, 4},
  };
  return p;
}

inline const SocProfile& activeProfile() {
#if defined(AWTRIX_SOC_ESP32S3)
  return esp32s3Profile();
#else
  return esp32Profile();
#endif
}

}
}
