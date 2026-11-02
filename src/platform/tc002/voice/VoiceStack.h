#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "platform/linux/net/HttpClient.h"
#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/runtime/MicrophoneInput.h"
#include "platform/tc002/voice/AssistantRouter.h"
#include "platform/tc002/voice/KnobGesture.h"
#include "platform/tc002/voice/Tc002VoiceSpeaker.h"
#include "platform/tc002/voice/VoiceApi.h"
#include "platform/tc002/voice/VoiceRuntime.h"
#include "platform/tc002/voice/VoiceScripting.h"
#include "platform/tc002/wake/WakeListener.h"

namespace awtrix::tc002::voice {

class VoiceStack {
 public:
  struct Setup {
    MicrophoneHub::Send send;
    bool supervised = false;
    Tc002Speaker* speaker = nullptr;
    std::string wakeWordModel;
    std::string configDirectory;
  };
  struct ScriptSetup {
    std::function<bool(const std::string&)> visible;
    std::string voiceActivityModel;
    net::FillRequest fillRequest;
  };

  explicit VoiceStack(const Setup& setup);
  VoiceStack(const VoiceStack&) = delete;
  VoiceStack& operator=(const VoiceStack&) = delete;

  VoiceScripting* composeScripting(ScriptSetup setup);
  void composeRouter(AssistantRouter::Ports ports);

  void receive(const SupervisorMessage& message, int64_t now);
  void tick(int64_t now, bool allowed, bool knobAllowed);
  void draw(Canvas& canvas, int64_t now);
  bool handle(const httplib::Request& request, httplib::Response& response);
  bool start(VoiceTrigger trigger);
  void stop();
  void stopTransfers();
  bool eraseConfig();

  bool available() const { return runtime_ != nullptr; }
  MicrophoneInput& microphone() { return input_; }
  KnobGesture& gesture() { return gesture_; }
  VoiceRuntime* runtime() { return runtime_.get(); }

 private:
  MicrophoneInput input_;
  MicrophoneHub hub_;
  MicrophoneHub::Consumer wakeMicrophone_, voiceMicrophone_, scriptMicrophone_;
  KnobGesture gesture_;
  std::optional<Tc002VoiceSpeaker> speaker_;
  std::unique_ptr<VoiceRuntime> runtime_;
  std::optional<VoiceApi> api_;
  std::unique_ptr<wake::WakeListener> wake_;
  std::unique_ptr<VoiceScripting> scripting_;
  AssistantRouter::NoModule noModule_;
  std::optional<AssistantRouter> router_;
};

}
