#include "VoiceErrors.h"
#include "platform/tc002/voice/AssistSession.h"

#include <climits>

#include "core/api/JsonReader.h"
#include "core/api/JsonWriter.h"

namespace awtrix::tc002::voice {
namespace {
long long number(const api::JsonReader& r, const char* key) {
  long long out = -1;
  api::memberValue(r, key).asLong(out);
  return out;
}
bool succeeded(const api::JsonReader& r) {
  bool success = false;
  api::memberValue(r, "success").asBool(success);
  return success;
}
}  // namespace
const char* AssistSession::name(State state) {
  switch (state) {
    case State::Offline:
      return "offline";
    case State::Connecting:
      return "connecting";
    case State::Ready:
      return "ready";
    case State::Starting:
      return "starting";
    case State::Listening:
      return "listening";
    case State::Processing:
      return "processing";
    case State::Speaking:
      return "speaking";
    case State::Error:
      return "error";
  }
  return "error";
}
bool AssistSession::busy() const {
  return state_ == State::Starting || state_ == State::Listening ||
         state_ == State::Processing || state_ == State::Speaking;
}
bool AssistSession::send(std::string json) {
  return ports_.send(std::move(json), false);
}
void AssistSession::connecting(const Config& config, int64_t now) {
  drop();
  config_ = config;
  state_ = State::Connecting;
  deadline_ = now + kConnectMs;
  supported_ = false;
  probeAt_ = now;
  microphone_.probe();
}
void AssistSession::drop() {
  microphone_.cancel();
  ports_.stopSpeaker();
  ports_.disconnect();
  state_ = State::Offline;
}
void AssistSession::cancel() {
  drop();
  error_.clear();
  lastError_.clear();
}
void AssistSession::fail(const char* code, int64_t now) {
  if (busy()) {
    lastError_ = code;
    lastErrorUntil_ = now + kLastErrorMs;
  } else {
    error_ = code;
  }
  drop();
  state_ = State::Error;
  deadline_ = now + kRetryMs;
}
void AssistSession::lost(int64_t now) {
  fail(errors::connectionLost, now);
}
bool AssistSession::begin(int64_t now) {
  if (!ready()) return false;
  if (request_ > INT_MAX - 2) {
    fail(errors::connectionLost, now);
    return false;
  }
  lastError_.clear();
  handler_ = -1;
  stt_ = recognitionEnded_ = runEnded_ = playbackEnded_ = false;
  microphone_.cancel();
  run_ = ++request_;
  std::string json;
  api::JsonWriter w(json);
  w.beginObject()
      .member("id", run_)
      .member("type", "assist_pipeline/run")
      .member("start_stage", "stt")
      .member("end_stage", "tts");
  if (!config_.pipeline.empty()) w.member("pipeline", config_.pipeline);
  if (!config_.device.empty()) w.member("device_id", config_.device);
  w.key("input")
      .beginObject()
      .member("sample_rate", 16000)
      .endObject()
      .endObject();
  state_ = State::Starting;
  deadline_ = now + kStartMs;
  began_ = now;
  if (!send(std::move(json))) {
    fail(errors::haError, now);
    return false;
  }
  return true;
}
void AssistSession::stopCapture(int64_t now) {
  if (!microphone_.capturing()) return;
  microphone_.stop(now);
  deadline_ = now + kCaptureEndMs;
}
void AssistSession::endInput(int64_t now) {
  if (state_ == State::Starting || state_ == State::Listening) {
    if (!microphone_.capturing()) {
      cancel();
      return;
    }
    stopCapture(now);
    state_ = State::Processing;
  }
}
bool AssistSession::audio(const StreamEvent& e, int64_t now) {
  if (now < e.hostAtMs || now - e.hostAtMs > 250) return false;
  std::string bytes(1, static_cast<char>(handler_));
  bytes.reserve(1 + e.samples.size() * 2);
  for (int16_t sample : e.samples) {
    const uint16_t value = static_cast<uint16_t>(sample);
    bytes.push_back(static_cast<char>(value & 255));
    bytes.push_back(static_cast<char>(value >> 8));
  }
  return ports_.send(std::move(bytes), true);
}
void AssistSession::eof(int64_t now) {
  if (recognitionEnded_) return;
  if (!ports_.send(std::string(1, static_cast<char>(handler_)), true)) {
    fail(errors::audioTooSlow, now);
    return;
  }
  state_ = State::Processing;
  deadline_ = now + kAnswerMs;
}
void AssistSession::apply(MicrophoneClient::Report report, int64_t now) {
  using Report = MicrophoneClient::Report;
  switch (report) {
    case Report::None:
    case Report::Audio:
      return;
    case Report::Available:
      supported_ = true;
      return;
    case Report::Unavailable:
      supported_ = false;
      return;
    case Report::Started:
      state_ = State::Listening;
      deadline_ = began_ + kListenMs;
      return;
    case Report::Gap:
    case Report::Ended:
      fail(errors::microphoneFailed, now);
      return;
    case Report::Stopped:
      if (microphone_.started() && microphone_.error().empty())
        eof(now);
      else
        fail(errors::microphoneFailed, now);
      return;
  }
}
void AssistSession::receive(const StreamEvent& e, int64_t now) {
  const MicrophoneClient::Report report = microphone_.receive(e, now);
  apply(report, now);
  if (report != MicrophoneClient::Report::Audio) return;
  if (!e.error.empty())
    fail(errors::microphoneFailed, now);
  else if (!audio(e, now))
    fail(errors::audioTooSlow, now);
}
void AssistSession::becomeReady() {
  state_ = State::Ready;
  deadline_ = 0;
}
bool AssistSession::authenticate(const std::string& type, int64_t now) {
  if (state_ == State::Connecting && type == "auth_required") {
    std::string auth;
    api::JsonWriter(auth)
        .beginObject()
        .member("type", "auth")
        .member("access_token", config_.token)
        .endObject();
    if (!send(std::move(auth))) fail(errors::connectionLost, now);
    return true;
  }
  if (type == "auth_invalid") {
    fail(errors::tokenRejected, now);
    return true;
  }
  if (state_ == State::Connecting && type == "auth_ok") {
    request_ = 0;
    list_ = ++request_;
    std::string query;
    api::JsonWriter(query)
        .beginObject()
        .member("id", list_)
        .member("type", "assist_pipeline/pipeline/list")
        .endObject();
    if (!send(std::move(query))) fail(errors::haError, now);
    return true;
  }
  return false;
}
void AssistSession::pipelinesListed(const api::JsonReader& r, int64_t now) {
  if (!succeeded(r)) {
    fail(errors::haError, now);
    return;
  }
  const auto result = api::memberValue(r, "result");
  const auto pipelines = api::memberValue(result, "pipelines");
  if (!pipelines.isArray()) {
    fail(errors::haError, now);
    return;
  }
  pipelines_ = std::string(pipelines.valueText());
  auto entries = pipelines;
  entries.enterArray();
  bool usable = false;
  const std::string wanted = config_.pipeline.empty()
                                 ? api::memberText(result, "preferred_pipeline")
                                 : config_.pipeline;
  while (entries.nextElement()) {
    if (api::memberText(entries, "id") == wanted &&
        !api::memberText(entries, "stt_engine").empty() &&
        !api::memberText(entries, "tts_engine").empty())
      usable = true;
    if (!entries.skipValue()) break;
  }
  if (!usable) {
    fail(errors::pipelineIncomplete, now);
    return;
  }
  error_.clear();
  becomeReady();
}
void AssistSession::text(std::string_view json, int64_t now) {
  if (json.size() > 65536 || !api::isWellFormed(json)) {
    fail(errors::haError, now);
    return;
  }
  api::JsonReader r(json);
  const std::string type = api::memberText(r, "type");
  if (authenticate(type, now)) return;
  const auto id = number(r, "id");
  if (type == "result" && id == list_ && state_ == State::Connecting) {
    pipelinesListed(r, now);
    return;
  }
  if (!busy() || id != run_) return;
  if (type == "result") {
    if (!succeeded(r)) fail(errors::haError, now);
    return;
  }
  if (type == "event") runEvent(r, now);
}
void AssistSession::runEvent(const api::JsonReader& r, int64_t now) {
  const auto event = api::memberValue(r, "event");
  const auto data = api::memberValue(event, "data");
  const std::string stage = api::memberText(event, "type");
  if (stage == "error") {
    const auto code = api::memberText(data, "code");
    fail(code == "stt-provider-missing" ||
                 code == "stt-provider-unsupported-metadata"
             ? errors::pipelineIncomplete
         : code == "stt-no-text-recognized" ? errors::nothingUnderstood
                                            : errors::haError,
         now);
    return;
  }
  if (stage == "run-start") {
    const auto handler =
        number(api::memberValue(data, "runner_data"), "stt_binary_handler_id");
    if (handler < 1 || handler > 255 || handler_ >= 0) {
      fail(errors::haError, now);
      return;
    }
    handler_ = static_cast<int>(handler);
  } else if (stage == "stt-start") {
    if (handler_ < 0 || stt_) {
      fail(errors::haError, now);
      return;
    }
    stt_ = true;
    began_ = now;
    deadline_ = now + kCaptureStartMs;
    microphone_.start(now);
  } else if (stage == "stt-end") {
    recognitionEnded_ = true;
    stopCapture(now);
    state_ = State::Processing;
    deadline_ = now + kAnswerMs;
  } else if (stage == "tts-end") {
    const auto output = api::memberValue(data, "tts_output");
    const std::string url = api::memberText(output, "url");
    std::string target;
    if (state_ == State::Speaking || !ttsTarget(config_, url, target) ||
        !ports_.speak(target)) {
      fail(errors::playbackFailed, now);
      return;
    }
    state_ = State::Speaking;
    deadline_ = now + kSpeakMs;
  } else if (stage == "run-end") {
    runEnded_ = true;
    if (state_ != State::Speaking) {
      fail(errors::haError, now);
      return;
    }
    if (playbackEnded_) becomeReady();
  }
}
void AssistSession::playbackDone(bool success, int64_t now) {
  if (state_ != State::Speaking) return;
  if (!success) {
    fail(errors::playbackFailed, now);
    return;
  }
  playbackEnded_ = true;
  if (runEnded_) becomeReady();
}
void AssistSession::tick(int64_t now) {
  if (!lastError_.empty() && now >= lastErrorUntil_) lastError_.clear();
  if (state_ == State::Ready && !supported_ && now - probeAt_ >= kStreamAnswerMs) {
    probeAt_ = now;
    microphone_.probeAgain();
  }
  if (state_ == State::Error || state_ == State::Offline ||
      state_ == State::Ready)
    return;
  if (deadline_ && now >= deadline_) {
    fail(state_ == State::Connecting ? errors::connectionLost : errors::timeout, now);
    return;
  }
  apply(microphone_.tick(now), now);
}
}  // namespace awtrix::tc002::voice
