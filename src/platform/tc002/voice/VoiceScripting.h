#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/script/ScriptExtension.h"
#include "platform/linux/LinuxMemory.h"
#include "platform/posix/Hold.h"
#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/voice/AssistantRouter.h"
#include "platform/tc002/voice/ClipStore.h"
#include "platform/tc002/voice/VoiceRecorder.h"
#include "platform/tc002/voice/VoiceSpeaker.h"
#include "platform/tc002/voice/VoiceStatus.h"
#include "platform/tc002/voice/VoiceTransfers.h"

namespace awtrix::tc002::voice {

class VoiceScripting : public script::ScriptExtension, public AssistantRouter::Module {
 public:
  struct Services {
    MicrophonePort microphone;
    std::function<void(MicrophoneMark)> startFrom;
    std::function<bool(MicrophoneMark)> live;
    VoiceSpeaker* speaker = nullptr;
    std::function<bool()> assistantBusy;
    std::function<bool(const std::string&)> visible;
    std::function<int64_t()> clock;
    std::string clipDirectory;
    MemoryRoom::Reader availableMemory;
    std::string voiceActivityModel;
    net::FillRequest fillRequest;
  };
  static constexpr int kPerTick = 32;

  explicit VoiceScripting(Services services);
  VoiceScripting(const VoiceScripting&) = delete;
  VoiceScripting& operator=(const VoiceScripting&) = delete;

  std::vector<std::string> modules() const override { return {Module::kName}; }
  std::vector<std::string> hooks() const override { return {kHook}; }
  void install(script::ScriptExtensionHost& host) override;
  void tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) override;
  void forget(script::ScriptExtensionHost& host, const std::string& app) override;
  void hidden(script::ScriptExtensionHost& host, const std::string& app) override;
  bool holds(const std::string& app) const override;

  void receive(const StreamEvent& event, int64_t now) { recorder_.receive(event, now); }
  void update(int64_t now);
  void shutdown() { transfers_.stop(); }
  bool busy() const override { return static_cast<bool>(voiceChannel_); }
  bool starting() const override { return recorder_.starting(); }
  void trigger(const std::string& app, const std::string& name, MicrophoneMark mark) override;
  bool triggered() const override { return trigger_ && !trigger_->delivered; }
  void dropTrigger() override;
  void beginFrame(script::ScriptExtensionHost& host, const std::string& app) override;

  int record(const std::string& app, const wake::EndpointSettings& settings, bool fromTrigger) {
    return recorder_.record(app, settings, fromTrigger);
  }
  void stop(const std::string& app) { recorder_.stop(app); }
  void cancel(const std::string& app);
  int play(const std::string& app, ClipId clip);
  void release(const std::string& app, ClipId clip) { clips_.releaseReady(app, clip); }
  const char* state(const std::string& app) const { return recorder_.state(app); }
  uint8_t level(const std::string& app) const { return recorder_.level(app); }
  int send(const std::string& app, ClipId clip, const VoiceTransfers::Options& options);
  int fetch(const std::string& app, std::string body, const VoiceTransfers::Options& options);

 private:
  struct Trigger {
    std::string app, name;
    MicrophoneMark mark;
    bool drawn = false, delivered = false;
  };
  void queue(VoiceEvent event);
  void cancelEvent(VoiceEvent& event);
  MicrophoneMark takeMark(const std::string& app);
  void dropTriggerOf(const std::string& app);
  void deliverTrigger(script::ScriptExtensionHost& host, const RenderCtx* ctx);
  bool voiceChannelWanted() const;
  bool musicWanted() const;
  bool claimVoiceChannel();
  void releaseVoiceChannel();
  void updateMusic();
  void settleSpeaker();

  static int nRecord(bvm* vm);
  static int nStop(bvm* vm);
  static int nCancel(bvm* vm);
  static int nPlay(bvm* vm);
  static int nRelease(bvm* vm);
  static int nLevel(bvm* vm);
  static int nState(bvm* vm);
  static int nSend(bvm* vm);
  static int nFetch(bvm* vm);

  Services services_;
  ClipStore clips_;
  VoiceTransfers transfers_;
  VoiceRecorder recorder_;
  std::deque<VoiceEvent> events_;
  std::optional<Trigger> trigger_;
  posix::Hold voiceChannel_, music_;
};

}
