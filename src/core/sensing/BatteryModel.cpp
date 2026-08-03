#include "core/sensing/BatteryModel.h"

#include <cmath>
#include <cstddef>

namespace awtrix {

namespace {
struct CurvePoint {
  float volts;
  uint8_t percent;
};

// Resting-voltage to charge curve for a single Li-ion cell, in descending voltage order. The
// middle is deliberately flat, which is why a lookup with interpolation beats any formula here.
constexpr CurvePoint kCurve[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80},
    {3.98f, 75},  {3.95f, 70}, {3.91f, 65}, {3.87f, 60}, {3.85f, 55},
    {3.84f, 50},  {3.82f, 45}, {3.80f, 40}, {3.79f, 35}, {3.77f, 30},
    {3.75f, 25},  {3.73f, 20}, {3.71f, 15}, {3.69f, 10}, {3.61f, 5},
    {3.27f, 0},
};
constexpr std::size_t kCurveLen = sizeof(kCurve) / sizeof(kCurve[0]);
}

// The ADC pin sees the cell through a resistor divider, so scale back up. dividerRatio is cell
// volts per pin volt, board-specific, and calibrated by the user against a full cell.
float cellVoltsFromPinMillivolts(int pinMillivolts, float dividerRatio) {
  if (pinMillivolts < 0) return 0.0f;
  const float ratio = dividerRatio > 0.0f ? dividerRatio : kDefaultBatteryDividerRatio;
  return static_cast<float>(pinMillivolts) / 1000.0f * ratio;
}

uint8_t socFromVolts(float cellVolts) {
  if (cellVolts >= kCurve[0].volts) return kCurve[0].percent;
  if (cellVolts <= kCurve[kCurveLen - 1].volts) return kCurve[kCurveLen - 1].percent;
  for (std::size_t i = 1; i < kCurveLen; ++i) {
    if (cellVolts >= kCurve[i].volts) {
      const CurvePoint& hi = kCurve[i - 1];
      const CurvePoint& lo = kCurve[i];
      const float t = (cellVolts - lo.volts) / (hi.volts - lo.volts);
      return static_cast<uint8_t>(std::lround(lo.percent + t * (hi.percent - lo.percent)));
    }
  }
  return 0;
}

}
