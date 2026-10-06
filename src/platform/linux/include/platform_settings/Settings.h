#pragma once

namespace awtrix {
struct Settings;
namespace api { class JsonReader; }
constexpr int kClockFaceSheet = 0, kClockFaceRing = 1, kClockFaceFlap = 2, kClockFaceMonth = 3,
              kClockFaceBig = 4;
constexpr int kClockFaceCount = 5;
inline constexpr const char* kClockFaceNames[kClockFaceCount] = {"sheet", "ring", "flap", "month", "big"};
inline constexpr const char* kMusicSourceNames[] = {"auto", "playback", "microphone"};
enum class ClockProfile { Any, Classic, Faces };
inline constexpr ClockProfile kClassicClockProfile = ClockProfile::Classic;
class BuiltinAppProfile {
 public:
  void setClockFaces(bool faces) { clockFaces_ = faces; }
  bool clockFaces() const { return clockFaces_; }
  static constexpr bool offers(ClockProfile profile, bool faces) {
    return profile == ClockProfile::Any || (profile == ClockProfile::Faces ? faces : !faces);
  }
 private:
  bool clockFaces_ = false;
};
void restorePlatformSettings(Settings&, const api::JsonReader&);
}
