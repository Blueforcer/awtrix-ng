#pragma once

#include "core/script/ScriptApplication.h"

namespace awtrix {
class CoreEngine;
namespace sound { class AudioRouter; }

namespace script {

class EngineScriptApplication final : public IScriptApplication {
 public:
  EngineScriptApplication(CoreEngine& engine, sound::AudioRouter& audio)
      : engine_(engine), audio_(audio) {}
  bool notify(const std::string& json, const std::string& script) override;
  const Settings* settings() override;
  const RuntimeState* runtime() override;
  bool setSettings(const std::string& json) override;
  bool setDisplayPower(bool on) override;
  int sound(SoundAction action, const std::string& json, const std::string& script,
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
  sound::AudioRouter& audio_;
};

}
}
