#pragma once

namespace awtrix::hostd {

inline constexpr int kDefaultPort = 8300;
inline constexpr unsigned long kLogKeepBytes = 1024 * 1024;

inline constexpr int kExitFailure = 1;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitPortBusy = 10;

}
