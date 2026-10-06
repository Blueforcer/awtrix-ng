#pragma once

#include <functional>

#include "core/script/ScriptApplication.h"

namespace awtrix::test {

struct ScriptApplication : script::IScriptApplication {
  using SoundAction = script::SoundAction;
  std::function<bool(const std::string&, const std::string&)> notifyFn;
  std::function<const Settings*()> settingsFn;
  std::function<const RuntimeState*()> runtimeFn;
  std::function<bool(const std::string&)> setSettingsFn;
  std::function<bool(bool)> setDisplayPowerFn;
  std::function<int(SoundAction, const std::string&, const std::string&, std::string&)> soundFn;
  std::function<bool()> soundPlayingFn;
  std::function<bool()> audioPlayingFn;
  std::function<int()> soundCapsFn;
  std::function<void()> rotateNextFn;
  std::function<void()> rotatePreviousFn;
  std::function<bool(const std::string&)> showAppFn;
  std::function<void(bool)> holdRotationFn;
  std::function<void()> restartTurnFn;
  std::function<bool(const std::string&)> closeSessionFn;

  bool notify(const std::string& json, const std::string& script) override { return notifyFn ? notifyFn(json, script) : false; }
  const Settings* settings() override { return settingsFn ? settingsFn() : nullptr; }
  const RuntimeState* runtime() override { return runtimeFn ? runtimeFn() : nullptr; }
  bool setSettings(const std::string& json) override { return setSettingsFn ? setSettingsFn(json) : false; }
  bool setDisplayPower(bool on) override { return setDisplayPowerFn ? setDisplayPowerFn(on) : false; }
  int sound(SoundAction action, const std::string& json, const std::string& script, std::string& error) override { return soundFn ? soundFn(action, json, script, error) : 0; }
  bool soundPlaying() override { return soundPlayingFn ? soundPlayingFn() : false; }
  bool audioPlaying() override { return audioPlayingFn ? audioPlayingFn() : false; }
  int soundCaps() override { return soundCapsFn ? soundCapsFn() : 0; }
  void rotateNext() override { if (rotateNextFn) rotateNextFn(); }
  void rotatePrevious() override { if (rotatePreviousFn) rotatePreviousFn(); }
  bool showApp(const std::string& id) override { return showAppFn ? showAppFn(id) : false; }
  void holdRotation(bool hold) override { if (holdRotationFn) holdRotationFn(hold); }
  void restartTurn() override { if (restartTurnFn) restartTurnFn(); }
  bool closeSession(const std::string& id) override { return closeSessionFn ? closeSessionFn(id) : false; }
};

}
