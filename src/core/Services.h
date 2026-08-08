#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Command.h"

namespace awtrix {

class StateStore;
class EffectRegistry;

class IAppService {
 public:
  virtual ~IAppService() = default;
  virtual DispatchResult setPushedApp(const std::string& name, const std::string& json,
                                      DispatchDetail& detail) = 0;
  virtual void deletePushedApp(const std::string& name) = 0;
  virtual bool setAppOrder(const std::string& json) = 0;
  virtual bool switchApp(const std::string& nameOrJson) = 0;
  virtual void nextApp() = 0;
  virtual void previousApp() = 0;
};

class INotifyService {
 public:
  virtual ~INotifyService() = default;
  virtual DispatchResult notify(const std::string& json, uint8_t source,
                                DispatchDetail& detail) = 0;
  virtual void dismiss() = 0;
  virtual bool dismissNamed(const std::string& name) = 0;
};

enum class SoundKind { Any, Clip, Melody };

class ISoundService {
 public:
  virtual ~ISoundService() = default;
  // SoundKind::Any is what a script means by a name: a clip wins over a melody of the same name.
  // The API names the kind instead, so "no clip called ding" cannot be answered with a melody.
  virtual bool playSound(const std::string& name, SoundKind kind) = 0;
  virtual void playRtttl(const std::string& rtttl) = 0;
  virtual void r2d2(const std::string& payload) = 0;
  virtual void stop() = 0;
  virtual bool supportsRtttl() const { return true; }
};

class IDisplayService {
 public:
  virtual ~IDisplayService() = default;
  virtual void sendScreen() = 0;
};

// All four of these are expected to be deferred, not immediate: the implementation records what
// was asked for and carries it out after the loop, so the caller can still send its reply.
class ISystemService {
 public:
  virtual ~ISystemService() = default;
  virtual void reboot() = 0;
  virtual void sleep(uint64_t durationMs) = 0;
  virtual void factoryReset() = 0;
  virtual void resetSettings() = 0;
};

class IRadioService {
 public:
  virtual ~IRadioService() = default;
  virtual DispatchResult play(const std::string& url, const std::string& label,
                              DispatchDetail& detail) = 0;
  virtual void stop() = 0;
  virtual void setVolume(int percent) = 0;
  virtual bool isPlaying() const = 0;
  virtual uint32_t underruns() const { return 0; }
  virtual uint32_t decodeUs() const { return 0; }
  virtual uint32_t starvedMs() const { return 0; }
  virtual uint32_t bufferBytes() const { return 0; }
};

// Plays a stored MP3 clip on the same output the radio uses. The caller hands in a validated
// "/CLIPS/<name>.mp3" path; whether the file exists is the caller's problem.
class IClipService {
 public:
  virtual ~IClipService() = default;
  virtual bool playClip(const std::string& path) = 0;
  virtual void stopClip() = 0;
  virtual bool clipPlaying() const = 0;
};

class IRadioStations {
 public:
  virtual ~IRadioStations() = default;
  virtual DispatchResult setStations(const std::string& json, DispatchDetail& detail) = 0;
  virtual std::string stationsJson() const = 0;
  virtual std::string stationUrl(const std::string& name) const = 0;
  virtual std::string stationNameAt(int index) const = 0;
};

class IScriptService {
 public:
  virtual ~IScriptService() = default;
  virtual DispatchResult setScript(const std::string& name, const std::string& source,
                                   DispatchDetail& detail) = 0;
  virtual void removeScript(const std::string& name) = 0;
  virtual DispatchResult setScriptConfig(const std::string& name, const std::string& json,
                                         DispatchDetail& detail) {
    (void)name;
    (void)json;
    detail.message = "scripting is disabled (scriptingEnabled is off)";
    return DispatchResult::Unavailable;
  }
  // These four are polled by CoreEngine every tick for the app on screen, so keep them cheap. The
  // defaults are what a script that does not care about the question answers.
  virtual bool scriptWantsShow(const std::string& name) {
    (void)name;
    return true;
  }
  virtual long scriptDurationMs(const std::string& name) {
    (void)name;
    return 0;
  }
  virtual bool scriptScrollHolds(const std::string& name) {
    (void)name;
    return false;
  }
  virtual bool scriptIsHeadless(const std::string& name) {
    (void)name;
    return false;
  }
  virtual void setRunningScripts(const std::vector<std::string>& running) { (void)running; }
};

// Handed to the dispatcher for the duration of one command. The references are always live; the
// pointers stay null when the build or the config leaves that feature out.
struct CommandContext {
  StateStore& state;
  IAppService& apps;
  INotifyService& notify;
  ISoundService& sound;
  IDisplayService& display;
  ISystemService& system;
  IScriptService* scripts = nullptr;
  IRadioService* radio = nullptr;
  IRadioStations* stations = nullptr;
  const EffectRegistry* overlays = nullptr;
  DispatchDetail detail{};
};

}
