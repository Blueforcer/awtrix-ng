#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace awtrix::tc002d {

// Charge percentage of the TC002 cell from the MCU's battery voltage and USB samples. The MCU's own
// percentage needs 4200 mV for 100 %, but it measures the charger's full voltage at 4100 to 4130 mV,
// and it keeps stale values across USB changes, so it is not used.
//
// The median of the last kWindow samples, at least kSpacingMs apart (about five minutes), goes
// through curves measured on a TC002, so short loads such as a sound do not count. The first sample
// sets the percentage; after that and after every USB change it holds until the window is full of
// new samples. On battery it only falls, along the discharge curve. On USB it only rises, along the
// charge curve up to 100 % at the charger's full voltage. Nothing the MCU reports tells how far the
// charger is past that point, so 100 % comes before the charge light turns green.
class BatteryGauge {
 public:
  static constexpr std::size_t kWindow = 31;
  static constexpr int64_t kSpacingMs = 9000;  // the daemon queries every 10 s

  void sample(int millivolts, bool usb, int64_t nowMs);
  int percent() const { return percent_; }  // -1 before the first sample

  static int estimate(int millivolts, bool usb);

 private:
  std::array<int, kWindow> window_{};
  std::size_t count_ = 0, next_ = 0;
  bool usb_ = false;
  int percent_ = -1;
  int64_t acceptedAt_ = 0;
};

}
