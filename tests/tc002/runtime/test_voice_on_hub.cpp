#include "../../support.h"
#include "FakeMicrophoneDaemon.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/voice/AssistSession.h"
#include "platform/tc002/voice/VoiceErrors.h"

using namespace awtrix::tc002;
using awtrix::test::check;
using voice::AssistSession;

namespace {

constexpr int kHandler = 7;

struct Rig {
  int64_t now = 1000;
  awtrix::test::FakeMicrophoneDaemon daemon;
  MicrophoneHub hub{daemon.link(), {}};
  MicrophoneHub::Consumer voice = hub.attach([this](const StreamEvent& e, int64_t at) { session.receive(e, at); });
  std::vector<std::pair<std::string, bool>> sent;
  bool disconnected = false;
  AssistSession session{{[this](std::string data, bool binary) {
                           sent.emplace_back(std::move(data), binary);
                           return true;
                         },
                         [this] { disconnected = true; },
                         voice.port(),
                         [](const std::string&) { return true; },
                         [] {}}};
  MicrophoneHub::Consumer keeper = hub.attach([this](const StreamEvent& e, int64_t) { kept.push_back(e); });
  std::vector<StreamEvent> kept;
  bool keeping = false;

  Rig() { hub.tick(now); }

  void answer() {
    for (const StreamEvent& e : daemon.answers(now)) hub.receive(e, now);
    if (daemon.streaming()) hub.receive(daemon.ramp(now), now);
  }
  void fail(const char* error) { hub.receive(daemon.end(error, now), now); }

  void frame() {
    now += 30;
    answer();
    hub.tick(now);
    session.tick(now);
    hub.hold(session.starting());
    if (keeping) keeper.port()({1, StreamControl::Keepalive});
  }
  void frames(int64_t ms) {
    for (const int64_t end = now + ms; now < end;) frame();
  }
  template <class Done>
  void framesUntil(Done done, int64_t ms) {
    for (const int64_t end = now + ms; !done() && now < end;) frame();
  }

  void keep() {
    keeping = true;
    keeper.port()({1, StreamControl::Start});
  }
  void release() {
    keeping = false;
    keeper.port()({1, StreamControl::Stop});
  }
  void connect() {
    session.connecting({true, false, "http://ha:8123", "secret", "", ""}, now);
    frame();
    session.text(R"({"type":"auth_required"})", now);
    session.text(R"({"type":"auth_ok"})", now);
    session.text(
        R"({"id":1,"type":"result","success":true,"result":{"preferred_pipeline":"p","pipelines":[{"id":"p","stt_engine":"stt","tts_engine":"tts"}]}})",
        now);
    disconnected = false;
  }
  void ha(const char* stage, const char* data = "{}") {
    session.text(std::string(R"({"id":2,"type":"event","event":{"type":")") + stage + R"(","data":)" + data + "}}",
                 now);
  }
  void run() {
    session.begin(now);
    ha("run-start", R"({"runner_data":{"stt_binary_handler_id":7}})");
    ha("stt-start");
  }

  std::vector<int16_t> heard() const {
    std::vector<int16_t> samples;
    for (const auto& [data, binary] : sent) {
      if (!binary || data.size() < 3 || data[0] != kHandler) continue;
      for (std::size_t i = 1; i + 1 < data.size(); i += 2)
        samples.push_back(static_cast<int16_t>(static_cast<unsigned char>(data[i]) |
                                               static_cast<unsigned char>(data[i + 1]) << 8));
    }
    return samples;
  }
  bool eofSent() const {
    for (const auto& [data, binary] : sent)
      if (binary && data == std::string(1, kHandler)) return true;
    return false;
  }
  unsigned count(StreamControl::Operation operation) const { return daemon.count(operation); }
  bool listening() const { return session.state() == AssistSession::State::Listening; }
  std::string status() const {
    return "state " + std::to_string(static_cast<int>(session.state())) + ", error '" + session.error() +
           "', probes " + std::to_string(count(StreamControl::Probe)) + ", starts " +
           std::to_string(count(StreamControl::Start)) + ", stops " + std::to_string(count(StreamControl::Stop)) +
           ", heard " + std::to_string(heard().size());
  }
};

bool gapless(const std::vector<int16_t>& samples) {
  for (std::size_t i = 1; i < samples.size(); ++i)
    if (samples[i] != (samples[i - 1] + 1) % 32768) return false;
  return true;
}

void joinsAStreamAnotherConsumerKeeps() {
  Rig r;
  r.keep();
  r.frames(90);
  check(!r.kept.empty() && r.kept.back().kind == StreamEvent::Kind::Audio, "the stand-in streams");
  r.connect();
  check(r.session.ready() && r.count(StreamControl::Probe) == 1,
        "the hub answers Voice's probe itself while the stream runs: " + r.status());
  r.run();
  check(r.session.state() == AssistSession::State::Starting, "Voice's start is answered later, as by the daemon");
  r.frame();
  check(r.listening() && r.heard().size() == kStreamBlockSamples,
        "the next flush brings Started and the first block: " + r.status());
  r.frames(900);
  const auto heard = r.heard();
  check(r.listening() && r.session.error().empty(), "the capture outlives the hub's consumer lease: " + r.status());
  check(heard.size() == 31 * kStreamBlockSamples && gapless(heard), "every block reaches HA, gapless");
  check(r.count(StreamControl::Start) == 1, "Voice joins the running stream instead of starting one");
}

void joinsAStreamReleasedWhileItStarts() {
  Rig r;
  r.keep();
  r.connect();
  r.frames(90);
  r.session.begin(r.now);
  r.frame();
  r.release();
  r.frames(2 * MicrophoneHub::kLingerMs);
  check(r.count(StreamControl::Stop) == 0 && r.hub.active(),
        "while Voice waits for Home Assistant, the hub keeps the stand-in's released stream open");
  r.ha("run-start", R"({"runner_data":{"stt_binary_handler_id":7}})");
  r.ha("stt-start");
  r.frame();
  check(r.listening() && r.count(StreamControl::Start) == 1,
        "Voice joins it once Home Assistant is ready: " + r.status());
  r.frames(300);
  check(!r.heard().empty() && gapless(r.heard()), "and its audio reaches HA");
}

void endsWithoutTimeout() {
  Rig r;
  r.connect();
  check(r.session.ready(), "the hub probes upstream for Voice");
  r.run();
  r.frames(60);
  check(r.listening(), "Voice's start opens the upstream stream");
  r.frames(300);
  const auto captured = r.heard().size();
  r.session.endInput(r.now);
  check(!r.eofSent(), "the end of the capture arrives later, as from the daemon");
  r.frame();
  check(r.eofSent() && r.heard().size() == captured, "the end reaches the session after its stop; HA gets EOF");
  r.frames(5000);
  check(r.session.state() == AssistSession::State::Processing && r.session.error().empty() && !r.disconnected,
        "the session waits for HA without a microphone timeout: " + r.status());
  check(r.count(StreamControl::Stop) == 1 && !r.hub.active(), "the upstream stream has ended meanwhile");
  r.ha("tts-end", R"({"tts_output":{"url":"/api/tts_proxy/answer.wav"}})");
  r.ha("run-end");
  r.session.playbackDone(true, r.now);
  check(r.session.ready(), "the run completes");
}

void cancelWhileCapturing() {
  Rig r;
  r.connect();
  r.run();
  r.frames(210);
  check(r.listening(), "Voice captures");
  const auto captured = r.heard().size();
  r.session.cancel();
  r.frames(1200);
  check(r.session.state() == AssistSession::State::Offline && r.session.error().empty() && r.disconnected,
        "cancelling leaves the session offline; the late end of its stream records no error: " + r.status());
  check(r.heard().size() == captured && !r.eofSent(), "no audio and no EOF reach HA after the cancel");
}

void startsWhileTheHubStops() {
  Rig r;
  r.keep();
  r.connect();
  r.frames(90);
  r.release();
  r.framesUntil([&r] { return r.count(StreamControl::Stop) != 0; }, 2 * MicrophoneHub::kLingerMs);
  check(r.count(StreamControl::Stop) == 1 && r.hub.active(),
        "after the stand-in's linger the hub stops its stream: " + r.status());
  r.run();
  r.framesUntil([&r] { return r.listening(); }, 1500);
  check(r.listening() && r.session.error().empty() && r.count(StreamControl::Probe) == 1 &&
            r.count(StreamControl::Start) == 2,
        "Voice starting while the hub stops gets a new stream within its 1.5 s window, without a probe: " +
            r.status());
  r.frames(300);
  check(!r.heard().empty() && gapless(r.heard()), "and its audio reaches HA");
}

void upstreamFailureEndsBoth() {
  Rig r;
  r.keep();
  r.connect();
  r.run();
  r.frames(120);
  check(r.listening(), "Voice captures beside the stand-in");
  r.fail("microphone UART frame lost or corrupt");
  check(r.session.error() == voice::errors::microphoneFailed, "an upstream failure reaches Voice as microphoneFailed");
  check(!r.kept.empty() && r.kept.back().kind == StreamEvent::Kind::Ended &&
            r.kept.back().error == "microphone UART frame lost or corrupt",
        "and ends the other consumer with its reason");
}

}

int main() {
  joinsAStreamAnotherConsumerKeeps();
  joinsAStreamReleasedWhileItStarts();
  endsWithoutTimeout();
  cancelWhileCapturing();
  startsWhileTheHubStops();
  upstreamFailureEndsBoth();
  return awtrix::test::finish("voice on hub");
}
