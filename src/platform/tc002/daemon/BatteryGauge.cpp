#include "platform/tc002/daemon/BatteryGauge.h"

#include <algorithm>

namespace awtrix::tc002d {

namespace {
struct Point {
  int millivolts, percent;
};

// Measured on a TC002 on 2026-10-09 with the display at full brightness throughout, readings as the
// MCU reports them. A second TC002 stayed within 4 % of the discharge curve.
// On battery: from a full charge until it switched off (6 h 50 min), against the share of the
// runtime left.
constexpr Point kDischarge[] = {
    {4090, 100}, {4070, 95}, {4050, 90}, {4030, 85}, {4010, 78}, {4000, 70}, {3980, 65},
    {3950, 60},  {3920, 55}, {3890, 50}, {3870, 45}, {3850, 40}, {3840, 35}, {3820, 30},
    {3790, 25},  {3760, 20}, {3720, 15}, {3660, 10}, {3590, 6},  {3550, 5},  {3430, 3},
    {3240, 1},   {3100, 0},
};
// On USB: from empty until the charger reached its full voltage (2 h 27 min at constant current),
// against the share of that time.
constexpr Point kCharge[] = {
    {4080, 100}, {4050, 95}, {4030, 90}, {4010, 85}, {3990, 78}, {3970, 68}, {3960, 60},
    {3950, 55},  {3940, 50}, {3930, 45}, {3910, 40}, {3890, 35}, {3870, 30}, {3840, 25},
    {3800, 20},  {3760, 15}, {3670, 10}, {3600, 8},  {3480, 5},  {3270, 2},  {3100, 0},
};

// Interpolates and rounds down, so the top percentage needs the top reading.
template <std::size_t N>
int lookup(const Point (&curve)[N], int millivolts) {
  if (millivolts >= curve[0].millivolts) return curve[0].percent;
  for (std::size_t i = 1; i < N; ++i) {
    const Point& lo = curve[i];
    if (millivolts < lo.millivolts) continue;
    const Point& hi = curve[i - 1];
    return lo.percent + (millivolts - lo.millivolts) * (hi.percent - lo.percent) / (hi.millivolts - lo.millivolts);
  }
  return curve[N - 1].percent;
}
}

int BatteryGauge::estimate(int millivolts, bool usb) {
  return usb ? lookup(kCharge, millivolts) : lookup(kDischarge, millivolts);
}

void BatteryGauge::sample(int millivolts, bool usb, int64_t nowMs) {
  if (millivolts < 0) return;
  if (percent_ < 0 || usb != usb_) {
    if (percent_ < 0) percent_ = estimate(millivolts, usb);
    usb_ = usb;
    count_ = next_ = 0;
  } else if (nowMs - acceptedAt_ < kSpacingMs) {
    return;
  }
  acceptedAt_ = nowMs;
  window_[next_] = millivolts;
  next_ = (next_ + 1) % window_.size();
  if (count_ < window_.size()) ++count_;
  if (count_ < window_.size()) return;
  auto sorted = window_;
  std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
  const int now = estimate(sorted[sorted.size() / 2], usb);
  percent_ = usb ? std::max(percent_, now) : std::min(percent_, now);
}

}
