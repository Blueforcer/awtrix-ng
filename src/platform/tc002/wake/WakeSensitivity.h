#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace awtrix::tc002::wake {

enum class WakeSensitivity { Low, Normal, High };

struct WakeSensitivityLevel {
  WakeSensitivity level;
  std::string_view name;
  float doubtScale;
};

inline constexpr std::array<WakeSensitivityLevel, 3> kWakeSensitivityLevels{{
    {WakeSensitivity::Low, "low", 0.57f},
    {WakeSensitivity::Normal, "normal", 1.0f},
    {WakeSensitivity::High, "high", 1.79f},
}};

inline const WakeSensitivityLevel& wakeSensitivityLevel(WakeSensitivity level) {
  return kWakeSensitivityLevels[static_cast<std::size_t>(level)];
}

inline std::string_view wakeSensitivityName(WakeSensitivity level) { return wakeSensitivityLevel(level).name; }

inline bool parseWakeSensitivity(std::string_view name, WakeSensitivity& out) {
  for (const WakeSensitivityLevel& known : kWakeSensitivityLevels) {
    if (known.name != name) continue;
    out = known.level;
    return true;
  }
  return false;
}

inline float wakeCutoff(float modelCutoff, WakeSensitivity level) {
  const float allowedDoubt = (1.0f - modelCutoff) * wakeSensitivityLevel(level).doubtScale;
  return std::max(0.0f, 1.0f - allowedDoubt);
}

}
