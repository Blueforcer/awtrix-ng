#include "platform/tc002/voice/VoiceRuntime.h"

#include <unistd.h>

#include "platform/posix/Files.h"
#include "platform/tc002/voice/StreamClock.h"

namespace awtrix::tc002::voice {
VoiceRuntime::VoiceRuntime(std::string directory, VoiceSpeaker& speaker, MicrophonePort microphone)
    : config_(std::move(directory)),
      speaker_(speaker),
      download_("/tmp", "awtrix-voice", {2 * 1024 * 1024, 15000, false, nullptr}),
      session_({[this](std::string bytes, bool binary) {
                  return socket_.send(std::move(bytes), binary);
                },
                [this] { socket_.cancel(); },
                std::move(microphone),
                [this](const std::string& target) {
                  downloadGeneration_ =
                      download_.start(config_.get().url, target);
                  return true;
                },
                [this] { stopSpeaker(); }}) {
  loaded_ = config_.load();
}
VoiceRuntime::~VoiceRuntime() { cancel(); }
void VoiceRuntime::receive(const StreamEvent& event, int64_t now) {
  if (event.kind != StreamEvent::Kind::Audio) {
    session_.receive(event, now);
    return;
  }
  auto localized = event;
  if (!localizeCaptureTime(event.hostAtMs, posix::monotonicMs(), now,
                           localized.hostAtMs))
    localized.error = "Invalid or stale microphone timestamp";
  session_.receive(localized, now);
  if (localized.error.empty() && event.epoch == session_.captureEpoch() &&
      session_.state() == AssistSession::State::Listening)
    overlay_.audio(event.samples.data(), event.samples.size(), now);
}
void VoiceRuntime::removePlaybackFile() {
  if (!playbackPath_.empty()) ::unlink(playbackPath_.c_str());
  playbackPath_.clear();
}
void VoiceRuntime::stopSpeaker() {
  download_.cancel();
  if (playing_) speaker_.stop();
  playing_ = false;
  removePlaybackFile();
}
void VoiceRuntime::cancel() {
  errorFeedback_ = false;
  pendingStart_ = false;
  session_.cancel();
  voiceChannel_.reset();
}
bool VoiceRuntime::updateConfig(std::string_view json, ConfigStore::Rejection& rejection) {
  const Config before = config_.get();
  if (!config_.update(json, rejection)) return false;
  if (!loaded_ || !onlyWakeChanged(before, config_.get())) {
    cancel();
    reconnectAt_ = 0;
  }
  loaded_ = true;
  return true;
}
bool VoiceRuntime::begin(int64_t now) {
  if (!session_.ready() || !speaker_.available()) return false;
  if (!voiceChannel_) voiceChannel_ = speaker_.claimVoiceChannel();
  if (!voiceChannel_) return false;
  pendingStart_ = true;
  startAt_ = now;
  overlay_.reset(now);
  return true;
}
void VoiceRuntime::endInput(int64_t now) {
  if (pendingStart_) {
    pendingStart_ = false;
    voiceChannel_.reset();
  } else
    session_.endInput(now);
}
void VoiceRuntime::tick(int64_t now, bool allowed) {
  if (now >= feedbackUntil_) errorFeedback_ = false;
  const auto& c = config_.get();
  if (!loaded_ || !allowed || !homeAssistant() || c.url.empty() || c.token.empty()) {
    if (pendingStart_ || session_.state() != AssistSession::State::Offline)
      cancel();
    reconnectAt_ = now + 500;
    return;
  }
  connectIfDue(c, now);
  pumpSocket(now);
  session_.tick(now);
  startPending(now);
  pollDownload(now);
  finishPlayback(now);
  releaseVoiceChannel(now);
}
void VoiceRuntime::connectIfDue(const Config& config, int64_t now) {
  if ((session_.state() != AssistSession::State::Offline &&
       session_.state() != AssistSession::State::Error) ||
      now < reconnectAt_)
    return;
  WebSocket::Endpoint endpoint;
  if (!parseOrigin(config.url, endpoint)) return;
  session_.connecting(config, now);
  generation_ = socket_.connect(endpoint);
  reconnectAt_ = now + 10000;
}
void VoiceRuntime::pumpSocket(int64_t now) {
  for (auto& event : socket_.poll()) {
    if (event.generation != generation_) continue;
    if (event.kind == WebSocket::Event::Text)
      session_.text(event.text, now);
    else if (event.kind != WebSocket::Event::Open)
      session_.lost(now);
  }
}
void VoiceRuntime::startPending(int64_t now) {
  if (!pendingStart_) return;
  if (!session_.ready() || !speaker_.available() || now - startAt_ >= 1000)
    endInput(now);
  else if (speaker_.quiet()) {
    pendingStart_ = false;
    session_.begin(now);
  }
}
void VoiceRuntime::pollDownload(int64_t now) {
  net::FileDownload::Result result;
  if (!download_.poll(result)) return;
  if (result.generation == downloadGeneration_ &&
      session_.state() == AssistSession::State::Speaking) {
    playbackPath_ = std::move(result.path);
    playing_ = result.success() && speaker_.play(playbackPath_);
    if (!playing_) session_.playbackDone(false, now);
  } else if (!result.path.empty())
    ::unlink(result.path.c_str());
}
void VoiceRuntime::finishPlayback(int64_t now) {
  if (!playing_ || !speaker_.finished()) return;
  playing_ = false;
  session_.playbackDone(speaker_.heard(), now);
  removePlaybackFile();
}
void VoiceRuntime::releaseVoiceChannel(int64_t now) {
  if (!voiceChannel_ || pendingStart_ || session_.busy()) return;
  if (session_.state() == AssistSession::State::Error) {
    errorFeedback_ = true;
    feedbackUntil_ = now + 1500;
  }
  voiceChannel_.reset();
}
void VoiceRuntime::draw(Canvas& canvas, int64_t now) {
  if (!busy()) return;
  const auto state = session_.state();
  using Phase = VoiceOverlay::Phase;
  overlay_.draw(canvas,
                errorFeedback_                              ? Phase::Error
                : pendingStart_                             ? Phase::Starting
                : state == AssistSession::State::Listening  ? Phase::Listening
                : state == AssistSession::State::Speaking   ? Phase::Speaking
                : state == AssistSession::State::Processing ? Phase::Processing
                                                            : Phase::Starting,
                now);
}
}  // namespace awtrix::tc002::voice
