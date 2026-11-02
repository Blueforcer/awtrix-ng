#include "../../support.h"

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/posix/Sequence.h"
#include "platform/tc002/contract/MicrophoneClient.h"

using namespace awtrix;
using awtrix::test::check;
using tc002::MicrophoneClient;
using tc002::StreamControl;
using tc002::StreamEvent;
using Phase = MicrophoneClient::Phase;
using Report = MicrophoneClient::Report;

namespace {

struct Rig {
  std::vector<StreamControl> sent;
  MicrophoneClient client{[this](StreamControl control) {
    sent.push_back(control);
    return true;
  }};
  int64_t now = 1000;

  StreamEvent event(StreamEvent::Kind kind, bool available = true) const {
    StreamEvent e;
    e.epoch = sent.back().epoch;
    e.kind = kind;
    e.available = available;
    return e;
  }
  StreamEvent audio(uint32_t firstSample) const {
    StreamEvent e = event(StreamEvent::Kind::Audio);
    e.firstSample = firstSample;
    e.samples.assign(480, 0);
    return e;
  }
  Report receive(const StreamEvent& e) { return client.receive(e, now); }
  void stream() {
    client.open(now);
    receive(event(StreamEvent::Kind::Support));
    receive(event(StreamEvent::Kind::Started));
  }
  unsigned count(StreamControl::Operation operation) const {
    unsigned n = 0;
    for (const auto& control : sent) n += control.operation == operation;
    return n;
  }
};

void opensAndStreams() {
  Rig rig;
  rig.client.open(rig.now);
  check(rig.sent.size() == 1 && rig.sent[0].operation == StreamControl::Probe && rig.sent[0].epoch == 1 &&
            rig.client.phase() == Phase::Opening,
        "open probes under the first epoch");
  check(rig.receive(rig.event(StreamEvent::Kind::Support)) == Report::None &&
            rig.sent.back().operation == StreamControl::Start && rig.client.phase() == Phase::Starting,
        "an available stream starts at once");
  check(rig.receive(rig.event(StreamEvent::Kind::Started)) == Report::Started &&
            rig.client.phase() == Phase::Streaming,
        "Started opens the stream");
  check(rig.receive(rig.audio(0)) == Report::Audio && rig.receive(rig.audio(480)) == Report::Audio,
        "continuous audio is audio");
  check(rig.receive(rig.audio(1200)) == Report::Gap && rig.receive(rig.audio(1680)) == Report::Audio,
        "a jump is a gap, and the count goes on from it");
  StreamEvent old = rig.audio(2160);
  old.epoch = 7;
  check(rig.receive(old) == Report::None, "another epoch is ignored");
}

void unavailable() {
  Rig rig;
  rig.client.open(rig.now);
  check(rig.receive(rig.event(StreamEvent::Kind::Support, false)) == Report::Unavailable &&
            rig.client.phase() == Phase::Idle && rig.client.error() == tc002::kStreamUnavailable &&
            rig.sent.size() == 1,
        "an MCU without the stream is reported and nothing more is sent");
  check(rig.receive(rig.event(StreamEvent::Kind::Started)) == Report::None, "late events are ignored");
}

void keepalives() {
  Rig rig;
  rig.stream();
  for (int i = 0; i < 20; ++i) {
    rig.now += 30;
    check(rig.client.tick(rig.now) == Report::None, "a stream needs no answer");
  }
  check(rig.count(StreamControl::Keepalive) == 4, "a keepalive every 150 ms");
  rig.client.cancel();
  rig.client.open(rig.now);
  rig.now += 3 * tc002::kStreamKeepaliveMs;
  rig.client.tick(rig.now);
  check(rig.count(StreamControl::Keepalive) == 4, "none while a probe waits for its answer");
}

void answerDeadline() {
  Rig rig;
  rig.client.open(rig.now);
  rig.now += tc002::kStreamAnswerMs - 1;
  check(rig.client.tick(rig.now) == Report::None, "a probe has 2 s for its answer");
  rig.now += 1;
  check(rig.client.tick(rig.now) == Report::Ended && rig.client.error() == tc002::kStreamNoAnswer &&
            rig.sent.back().operation == StreamControl::Stop && rig.client.phase() == Phase::Idle,
        "an unanswered probe ends the attempt and is withdrawn");
  rig.client.open(rig.now);
  check(rig.sent.back().epoch == 2, "a new attempt has a new epoch");
  rig.receive(rig.event(StreamEvent::Kind::Support));
  rig.now += tc002::kStreamAnswerMs;
  check(rig.client.tick(rig.now) == Report::Ended && rig.sent.back().operation == StreamControl::Stop,
        "an unanswered start ends it too");
}

void endings() {
  Rig rig;
  rig.stream();
  StreamEvent failed = rig.event(StreamEvent::Kind::Ended);
  failed.error = "microphone UART frame lost or corrupt";
  check(rig.receive(failed) == Report::Ended && rig.client.error() == failed.error &&
            rig.client.phase() == Phase::Idle && rig.count(StreamControl::Stop) == 0,
        "an end nobody asked for carries its reason; nothing is stopped");

  rig.stream();
  rig.client.stop(rig.now);
  check(rig.sent.back().operation == StreamControl::Stop && rig.client.phase() == Phase::Stopping,
        "stop asks for the end");
  rig.now += tc002::kStreamKeepaliveMs;
  rig.client.tick(rig.now);
  check(rig.sent.back().operation == StreamControl::Stop, "no keepalive while it stops");
  check(rig.receive(rig.event(StreamEvent::Kind::Ended)) == Report::Stopped && rig.client.phase() == Phase::Idle,
        "the end it asked for is a stop");

  rig.stream();
  rig.client.stop(rig.now);
  rig.now += tc002::kStreamAnswerMs;
  check(rig.client.tick(rig.now) == Report::Stopped && rig.client.phase() == Phase::Idle,
        "a stop that never ends is done after 2 s");

  rig.client.open(rig.now);
  const int probing = rig.sent.back().epoch;
  rig.client.stop(rig.now);
  check(rig.sent.back().operation == StreamControl::Stop && rig.sent.back().epoch == probing &&
            rig.client.phase() == Phase::Idle,
        "a probe no longer wanted is withdrawn at once");
  check(rig.receive(rig.event(StreamEvent::Kind::Support)) == Report::None &&
            rig.sent.back().operation == StreamControl::Stop,
        "and its late answer starts nothing");

  rig.stream();
  const std::size_t before = rig.sent.size();
  rig.client.cancel();
  check(rig.sent.size() == before + 1 && rig.sent.back().operation == StreamControl::Stop &&
            rig.client.phase() == Phase::Idle,
        "cancel stops the stream and forgets it");
  rig.client.cancel();
  check(rig.sent.size() == before + 1, "a second cancel sends nothing");
}

void aProbeStartsNothing() {
  Rig rig;
  rig.client.probe();
  const int epoch = rig.sent.back().epoch;
  check(rig.sent.back().operation == StreamControl::Probe && rig.client.epoch() == epoch &&
            rig.client.phase() == Phase::Probing,
        "a probe asks");
  check(rig.receive(rig.event(StreamEvent::Kind::Support)) == Report::Available && rig.sent.size() == 1,
        "its answer starts nothing");
  check(rig.receive(rig.event(StreamEvent::Kind::Support, false)) == Report::Unavailable,
        "and every later answer counts");
  rig.client.probeAgain();
  check(rig.sent.back().operation == StreamControl::Probe && rig.sent.back().epoch == epoch,
        "asking again keeps the epoch");
  check(rig.receive(rig.event(StreamEvent::Kind::Ended)) == Report::None &&
            rig.receive(rig.event(StreamEvent::Kind::Started)) == Report::None,
        "a probe knows no stream");
  rig.now += 100000;
  check(rig.client.tick(rig.now) == Report::None && rig.sent.size() == 2, "and has no deadline and no keepalive");
  rig.client.cancel();
  check(rig.sent.size() == 2 && rig.receive(rig.event(StreamEvent::Kind::Support)) == Report::None,
        "cancelling it sends nothing, and later answers are ignored");
}

void aStartNeedsNoProbe() {
  Rig rig;
  rig.client.probe();
  const int probed = rig.client.epoch();
  rig.client.start(rig.now);
  check(rig.sent.back().operation == StreamControl::Start && rig.sent.back().epoch != probed &&
            rig.client.capturing() && !rig.client.started(),
        "a start opens a new epoch");
  check(rig.receive(rig.event(StreamEvent::Kind::Started)) == Report::Started && rig.client.started(),
        "the stream starts");
  rig.client.tick(rig.now + tc002::kStreamKeepaliveMs);
  check(rig.sent.back().operation == StreamControl::Keepalive, "and is kept alive from the start on");
}

void aStopRemembersWhetherTheStreamStarted() {
  Rig never;
  never.client.start(never.now);
  never.client.stop(never.now);
  check(never.sent.back().operation == StreamControl::Stop && !never.client.capturing(), "the stop goes out");
  check(never.receive(never.event(StreamEvent::Kind::Ended)) == Report::Stopped && !never.client.started(),
        "the end of a stream that never started");

  Rig late;
  late.client.start(late.now);
  late.client.stop(late.now);
  check(late.receive(late.event(StreamEvent::Kind::Started)) == Report::None,
        "a start already on its way reports nothing");
  check(late.receive(late.event(StreamEvent::Kind::Ended)) == Report::Stopped && late.client.started(), "but it counts");
}

}

int main() {
  opensAndStreams();
  unavailable();
  keepalives();
  answerDeadline();
  endings();
  aProbeStartsNothing();
  aStartNeedsNoProbe();
  aStopRemembersWhetherTheStreamStarted();
  check(posix::nextInSequence(1) == 2 && posix::nextInSequence(INT_MAX) == 1,
        "epochs and serial numbers count up and wrap to 1");
  return awtrix::test::finish("tc002-microphone-client");
}
