#pragma once

namespace awtrix::net {

inline constexpr int kWifiExcellent = -55;
inline constexpr int kWifiGood = -65;
inline constexpr int kWifiFair = -75;

// Zero means no signal was reported.
inline constexpr int signalBars(int rssi) {
  return rssi == 0 ? 0 : rssi >= kWifiExcellent ? 4 : rssi >= kWifiGood ? 3 : rssi >= kWifiFair ? 2 : 1;
}

}
