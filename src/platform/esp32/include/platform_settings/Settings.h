#pragma once

namespace awtrix {
struct Settings;
namespace api { class JsonReader; }
enum class ClockProfile { Any };
inline constexpr ClockProfile kClassicClockProfile = ClockProfile::Any;
class BuiltinAppProfile {
 public:
  constexpr bool clockFaces() const { return false; }
  static constexpr bool offers(ClockProfile, bool) { return true; }
};
inline void restorePlatformSettings(Settings&, const api::JsonReader&) {}
}
