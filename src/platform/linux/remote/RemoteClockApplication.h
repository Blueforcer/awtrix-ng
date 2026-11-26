#pragma once

#include <set>
#include <string>

#include "core/script/ScriptApplication.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {
class CoreEngine;
}

namespace awtrix::remote {

class RemoteRole;

// What a script on the script host does to the clock goes there as an act, which the clock carries
// out as it would for a local script; settings and display power go to its HTTP API. Settings and
// readings are read from this process, which mirrors the clock's.
class RemoteClockApplication : public script::IScriptApplication {
 public:
  RemoteClockApplication(CoreEngine& engine, RemoteRole& role) : engine_(engine), role_(role) {}

  bool notify(const std::string& json, const std::string& script) override;
  const Settings* settings() override;
  const RuntimeState* runtime() override;
  bool setSettings(const std::string& json) override;
  bool setDisplayPower(bool on) override;
  int sound(script::SoundAction action, const std::string& json, const std::string& script,
            std::string& error) override;
  bool soundPlaying() override;
  bool audioPlaying() override;
  int soundCaps() override;
  void rotateNext() override;
  void rotatePrevious() override;
  bool showApp(const std::string& id) override;
  void holdRotation(bool hold) override;
  void restartTurn() override;
  bool closeSession(const std::string& id) override;

 private:
  CoreEngine& engine_;
  RemoteRole& role_;
  std::set<std::string> sounding_;
};

// The outputs the "audio" object of the clock's GET /api/v1/capabilities names.
sound::Caps soundCapsFrom(const std::string& capabilities);

}
