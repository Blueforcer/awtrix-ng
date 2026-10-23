// BatteryGauge with voltages as the TC002's MCU reports them and synthetic sample times.
#include "platform/tc002/daemon/BatteryGauge.h"
#include "support.h"

using awtrix::tc002d::BatteryGauge;
using tc002d_test::check;

namespace {

constexpr int64_t kStep = 10000;  // the daemon's battery query interval
constexpr int kWindow = static_cast<int>(BatteryGauge::kWindow);

// One reading per query interval; returns the time of the next one.
int64_t feed(BatteryGauge& gauge, int millivolts, bool usb, int count, int64_t now) {
  for (int i = 0; i < count; ++i, now += kStep) gauge.sample(millivolts, usb, now);
  return now;
}

void estimates() {
  check(BatteryGauge::estimate(4100, true) == 100, "charger full voltage is 100 %");
  check(BatteryGauge::estimate(4080, true) == 100, "measured: the charger reached its full voltage at 4080 mV");
  check(BatteryGauge::estimate(4079, true) == 99, "below it the charge is still running");
  check(BatteryGauge::estimate(3940, true) == 50, "measured: half the charging time at 3940 mV");
  check(BatteryGauge::estimate(4089, false) == 99, "rounds down: 100 % needs the top reading");
  check(BatteryGauge::estimate(4090, false) == 100, "a full cell on battery is 100 %");
  check(BatteryGauge::estimate(3890, false) == 50, "measured: half the runtime left at 3890 mV");
  check(BatteryGauge::estimate(3660, false) == 10, "measured: a tenth left at 3660 mV");
  check(BatteryGauge::estimate(3905, false) == 52, "interpolated between measured points");
  check(BatteryGauge::estimate(3900, true) < BatteryGauge::estimate(3900, false),
        "the same reading means less charge while charging");
  check(BatteryGauge::estimate(3000, false) == 0 && BatteryGauge::estimate(3000, true) == 0, "empty clamps to 0 %");
  check(BatteryGauge::estimate(4300, false) == 100, "above full clamps to 100 %");
  int previous = -1;
  bool rising = true;
  for (int mv = 3400; mv <= 4200; mv += 5) {
    const int now = BatteryGauge::estimate(mv, false);
    rising = rising && now >= previous;
    previous = now;
  }
  check(rising, "higher readings never mean less charge");
}

void reportedFullCharge() {
  // The MCU said 72 % here, with USB connected and the charger at its full voltage.
  BatteryGauge gauge;
  check(gauge.percent() == -1, "unknown before the first sample");
  int64_t now = feed(gauge, 4100, true, 1, 0);
  check(gauge.percent() == 100, "first sample at full voltage on USB reads 100 %");
  feed(gauge, 4095, true, 2 * kWindow, now);
  check(gauge.percent() == 100, "ADC steps below full do not drop it");

  // Measured: plugged in on battery at 4000 mV, the reading reached the full voltage at once.
  BatteryGauge plugged;
  now = feed(plugged, 4000, false, kWindow, 0);
  const int before = plugged.percent();
  now = feed(plugged, 4095, true, kWindow - 1, now);
  check(plugged.percent() == before, "holds until the window is full of charging samples");
  feed(plugged, 4095, true, 1, now);
  check(plugged.percent() == 100, "then the charger's full voltage reads 100 %");
}

void charging() {
  BatteryGauge gauge;
  int64_t now = feed(gauge, 3900, true, kWindow, 0);
  const int start = gauge.percent();
  check(start > 0 && start < 100, "a partly charged cell on USB");
  now = feed(gauge, 3850, true, kWindow, now);
  check(gauge.percent() == start, "never falls while USB is connected");
  now = feed(gauge, 4000, true, kWindow, now);
  check(gauge.percent() > start && gauge.percent() < 100, "rises with the charging voltage");
  now = feed(gauge, 4079, true, kWindow, now);
  check(gauge.percent() > start && gauge.percent() < 100, "below the full voltage it never reaches 100 %");
  feed(gauge, 4100, true, kWindow, now);
  check(gauge.percent() == 100, "reaches 100 % at the charger's full voltage");
}

void discharging() {
  BatteryGauge gauge;
  int64_t now = feed(gauge, 3950, false, kWindow, 0);
  const int start = gauge.percent();
  check(start > 0 && start < 100, "a partly charged cell on battery");
  now = feed(gauge, 4000, false, kWindow, now);
  check(gauge.percent() == start, "a lighter load does not raise it");
  now = feed(gauge, 3700, false, kWindow / 2, now);
  check(gauge.percent() == start, "a load for less than half the window is filtered out");
  now = feed(gauge, 3950, false, kWindow, now);
  feed(gauge, 3800, false, kWindow / 2 + 1, now);
  check(gauge.percent() < start, "a lasting lower voltage lowers it");
}

void usbChanges() {
  BatteryGauge gauge;
  int64_t now = feed(gauge, 3800, false, kWindow, 0);
  const int onBattery = gauge.percent();
  now = feed(gauge, 3950, true, kWindow - 1, now);
  check(gauge.percent() == onBattery, "plugging in holds the value until the window is full");
  now = feed(gauge, 3950, true, 1, now);
  check(gauge.percent() > onBattery, "then it follows the charge");

  now = feed(gauge, 4100, true, kWindow, now);
  check(gauge.percent() == 100, "charged");
  now = feed(gauge, 3900, false, kWindow - 1, now);
  check(gauge.percent() == 100, "unplugging holds the value until the window is full");
  now = feed(gauge, 3900, false, 1, now);
  const int discharged = gauge.percent();
  check(discharged < 100, "then it follows the discharge");
  feed(gauge, 3900, true, kWindow, now);
  check(gauge.percent() >= discharged, "plugging in never lowers it");
}

void startHolds() {
  BatteryGauge gauge;
  int64_t now = feed(gauge, 4050, false, 1, 0);
  const int first = gauge.percent();
  now = feed(gauge, 3700, false, kWindow / 2, now);
  check(gauge.percent() == first, "the first sample holds while the boot load fills the window");
  feed(gauge, 4050, false, kWindow / 2 + 1, now);
  check(gauge.percent() == first, "and the boot load does not count once the window is full");
}

void spacingAndUnknown() {
  BatteryGauge gauge;
  gauge.sample(-1, false, 0);
  check(gauge.percent() == -1, "an unknown voltage is no sample");
  int64_t now = feed(gauge, 3950, false, kWindow, 0);
  const int start = gauge.percent();
  for (int i = 0; i < 2 * kWindow; ++i) gauge.sample(3600, false, now + i * 30);
  check(gauge.percent() == start, "a burst of stream reports counts once");
}

}

int main() {
  estimates();
  reportedFullCharge();
  charging();
  discharging();
  usbChanges();
  startHolds();
  spacingAndUnknown();
  return tc002d_test::finish("tc002d battery gauge");
}
