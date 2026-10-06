#include "VoiceErrors.h"
#include "platform/tc002/voice/VoiceRuntime.h"

#include <unistd.h>

#include "core/api/ApiRouter.h"
#include "core/api/JsonWriter.h"
#include "platform/linux/host/vendor/httplib.h"
#include "platform/posix/Files.h"
#include "platform/tc002/voice/StreamClock.h"

namespace awtrix::tc002::voice {
VoiceRuntime::VoiceRuntime(std::string directory, Tc002Speaker& speaker,
                           MicrophoneInput& microphone,
                           Send send)
    : config_(std::move(directory)),
      speaker_(speaker),
      microphone_(microphone),
      download_("/tmp", "awtrix-voice", {2 * 1024 * 1024, 15000, false, nullptr}),
      session_({[this](std::string bytes, bool binary) {
                  return socket_.send(std::move(bytes), binary);
                },
                [this] { socket_.cancel(); },
                [send](StreamControl control) {
                  return send(encodeStreamControl(control));
                },
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
void VoiceRuntime::stopSpeaker() {
  download_.cancel();
  speaker_.stopVoice();
  playing_ = false;
  if (!playbackPath_.empty()) ::unlink(playbackPath_.c_str());
  playbackPath_.clear();
}
void VoiceRuntime::cancel() {
  errorFeedback_ = false;
  pendingStart_ = false;
  session_.cancel();
  microphone_.setStreaming(false);
  speaker_.setVoiceOwned(false);
  owned_ = false;
}
bool VoiceRuntime::begin(int64_t now) {
  if (!session_.ready() || !speaker_.available()) return false;
  speaker_.setVoiceOwned(true);
  microphone_.setStreaming(true);
  owned_ = true;
  pendingStart_ = true;
  startAt_ = now;
  overlay_.reset(now);
  return true;
}
void VoiceRuntime::endInput(int64_t now) {
  if (pendingStart_) {
    pendingStart_ = false;
    microphone_.setStreaming(false);
    speaker_.setVoiceOwned(false);
    owned_ = false;
  } else
    session_.endInput(now);
}
void VoiceRuntime::tick(int64_t now, bool allowed) {
  if (now >= feedbackUntil_) errorFeedback_ = false;
  const auto& c = config_.get();
  if (!loaded_ || !allowed || !c.enabled) {
    if (pendingStart_ || session_.state() != AssistSession::State::Offline)
      cancel();
    reconnectAt_ = now + 500;
    return;
  }
  if ((session_.state() == AssistSession::State::Offline ||
       session_.state() == AssistSession::State::Error) &&
      now >= reconnectAt_) {
    WebSocket::Endpoint endpoint;
    if (parseOrigin(c.url, endpoint)) {
      session_.connecting(c, now);
      generation_ = socket_.connect(endpoint);
      reconnectAt_ = now + 10000;
    }
  }
  for (auto& event : socket_.poll()) {
    if (event.generation != generation_) continue;
    if (event.kind == WebSocket::Event::Text)
      session_.text(event.text, now);
    else if (event.kind != WebSocket::Event::Open)
      session_.lost(now);
  }
  session_.tick(now);
  if (pendingStart_) {
    if (!session_.ready() || !speaker_.available() || now - startAt_ >= 1000)
      endInput(now);
    else if (speaker_.voiceQuiet()) {
      pendingStart_ = false;
      session_.begin(now);
    }
  }
  net::FileDownload::Result result;
  if (download_.poll(result)) {
    if (result.generation == downloadGeneration_ &&
        session_.state() == AssistSession::State::Speaking) {
      playbackPath_ = std::move(result.path);
      playing_ = result.success() && speaker_.playVoice(playbackPath_);
      if (!playing_) session_.playbackDone(false, now);
    } else if (!result.path.empty())
      ::unlink(result.path.c_str());
  }
  if (playing_ && (!speaker_.available() || speaker_.voiceFinished())) {
    int64_t audible = -1;
    const bool success =
        speaker_.available() && speaker_.bootSoundStart(audible) ==
                                    Tc002AudioSink::SystemStart::Audible;
    playing_ = false;
    session_.playbackDone(success, now);
    if (!playbackPath_.empty()) ::unlink(playbackPath_.c_str());
    playbackPath_.clear();
  }
  if (owned_ && !pendingStart_ && !session_.busy()) {
    if (session_.state() == AssistSession::State::Error) {
      errorFeedback_ = true;
      feedbackUntil_ = now + 1500;
    }
    microphone_.setStreaming(false);
    speaker_.setVoiceOwned(false);
    owned_ = false;
  }
}
bool VoiceRuntime::draw(Canvas& canvas, const GfxFont&, int64_t now) {
  if (!busy()) return false;
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
  return true;
}
namespace {
void sendError(httplib::Response& res, int status, const char* code,
               const std::string& message, const std::string& field = {}) {
  res.status = status;
  res.set_content(api::errorJson(code, message, field), "application/json");
}
}  // namespace
bool VoiceRuntime::handle(const httplib::Request& req, httplib::Response& res) {
  if (req.path != "/api/v1/voice") return false;
  res.set_header("Cache-Control", "no-store");
  if (req.method == "GET") {
    std::string json;
    api::JsonWriter(json)
        .beginObject()
        .key("config")
        .raw(config_.publicJson())
        .member("state", AssistSession::name(session_.state()))
        .member("error",
                !loaded_ ? errors::configUnavailable
                : session_.state() == AssistSession::State::Ready &&
                        !session_.microphoneAvailable()
                    ? errors::microphoneUpdate
                    : session_.error())
        .key("pipelines")
        .raw(session_.pipelines())
        .endObject();
    res.set_content(json, "application/json");
  } else if (req.method == "POST") {
    if (req.get_header_value("X-Awtrix-Voice") != "1" ||
        !api::sameOrigin(req.get_header_value("Origin"), req.get_header_value("Host"),
                          req.get_header_value_count("Origin"), req.get_header_value_count("Host"), true)) {
      sendError(res, 403, "forbiddenOrigin",
                "only from this device's web page");
      return true;
    }
    ConfigStore::Rejection rejection;
    if (!config_.update(req.body, rejection)) {
      if (rejection.stored)
        sendError(res, 422, "validationFailed", rejection.message, rejection.field);
      else
        sendError(res, 500, "internalError", rejection.message);
    } else {
      cancel();
      loaded_ = true;
      reconnectAt_ = 0;
      res.set_content("{\"ok\":true}", "application/json");
    }
  } else {
    sendError(res, 405, "methodNotAllowed", "allowed: GET, POST");
  }
  return true;
}
}  // namespace awtrix::tc002::voice
