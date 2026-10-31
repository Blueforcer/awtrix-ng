#include "../../support.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
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

// HA Voice's session on a microphone hub port, the hub on a fake daemon, as the runtime wires them.
struct Rig {
  int64_t now = 1000;
  std::vector<StreamControl> upstream;
  std::size_t answered = 0;
  bool streaming = false;
  int streamEpoch = 0;
  uint32_t upstreamSample = 0;
  MicrophoneHub hub{[this](std::string wire) {
                      StreamControl control;
                      if (decodeStreamControl(wire, control)) upstream.push_back(control);
                      return true;
                    },
                    {}};
  int voice = hub.attach([this](const StreamEvent& e, int64_t at) { session.receive(e, at); });
  std::vector<std::pair<std::string, bool>> sent;
  bool disconnected = false;
  AssistSession session{{[this](std::string data, bool binary) {
                           sent.emplace_back(std::move(data), binary);
                           return true;
                         },
                         [this] { disconnected = true; },
                         hub.port(voice),
                         [](const std::string&) { return true; },
                         [] {}}};
  // A second consumer standing in for the wake word listener.
  int keeper = hub.attach([this](const StreamEvent& e, int64_t) { kept.push_back(e); });
  std::vector<StreamEvent> kept;
  bool keeping = false;

  // The runtime ticks the hub every frame from its start, and control() takes the time of that tick.
  Rig() { hub.tick(now); }

  // The daemon answers what the hub sent before this frame, then sends one 480-sample block while it
  // streams; every sample holds its upstream index modulo 32768.
  void daemon() {
    const std::size_t pending = upstream.size();
    for (; answered < pending; ++answered) {
      const StreamControl control = upstream[answered];
      StreamEvent e;
      e.epoch = control.epoch;
      e.hostAtMs = now;
      if (control.operation == StreamControl::Probe) {
        e.kind = StreamEvent::Kind::Support;
        e.available = true;
      } else if (control.operation == StreamControl::Start) {
        e.kind = StreamEvent::Kind::Started;
        streaming = true;
        streamEpoch = control.epoch;
        upstreamSample = 0;
      } else if (control.operation == StreamControl::Stop) {
        e.kind = StreamEvent::Kind::Ended;
        e.firstSample = upstreamSample;
        streaming = false;
      } else {
        continue;
      }
      hub.receive(e, now);
    }
    if (!streaming) return;
    StreamEvent block;
    block.epoch = streamEpoch;
    block.kind = StreamEvent::Kind::Audio;
    block.firstSample = upstreamSample;
    block.hostAtMs = now;
    for (uint32_t i = 0; i < kStreamBlockSamples; ++i)
      block.samples.push_back(static_cast<int16_t>((upstreamSample + i) % 32768));
    upstreamSample += kStreamBlockSamples;
    hub.receive(block, now);
  }
  // The daemon ends its stream with an error.
  void fail(const char* error) {
    StreamEvent e;
    e.epoch = streamEpoch;
    e.kind = StreamEvent::Kind::Ended;
    e.firstSample = upstreamSample;
    e.hostAtMs = now;
    e.error = error;
    streaming = false;
    hub.receive(e, now);
  }

  // One 30 ms frame in the runtime's order: the daemon's messages reach the hub, the hub ticks, Voice
  // ticks and holds the hub while it starts, then the stand-in renews.
  void frame() {
    now += 30;
    daemon();
    hub.tick(now);
    session.tick(now);
    hub.hold(session.state() == AssistSession::State::Starting);
    if (keeping) hub.control(keeper, {1, StreamControl::Keepalive});
  }
  void frames(int64_t ms) {
    for (const int64_t end = now + ms; now < end;) frame();
  }

  void keep() {
    keeping = true;
    hub.control(keeper, {1, StreamControl::Start});
  }
  void release() {
    keeping = false;
    hub.control(keeper, {1, StreamControl::Stop});
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
  // An event of the run HA started for begin().
  void ha(const char* stage, const char* data = "{}") {
    session.text(std::string(R"({"id":2,"type":"event","event":{"type":")") + stage + R"(","data":)" + data + "}}",
                 now);
  }
  // Begins a run; HA's stt-start makes the session start its capture.
  void run() {
    session.begin(now);
    ha("run-start", R"({"runner_data":{"stt_binary_handler_id":7}})");
    ha("stt-start");
  }

  // The samples the session sent to HA, in order.
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
  unsigned count(StreamControl::Operation operation) const {
    unsigned n = 0;
    for (const auto& control : upstream) n += control.operation == operation;
    return n;
  }
  bool listening() const { return session.state() == AssistSession::State::Listening; }
};

// Consecutive upstream samples without a gap or a repeat.
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
        "the hub answers Voice's probe itself while the stream runs");
  r.run();
  check(r.session.state() == AssistSession::State::Starting, "Voice's start is answered later, as by the daemon");
  r.frame();
  check(r.listening() && r.heard().size() == kStreamBlockSamples, "the next flush brings Started and the first block");
  r.frames(900);
  const auto heard = r.heard();
  check(r.listening() && r.session.error().empty(), "the capture outlives the hub's consumer lease");
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
  check(r.listening() && r.count(StreamControl::Start) == 1, "Voice joins it once Home Assistant is ready");
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
        "the session waits for HA without a microphone timeout");
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
        "cancelling leaves the session offline; the late end of its stream records no error");
  check(r.heard().size() == captured && !r.eofSent(), "no audio and no EOF reach HA after the cancel");
}

void startsWhileTheHubStops() {
  Rig r;
  r.keep();
  r.connect();
  r.frames(90);
  r.release();
  while (r.count(StreamControl::Stop) == 0) r.frame();
  check(r.hub.active(), "after the stand-in's linger the hub stops its stream");
  const int64_t asked = r.now;
  r.run();
  while (!r.listening() && r.now - asked < 1500) r.frame();
  check(r.listening() && r.session.error().empty() && r.count(StreamControl::Probe) == 1 &&
            r.count(StreamControl::Start) == 2,
        "Voice starting while the hub stops gets a new stream within its 1.5 s window, without a probe");
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

}  // namespace

int main() {
  joinsAStreamAnotherConsumerKeeps();
  joinsAStreamReleasedWhileItStarts();
  endsWithoutTimeout();
  cancelWhileCapturing();
  startsWhileTheHubStops();
  upstreamFailureEndsBoth();
  std::printf("voice on hub: %s\n", awtrix::test::failures() ? "FAILED" : "ok");
  return awtrix::test::failures() ? 1 : 0;
}
