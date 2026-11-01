#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Command.h"
#include "platform/posix/Hold.h"
#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/voice/KnobGesture.h"

namespace awtrix::script {
struct ScriptMeta;
}

namespace awtrix::tc002::wake {
class WakeListener;
}

namespace awtrix::tc002::voice {

enum class VoiceTrigger { Wake, Knob, Mqtt, Api };

class AssistantRouter {
 public:
  class Voice {
   public:
    virtual ~Voice() = default;
    virtual bool homeAssistant() const = 0;
    virtual const std::string& assistantApp() const = 0;
    virtual bool wakeWord() const = 0;
    virtual bool ready() const = 0;
    virtual bool busy() const = 0;
    virtual bool starting() const = 0;
    virtual bool begin(int64_t now) = 0;
  };

  class Module {
   public:
    static constexpr const char* kName = "voice";

    virtual ~Module() = default;
    virtual bool busy() const = 0;
    virtual bool starting() const = 0;
    virtual bool triggered() const = 0;
    virtual bool holds(const std::string& app) const = 0;
    virtual void trigger(const std::string& app, const std::string& name, MicrophoneMark mark) = 0;
    virtual void dropTrigger() = 0;
  };

  struct Ports {
    std::function<bool()> allowed;
    std::function<const script::ScriptMeta*(const std::string&)> meta;
    std::function<std::vector<std::string>()> scripts;
    std::function<bool(const std::string&)> runnable;
    std::function<DispatchResult(const std::string&, DispatchDetail&)> switchApp;
    std::function<bool(const std::string&)> shown;
    std::function<bool()> panelOff;
    std::function<void(bool)> setPanelPower;
    std::function<void()> dismissQuickSettings;
    std::function<posix::Hold()> holdMusic;
  };

  struct Parts {
    Voice& voice;
    Module* module;
    KnobGesture& gesture;
    MicrophoneHub& microphone;
    wake::WakeListener* wake;
    const MicrophoneHub::Consumer& wakeMicrophone;
  };

  static constexpr int64_t kShowTimeoutMs = 3000, kAssistantIdleMs = 10000;

  AssistantRouter(Ports ports, const Parts& parts);
  AssistantRouter(const AssistantRouter&) = delete;
  AssistantRouter& operator=(const AssistantRouter&) = delete;

  std::vector<std::string> apps() const;
  bool start(VoiceTrigger trigger, int64_t now);
  void tick(int64_t now, bool knob);

 private:
  struct Interaction {
    std::string app;
    int64_t startedAt = 0, activeAt = 0;
    bool panelOff = false;
    posix::Hold music;
  };

  bool present() const;
  bool ready() const;
  bool homeAssistantReady() const;
  bool answers(const std::string& app) const;
  bool startable(const std::string& app) const;
  bool startFrom(VoiceTrigger trigger, MicrophoneMark mark, int64_t now);
  bool startHomeAssistant(int64_t now);
  bool startApp(const std::string& app, VoiceTrigger trigger, MicrophoneMark mark, int64_t now);
  void tickInteraction(int64_t now);
  bool holdsMicrophone() const;

  Ports ports_;
  Parts parts_;
  std::optional<Interaction> interaction_;
};

}
