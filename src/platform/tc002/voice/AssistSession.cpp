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
  cancel();
  config_ = config;
  error_.clear();
  state_ = State::Connecting;
  deadline_ = now + 10000;
  supported_ = false;
  probeAt_ = now;
  epoch_ = epoch_ == INT_MAX ? 1 : epoch_ + 1;
  ports_.microphone({epoch_, StreamControl::Probe});
}
void AssistSession::cancel() {
  if (capture_) ports_.microphone({epoch_, StreamControl::Stop});
  capture_ = false;
  ports_.stopSpeaker();
  ports_.disconnect();
  state_ = State::Offline;
}
void AssistSession::fail(const char* code, int64_t now) {
  cancel();
  error_ = code;
  state_ = State::Error;
  deadline_ = now + 5000;
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
  error_.clear();
  handler_ = -1;
  samples_ = 0;
  started_ = stopping_ = stt_ = recognitionEnded_ = ended_ = eof_ = runEnded_ =
      playbackEnded_ = false;
  epoch_ = epoch_ == INT_MAX ? 1 : epoch_ + 1;
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
  deadline_ = now + 5000;
  began_ = keepalive_ = now;
  if (!send(std::move(json))) {
    fail(errors::haError, now);
    return false;
  }
  // Prepares HA first; capture starts on stt-start. The listening overlay follows
  // the MCU acknowledgement; no startup speech buffer.
  capture_ = false;
  return true;
}
void AssistSession::stopCapture(int64_t now) {
  if (capture_ && !stopping_) {
    stopping_ = true;
    if (!ports_.microphone({epoch_, StreamControl::Stop}))
      fail(errors::microphoneFailed, now);
    else
      deadline_ = now + 1500;
  }
}
void AssistSession::endInput(int64_t now) {
  if (state_ == State::Starting || state_ == State::Listening) {
    if (!capture_) {
      cancel();
      return;
    }
    stopCapture(now);
    if (state_ != State::Error) state_ = State::Processing;
  }
}
bool AssistSession::audio(const StreamEvent& e, int64_t now) {
  if (handler_ < 0 || !stt_ || now < e.hostAtMs || now - e.hostAtMs > 250)
    return false;
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
  if (!ended_ || !stt_ || recognitionEnded_ || eof_ || handler_ < 0) return;
  if (!ports_.send(std::string(1, static_cast<char>(handler_)), true)) {
    fail(errors::audioTooSlow, now);
    return;
  }
  eof_ = true;
  state_ = State::Processing;
  deadline_ = now + 30000;
}
void AssistSession::receive(const StreamEvent& e, int64_t now) {
  if (e.epoch != epoch_) return;
  if (e.kind == StreamEvent::Kind::Support) {
    supported_ = e.available;
    return;
  }
  if (!capture_) return;
  if (!e.error.empty()) {
    fail(errors::microphoneFailed, now);
    return;
  }
  if (e.kind == StreamEvent::Kind::Started) {
    if (started_) {
      fail(errors::microphoneFailed, now);
      return;
    }
    started_ = true;
    if (stt_ && !stopping_) {
      state_ = State::Listening;
      deadline_ = began_ + 60000;
    }
    return;
  }
  if (!started_ || e.firstSample != samples_) {
    fail(errors::microphoneFailed, now);
    return;
  }
  if (e.kind == StreamEvent::Kind::Audio) {
    samples_ += static_cast<uint32_t>(e.samples.size());
    if (recognitionEnded_) return;
    if (!audio(e, now)) fail(errors::audioTooSlow, now);
  } else if (e.kind == StreamEvent::Kind::Ended) {
    capture_ = false;
    if (started_ && !stopping_) {
      fail(errors::microphoneFailed, now);
      return;
    }
    ended_ = true;
    eof(now);
  }
}
void AssistSession::text(std::string_view json, int64_t now) {
  if (json.size() > 65536 || !api::isWellFormed(json)) {
    fail(errors::haError, now);
    return;
  }
  api::JsonReader r(json);
  const std::string type = api::memberText(r, "type");
  if (state_ == State::Connecting && type == "auth_required") {
    std::string auth;
    api::JsonWriter(auth)
        .beginObject()
        .member("type", "auth")
        .member("access_token", config_.token)
        .endObject();
    if (!send(std::move(auth))) fail(errors::connectionLost, now);
    return;
  }
  if (type == "auth_invalid") {
    fail(errors::tokenRejected, now);
    return;
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
    return;
  }
  const auto id = number(r, "id");
  if (type == "result" && id == list_ && state_ == State::Connecting) {
    bool success = false;
    api::memberValue(r, "success").asBool(success);
    if (!success) {
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
    state_ = State::Ready;
    deadline_ = 0;
    return;
  }
  if (!busy() || id != run_) return;
  if (type == "result") {
    bool success = false;
    api::memberValue(r, "success").asBool(success);
    if (!success) fail(errors::haError, now);
    return;
  }
  if (type != "event") return;
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
    began_ = keepalive_ = now;
    deadline_ = now + 1500;
    capture_ = ports_.microphone({epoch_, StreamControl::Start});
    if (!capture_) {
      fail(errors::microphoneFailed, now);
      return;
    }
  } else if (stage == "stt-end") {
    // HA's voice activity detector ends the input. Stop this capture.
    recognitionEnded_ = true;
    stopCapture(now);
    if (state_ != State::Error) {
      state_ = State::Processing;
      deadline_ = now + 30000;
    }
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
    deadline_ = now + 60000;
  } else if (stage == "run-end") {
    runEnded_ = true;
    if (state_ != State::Speaking) {
      fail(errors::haError, now);
      return;
    }
    if (playbackEnded_) {
      state_ = State::Ready;
      deadline_ = 0;
    }
  }
}
void AssistSession::playbackDone(bool success, int64_t now) {
  if (state_ != State::Speaking) return;
  if (!success) {
    fail(errors::playbackFailed, now);
    return;
  }
  playbackEnded_ = true;
  if (runEnded_) {
    state_ = State::Ready;
    deadline_ = 0;
  }
}
void AssistSession::tick(int64_t now) {
  if (state_ == State::Ready && !supported_ && now - probeAt_ >= 2000) {
    probeAt_ = now;
    ports_.microphone({epoch_, StreamControl::Probe});
  }
  if (state_ == State::Error || state_ == State::Offline ||
      state_ == State::Ready)
    return;
  if (deadline_ && now >= deadline_) {
    fail(errors::timeout, now);
    return;
  }
  if (capture_ && !stopping_ && now - keepalive_ >= 150) {
    keepalive_ = now;
    if (!ports_.microphone({epoch_, StreamControl::Keepalive}))
      fail(errors::microphoneFailed, now);
  }
}
}  // namespace awtrix::tc002::voice
