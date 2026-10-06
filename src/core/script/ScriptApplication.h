#pragma once

#include <cstdint>
#include <string>

namespace awtrix {
struct Settings;
struct RuntimeState;

namespace script {

// The prelude uses these ordinals; Release belongs to the host when a script stops.
enum class SoundAction : uint8_t { Play, Effect, Stop, StopMusic, Release };

// Borrowed application operations. Missing operations return an unavailable result.
class IScriptApplication {
 public:
  virtual ~IScriptApplication() = default;
  // The caller name comes from the binding, not the notification payload.
  virtual bool notify(const std::string&, const std::string&) { return false; }
  virtual const Settings* settings() { return nullptr; }
  virtual const RuntimeState* runtime() { return nullptr; }
  virtual bool setSettings(const std::string&) { return false; }
  virtual bool setDisplayPower(bool) { return false; }
  // Returns 1 accepted, 0 unavailable, or -1 with an error; the binding names the caller.
  virtual int sound(SoundAction, const std::string&, const std::string&, std::string&) { return 0; }
  virtual bool soundPlaying() { return false; }
  virtual bool audioPlaying() { return false; }
  // MP3, RTTTL, song, speech, track, radio, URL, effect and clip occupy bits 0..8.
  virtual int soundCaps() { return 0; }
  virtual void rotateNext() {}
  virtual void rotatePrevious() {}
  virtual bool showApp(const std::string&) { return false; }
  virtual void holdRotation(bool) {}
  virtual void restartTurn() {}
  // Ends an on-demand session after the current script call returns.
  virtual bool closeSession(const std::string&) { return false; }
};

}
}
