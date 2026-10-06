#include "platform/linux/LinuxRuntime.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <unistd.h>

#include "core/FrameClock.h"
#include "media/AwtrixFontAdapter.h"
#include "persistence/NvsSettings.h"
#include "platform/linux/host/HostPersistence.h"
#include "platform/linux/host/HostStore.h"
#include "platform/tc002/contract/RuntimeContract.h"
#include "system/Log.h"

namespace awtrix {

void LinuxRuntime::run() {
  nextFrame_ = monotonicMs();
  if (tc002_) tc002_->setTiming(performance_->panelTiming());
  performance_->start();
  while (!stopping_ && runFrame()) {
  }
}

bool LinuxRuntime::runFrame() {
  LinuxPerformanceReport& performance = *performance_;
  const int64_t frameStartedUs = performance.beginFrame();
  const int64_t now = monotonicMs();
  app_->beginFrame(now);
  if (!pollSupervisor(now)) return false;
  if (bleService_) {
    const int controller = bleService_->takeControllerRequest();
    if (controller >= 0) supervision_->requestBluetooth(controller == 1);
    bleService_->drainLog([](const std::string& line) { logf("%s", line.c_str()); });
  }
  if (iphoneBridge_) iphoneBridge_->tick(now);
  if (!pollInput(now)) return false;
  tickVoice(now);
  tickNetwork(now);
  const bool updating = update_ && update_->ownsPanel();
  const bool booting = !updating && intro_ && intro_->draw(app_->canvas(), awtrixFont(), now);
  if (intro_ && !booting && !updating) {
    logf("boot: intro %s, apps start %lld ms after it%s%s",
         intro_->soundSynchronized() ? "in sync with the sound" : "without the sound",
         static_cast<long long>(now - intro_->introStartMs()), intro_->shownAddress().empty() ? "" : " and ",
         intro_->shownAddress().c_str());
    intro_.reset();
    bootSoundClock_.reset();
  }
  const bool setup = o_.supervised && supervision_->accessPoint();
  if (!booting && !updating && !setup) app_->tick(now, scripts_);
  store_.tick(now);
  const int64_t renderStartedUs = performance.now();
  if (updating) update_->draw(app_->canvas(), awtrixFont());
  else if (!booting && setup) supervision_->drawProvisioning(app_->canvas(), awtrixFont(), now);
  else if (!booting) {
    app_->render(now);
    quickSettings_->draw(app_->canvas(), awtrixFont(), now);
    if (voice_) voice_->draw(app_->canvas(), awtrixFont(), now);
  }
  const int64_t renderEndedUs = performance.now();
  board().show(app_->canvas());
  const int64_t displayEndedUs = performance.now();
  if (!board().displayReady()) {
    std::fprintf(stderr, "Display output failed: %s\n", tc002_ ? tc002_->error().c_str() : board().name());
    displayFailed_ = true;
    return false;
  }
  if (update_) {
    const std::string ready = update_->poll(now, updating);
    if (!ready.empty() && !link_.send(ready)) logf("update: the supervisor channel refused the package");
  }
  if (stopping_ || !reportReady()) return false;
  saveDue(now);
  nextFrame_ += kFramePeriodMs;
  const int64_t workEndedUs = performance.now();
  const int64_t deadlineUs = nextFrame_ * 1000;
  const auto delay = nextFrame_ - monotonicMs();
  if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
  else nextFrame_ = monotonicMs();
  performance.record({frameStartedUs, renderStartedUs, renderEndedUs, displayEndedUs,
                      workEndedUs, performance.now(), deadlineUs});
  return true;
}

bool LinuxRuntime::pollSupervisor(int64_t now) {
  const bool open = link_.poll([&](const tc002::SupervisorMessage& message) {
    if (update_ && message.type == tc002::MessageType::Hello) update_->applyHello(message);
    supervision_->apply(message);
    if (message.type == tc002::MessageType::MicrophonePcm) microphone_->receive(message.microphonePcm, now);
    if (voice_ && message.type == tc002::MessageType::MicrophoneStreamEvent) voice_->receive(message.streamEvent, now);
  });
  if (open) return true;
  std::fputs("Supervisor channel closed; stopping\n", stderr);
  supervisorClosed_ = true;
  return false;
}

bool LinuxRuntime::pollInput(int64_t now) {
  if (!o_.physicalInput) return true;
  const bool panelBusy = intro_ || (update_ && update_->ownsPanel());
  if (knobRouter_->poll(input_, now, panelBusy, scripts_ != nullptr)) return true;
  std::fprintf(stderr, "Input failed: %s\n", input_.error().c_str());
  inputFailed_ = true;
  return false;
}

void LinuxRuntime::tickVoice(int64_t now) {
  const bool voiceAllowed = voiceAllowedNow();
  const bool knobVoice = voiceAllowed && !engine().state().settings().blockNavigation;
  if (!knobVoice) knobGesture_.cancel();
  if (!voice_) return;
  voice_->tick(now, voiceAllowed);
  if (knobGesture_.tick(now, knobVoice && voice_->ready()) == tc002::voice::KnobGesture::Action::Start)
    startVoice(now);
}

void LinuxRuntime::tickNetwork(int64_t now) {
  const bool supervised = o_.supervised, lan = o_.administration.lan;
  if (supervised && lan) provisioning_->setActive(!supervision_->networkConnected());
  deviceFacts_->tick(now, !supervised);
  if (lan && deviceFacts_->hostname() != discoveryName_) {
    discoveryName_ = deviceFacts_->hostname();
    discovery_.begin(discoveryName_, o_.port);
  }
  discovery_.tick();
  if (mqttTls_ && brokerTrust_) {
    std::string fingerprint;
    if (brokerTrust_->peer()->takeNewPending(fingerprint))
      logf("mqtt: broker certificate not trusted, SHA-256 %s", fingerprint.c_str());
  }
  mirror_.tick(now, lan && (!supervised || supervision_->networkConnected()));
  http_.tick();
  extensions_.phones.poll();
  if (!mqttAllowed_) return;
  mqtt_.tick();
  if (o_.physicalInput) {
    const auto& rt = engine().state().runtime();
    knobEvents_.tick(rt.mqtt, rt.knob, rt.knobTurns,
        [this](const char* topic, const std::string& body, bool retained) { mqtt_.publish(topic, body, retained); });
  }
}

bool LinuxRuntime::reportReady() {
  if (!o_.supervised || readySent_) return true;
  if (!link_.send(tc002::encodeReady({o_.boardType, o_.width, o_.height, o_.physicalInput}))) {
    std::fputs("Supervisor channel refused the ready report\n", stderr);
    readinessFailed_ = true;
    return false;
  }
  readySent_ = true;
  return true;
}

void LinuxRuntime::saveDue(int64_t now) {
  if (!settingsSaver_.due(now)) return;
  if (dirty_) nvs::saveSettings(engine().state().settings());
  host::persistence::flushPending();
  dirty_ = host::persistence::pending(host::persistence::Document::Settings);
  settingsSaver_.saved(now);
}

bool LinuxRuntime::persist() {
  store_.flush();
  if (dirty_) nvs::saveSettings(engine().state().settings());
  const bool flushed = host::persistence::flushPending() && !store_.hasPending();
  dirty_ = host::persistence::pending(host::persistence::Document::Settings);
  return flushed;
}

int LinuxRuntime::stop() {
  performance_->stop();
  if (voice_) voice_->cancel();
  const bool resetAll = system_.resetAll;
  if (!resetAll) persist();
  http_.stop();
  scriptHttp_.stop();
  remoteImages_->stop();
  webhook_->stop();
  coverFetch_.stop();
  if (bleService_) {
    bleService_->stop();
    supervision_->requestBluetooth(false);
  }
  if (iphoneBridge_ && !resetAll) iphoneBridge_->flush();
  bool saved = true;
  if (!resetAll) {
    saved = persist();
    if (!saved) std::fputs("Shutdown could not persist all application state\n", stderr);
  }
  std::error_code error;
  if (system_.resetUserSettings && !resetAll)
    std::filesystem::remove(std::filesystem::u8path(host::hostPath("/settings.json")), error);
  if (resetAll && !eraseAll(error)) return 1;
  if (o_.supervised && !supervisorClosed_) {
    if (resetAll && !supervision_->factoryReset())
      std::fputs("Supervisor channel refused the factory reset\n", stderr);
    if (system_.rebootRequested && !supervision_->reboot())
      std::fputs("Supervisor channel refused the reboot request\n", stderr);
    if (!link_.flush(1000)) std::fputs("Supervisor channel did not take all pending messages\n", stderr);
  }
  if (update_ && update_->ownsPanel()) static_cast<void>(selectedBoard_.release());
  ::close(lock_);
  const bool clean = !error && saved && !displayFailed_ && !inputFailed_ && !readinessFailed_;
  const bool reported = performance_->finish(clean);
  return clean && reported ? 0 : 1;
}

bool LinuxRuntime::eraseAll(std::error_code& error) {
  if (voice_ && !voice_->eraseConfig()) {
    std::fputs("Factory reset could not erase private voice credentials\n", stderr);
    return false;
  }
  oauthService_->stop();
  if (!oauthService_->eraseAll()) {
    std::fputs("Factory reset could not erase script sign-ins\n", stderr);
    return false;
  }
  // Only the explicitly selected application data directory is erased. Keep
  // the held lock inode so a second process cannot enter during the reset.
  const auto root = std::filesystem::u8path(host::dataDir());
  std::filesystem::directory_iterator entry(root, error), end;
  while (!error && entry != end) {
    if (entry->path().filename() != ".lock")
      std::filesystem::remove_all(entry->path(), error); // does not follow symlinks
    if (!error) entry.increment(error);
  }
  return true;
}

}
