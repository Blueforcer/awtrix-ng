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
#include "platform/tc002/voice/VoiceCapture.h"
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
  };
  static constexpr int kPerTick = 32;

  explicit VoiceScripting(Services services);
  VoiceScripting(const VoiceScripting&) = delete;
  VoiceScripting& operator=(const VoiceScripting&) = delete;

  std::vector<std::string> modules() const override { return {Module::kName}; }
  std::vector<std::string> hooks() const override { return {"on_voice"}; }
  void install(script::ScriptExtensionHost& host) override;
  void tick(script::ScriptExtensionHost& host, const RenderCtx* ctx) override;
  void forget(script::ScriptExtensionHost& host, const std::string& app) override;
  void hidden(script::ScriptExtensionHost& host, const std::string& app) override;
  bool holds(const std::string& app) const override;

  void receive(const StreamEvent& event, int64_t now) { capture_.receive(event, now); }
  void update(int64_t now);
  bool busy() const override { return static_cast<bool>(voiceChannel_); }
  bool starting() const override { return pendingCapture_.has_value() || capture_.starting(); }
  void trigger(const std::string& app, const std::string& name, MicrophoneMark mark) override;
  bool triggered() const override { return trigger_ && !trigger_->delivered; }
  void dropTrigger() override;
  void beginFrame(script::ScriptExtensionHost& host, const std::string& app) override;

  int record(const std::string& app, const wake::EndpointSettings& settings, bool fromTrigger);
  void stop(const std::string& app);
  void cancel(const std::string& app);
  int play(const std::string& app, ClipId clip);
  void release(const std::string& app, ClipId clip) { clips_.releaseReady(app, clip); }
  const char* state(const std::string& app) const;
  uint8_t level(const std::string& app) const;
  int send(const std::string& app, ClipId clip, const VoiceTransfers::SendOptions& options);
  int fetch(const std::string& app, const std::string& url, const std::string& method, std::string body,
            const std::string& headers);

 private:
  enum class EventKind { Record, Play, Send, Fetch };
  struct Event {
    std::string app;
    EventKind kind;
    int number = 0, status = 0;
    ClipId clip = 0;
    uint32_t ms = 0;
    std::optional<std::string> text;
  };
  struct PendingCapture {
    wake::EndpointSettings settings;
    int64_t since = 0;
  };
  struct Trigger {
    std::string app, name;
    MicrophoneMark mark;
    bool drawn = false, delivered = false;
  };
  static Event outcome(std::string app, EventKind kind, int number, int status);
  static Event delivery(std::string app, EventKind kind, int number, ClipId clip, uint32_t ms, std::string format);
  static const char* eventName(EventKind kind);
  static std::string eventNumbers(const Event& event);
  static bool transferEvent(const Event& event);
  static Event eventOf(VoiceTransfers::Done done);
  void queue(Event event);
  void cancelEvent(Event& event);
  MicrophoneMark takeMark(const std::string& app);
  void dropTriggerOf(const std::string& app);
  void startCapture(const wake::EndpointSettings& settings, MicrophoneMark from, int64_t now);
  void deliverRecording(uint32_t ms);
  void endRecording(int status);
  void clearRecording();
  void endPlayback(bool heard);
  void deliverTrigger(script::ScriptExtensionHost& host, const RenderCtx* ctx);
  void stopAudio(const std::string& app);
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
  VoiceCapture capture_;
  std::deque<Event> events_;
  std::string recordApp_, playApp_;
  ClipId recordClip_ = 0;
  std::optional<PendingCapture> pendingCapture_;
  std::optional<Trigger> trigger_;
  int recordNumber_ = 0, playNumber_ = 0;
  bool playing_ = false;
  posix::Hold voiceChannel_, music_;
};

}
