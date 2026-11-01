#include "platform/tc002/voice/AssistantRouter.h"

#include <utility>

#include "core/api/JsonWriter.h"
#include "core/script/ScriptMeta.h"
#include "platform/tc002/wake/WakeListener.h"
#include "system/Log.h"

namespace awtrix::tc002::voice {
namespace {

const char* triggerName(VoiceTrigger trigger) {
  switch (trigger) {
    case VoiceTrigger::Wake: return "wake";
    case VoiceTrigger::Knob: return "knob";
    case VoiceTrigger::Mqtt: return "mqtt";
    case VoiceTrigger::Api: return "api";
  }
  return "";
}

bool answersVoice(const script::ScriptMeta& meta) {
  return !meta.module && !meta.headless && script::namesNeed(meta.needs, AssistantRouter::Module::kName);
}

}

AssistantRouter::AssistantRouter(Ports ports, const Parts& parts) : ports_(std::move(ports)), parts_(parts) {}

std::vector<std::string> AssistantRouter::apps() const {
  std::vector<std::string> out;
  if (!parts_.module) return out;
  for (std::string& name : ports_.scripts()) {
    const script::ScriptMeta* const meta = ports_.meta(name);
    if (meta && answersVoice(*meta)) out.push_back(std::move(name));
  }
  return out;
}

bool AssistantRouter::start(VoiceTrigger trigger, int64_t now) {
  return startFrom(trigger, parts_.microphone.mark(), now);
}

void AssistantRouter::tick(int64_t now, bool knob) {
  tickInteraction(now);
  const bool ready = this->ready();
  if (parts_.gesture.tick(now, knob && ready) == KnobGesture::Action::Start)
    startFrom(VoiceTrigger::Knob, parts_.microphone.mark(), now);
  parts_.microphone.hold(holdsMicrophone());
  if (!parts_.wake) return;
  using Listen = wake::WakeListener::Listen;
  const bool subscribed = ports_.allowed() && parts_.voice.wakeWord() && present();
  const Listen listen = !subscribed ? Listen::Off : ready ? Listen::On : Listen::Muted;
  if (const auto heard = parts_.wake->tick(now, listen))
    startFrom(VoiceTrigger::Wake, parts_.wakeMicrophone.markOf(heard->sample), now);
}

bool AssistantRouter::present() const {
  return parts_.voice.homeAssistant() ? parts_.voice.ready() : answers(parts_.voice.assistantApp());
}

bool AssistantRouter::ready() const {
  return parts_.voice.homeAssistant() ? homeAssistantReady() : startable(parts_.voice.assistantApp());
}

bool AssistantRouter::homeAssistantReady() const {
  return parts_.voice.ready() && !(parts_.module && parts_.module->busy());
}

bool AssistantRouter::answers(const std::string& app) const {
  if (!parts_.module) return false;
  const script::ScriptMeta* const meta = ports_.meta(app);
  return meta && answersVoice(*meta) && ports_.runnable(app);
}

bool AssistantRouter::startable(const std::string& app) const {
  const Module* const module = parts_.module;
  return answers(app) && !module->busy() && !module->triggered() && !module->holds(app) && !parts_.voice.busy();
}

bool AssistantRouter::startFrom(VoiceTrigger trigger, MicrophoneMark mark, int64_t now) {
  if (!ports_.allowed()) return false;
  if (parts_.voice.homeAssistant()) return startHomeAssistant(now);
  return startApp(parts_.voice.assistantApp(), trigger, mark, now);
}

bool AssistantRouter::startHomeAssistant(int64_t now) {
  if (!homeAssistantReady()) return false;
  ports_.dismissQuickSettings();
  return parts_.voice.begin(now);
}

bool AssistantRouter::startApp(const std::string& app, VoiceTrigger trigger, MicrophoneMark mark, int64_t now) {
  if (!startable(app)) {
    logf("voice: assistant app %s unavailable", app.c_str());
    return false;
  }
  std::string json;
  api::JsonWriter(json).beginObject().member("name", app).member("fast", true).endObject();
  DispatchDetail detail;
  if (ports_.switchApp(json, detail) != DispatchResult::Ok) {
    logf("voice: assistant app %s did not open: %s", app.c_str(), detail.message.c_str());
    return false;
  }
  ports_.dismissQuickSettings();
  const bool panelOff = ports_.panelOff();
  if (panelOff) ports_.setPanelPower(true);
  parts_.module->trigger(app, triggerName(trigger), mark);
  interaction_ = Interaction{app, now, now, interaction_ ? interaction_->panelOff : panelOff, ports_.holdMusic()};
  return true;
}

void AssistantRouter::tickInteraction(int64_t now) {
  if (!interaction_) return;
  Interaction& interaction = *interaction_;
  if (parts_.module->triggered()) {
    if (now - interaction.startedAt < kShowTimeoutMs) return;
    logf("voice: assistant app %s did not show", interaction.app.c_str());
  } else if (ports_.shown(interaction.app)) {
    if (parts_.module->holds(interaction.app)) interaction.activeAt = now;
    if (now - interaction.activeAt < kAssistantIdleMs) return;
  }
  parts_.module->dropTrigger();
  if (interaction.panelOff && !ports_.panelOff()) ports_.setPanelPower(false);
  interaction_.reset();
}

bool AssistantRouter::holdsMicrophone() const {
  const Module* const module = parts_.module;
  return parts_.voice.starting() || (module && (module->starting() || module->triggered()));
}

}
