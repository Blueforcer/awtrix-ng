#include "platform/tc002/voice/VoiceStack.h"

#include <utility>

#include "platform/linux/LinuxMemory.h"
#include "platform/posix/WorkerThread.h"
#include "platform/tc002/contract/tc002_layout.h"
#include "platform/tc002/voice/ClipStore.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"

namespace awtrix::tc002::voice {

VoiceStack::VoiceStack(const Setup& setup)
    : input_([send = setup.send, supervised = setup.supervised](std::string request) {
        return supervised && send(std::move(request));
      }),
      hub_(setup.send, [this](const StreamEvent& block, int64_t now) {
        input_.streamed(block.samples.data(), block.samples.size(), block.firstSample, now);
      }) {
  if (!setup.supervised) return;
  ClipStore::removeLeftovers(TC002_VOLATILE_DIR);
  if (!setup.speaker) return;
  if (!setup.wakeWordModel.empty()) {
    std::string error;
    auto microphone = hub_.attach([this](const StreamEvent& e, int64_t now) {
      if (wake_) wake_->receive(e, now);
    });
    wake_ = wake::WakeListener::open(setup.wakeWordModel, microphone.port(), std::make_unique<posix::WorkerThread>(),
                                     error);
    if (wake_) wakeMicrophone_ = std::move(microphone);
    else logf("wake word: %s", error.c_str());
  }
  speaker_.emplace(*setup.speaker);
  voiceMicrophone_ = hub_.attach([this](const StreamEvent& e, int64_t now) {
    if (runtime_) runtime_->receive(e, now);
  });
  runtime_ = std::make_unique<VoiceRuntime>(setup.configDirectory, *speaker_, voiceMicrophone_.port());
  api_.emplace(*runtime_, wake_ != nullptr,
               VoiceApi::Hooks{[this] { return router_->apps(); },
                               [this] { return start(VoiceTrigger::Api); },
                               [this] { return router_->listening(); },
                               [this] { return router_->appProblem(); }});
}

VoiceScripting* VoiceStack::composeScripting(ScriptSetup setup) {
  if (!runtime_) return nullptr;
  scriptMicrophone_ = hub_.attach([this](const StreamEvent& e, int64_t now) {
    if (scripting_) scripting_->receive(e, now);
  });
  scripting_ = std::make_unique<VoiceScripting>(VoiceScripting::Services{
      scriptMicrophone_.port(),
      [this](MicrophoneMark mark) { scriptMicrophone_.startFrom(mark); },
      [this](MicrophoneMark mark) { return hub_.live(mark); },
      &*speaker_, [this] { return runtime_->busy(); }, std::move(setup.visible),
      [] { return monotonicMs(); }, TC002_VOLATILE_DIR, readMemAvailable, std::move(setup.voiceActivityModel),
      std::move(setup.fillRequest)});
  return scripting_.get();
}

void VoiceStack::composeRouter(AssistantRouter::Ports ports) {
  if (!runtime_) return;
  AssistantRouter::Module& module = scripting_ ? static_cast<AssistantRouter::Module&>(*scripting_) : noModule_;
  router_.emplace(std::move(ports),
                  AssistantRouter::Parts{*runtime_, module, gesture_, hub_, wake_.get(), wakeMicrophone_});
}

void VoiceStack::receive(const SupervisorMessage& message, int64_t now) {
  if (message.type == MessageType::MicrophonePcm) input_.receive(message.microphonePcm, now);
  if (message.type == MessageType::MicrophoneStreamEvent) hub_.receive(message.streamEvent, now);
}

void VoiceStack::tick(int64_t now, bool allowed, bool knobAllowed) {
  hub_.tick(now);
  if (!knobAllowed) gesture_.cancel();
  if (runtime_) {
    runtime_->tick(now, allowed);
    if (scripting_) scripting_->update(now);
    router_->tick(now, knobAllowed);
  }
  input_.setStreaming(hub_.active());
}

void VoiceStack::draw(Canvas& canvas, int64_t now) {
  if (runtime_) runtime_->draw(canvas, now);
}

bool VoiceStack::handle(const httplib::Request& request, httplib::Response& response) {
  return api_ && api_->handle(request, response);
}

bool VoiceStack::start(VoiceTrigger trigger) { return router_->start(trigger, monotonicMs()); }

void VoiceStack::stop() {
  if (runtime_) runtime_->cancel();
  if (wake_) wake_->cancel();
  hub_.close();
}

void VoiceStack::stopTransfers() {
  if (scripting_) scripting_->shutdown();
}

bool VoiceStack::eraseConfig() { return !runtime_ || runtime_->eraseConfig(); }

}
