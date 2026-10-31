#include "../../support.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "platform/tc002/contract/SupervisorProtocol.h"
#include "platform/tc002/runtime/MicrophoneHub.h"

using namespace awtrix;
using awtrix::test::check;
using tc002::MicrophoneHub;
using tc002::StreamControl;
using tc002::StreamEvent;

namespace {

struct Rig {
  std::vector<StreamControl> up;
  std::vector<StreamEvent> tapped;
  bool linkUp = true;
  int64_t now = 1000;
  MicrophoneHub hub{[this](std::string wire) {
                      tc002::SupervisorMessage message;
                      if (tc002::decodeSupervisorMessage(wire, message) &&
                          message.type == tc002::MessageType::MicrophoneStreamControl)
                        up.push_back(message.streamControl);
                      return linkUp;
                    },
                    [this](const StreamEvent& e, int64_t) { tapped.push_back(e); }};
  uint32_t upstreamSample = 0;

  struct Consumer {
    int id = 0;
    std::vector<StreamEvent> events;
  };

  void attach(Consumer& c) {
    c.id = hub.attach([&c](const StreamEvent& e, int64_t) { c.events.push_back(e); });
  }
  // Like the daemon's, each upstream stream counts its samples from 0.
  void upstream(StreamEvent::Kind kind, bool available = true, std::string error = {}) {
    if (kind == StreamEvent::Kind::Started) upstreamSample = 0;
    StreamEvent e;
    e.epoch = up.back().epoch;
    e.kind = kind;
    e.available = available;
    e.error = std::move(error);
    hub.receive(e, now);
  }
  // One 480-sample block captured now, every sample holding its upstream index modulo 32768.
  void block(uint32_t jump = 0) {
    upstreamSample += jump;
    StreamEvent e;
    e.epoch = up.back().epoch;
    e.kind = StreamEvent::Kind::Audio;
    e.firstSample = upstreamSample;
    for (uint32_t i = 0; i < 480; ++i) e.samples.push_back(static_cast<int16_t>((upstreamSample + i) % 32768));
    upstreamSample += 480;
    now += 30;
    e.hostAtMs = now;
    hub.receive(e, now);
  }
  void stream() {
    upstream(StreamEvent::Kind::Support);
    upstream(StreamEvent::Kind::Started);
  }
  // Frames of 150 ms for ms milliseconds; the owner ticks the hub first, then the consumer renews its lease.
  void wait(int64_t ms, const Consumer& keep, int epoch) {
    for (const int64_t end = now + ms; now < end;) {
      now = std::min(now + 150, end);
      hub.tick(now);
      hub.control(keep.id, {epoch, StreamControl::Keepalive});
    }
  }
  unsigned count(StreamControl::Operation operation) const {
    unsigned n = 0;
    for (const auto& c : up) n += c.operation == operation;
    return n;
  }
};

bool is(const StreamEvent& e, StreamEvent::Kind kind, int epoch) { return e.kind == kind && e.epoch == epoch; }

uint64_t heard(const Rig::Consumer& c) {
  uint64_t samples = 0;
  for (const auto& e : c.events)
    if (e.kind == StreamEvent::Kind::Audio) samples += e.samples.size();
  return samples;
}

// The consumer's audio is one run: numbered from 0 without a gap, each sample one above the one before.
bool gapless(const Rig::Consumer& c) {
  uint32_t next = 0;
  int expected = -1;
  for (const auto& e : c.events) {
    if (e.kind != StreamEvent::Kind::Audio) continue;
    if (e.firstSample != next) return false;
    next += static_cast<uint32_t>(e.samples.size());
    for (const int16_t sample : e.samples) {
      if (expected >= 0 && sample != expected) return false;
      expected = (sample + 1) % 32768;
    }
  }
  return true;
}

void probeAlone() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {7, StreamControl::Probe});
  check(rig.up.size() == 1 && rig.up[0].operation == StreamControl::Probe, "a probe while idle probes upstream");
  rig.upstream(StreamEvent::Kind::Support);
  check(a.events.size() == 1 && is(a.events[0], StreamEvent::Kind::Support, 7) && a.events[0].available,
        "the answer reaches the consumer under its own epoch");
  check(rig.up.size() == 1 && !rig.hub.active(), "a probe alone starts no stream");
}

void bareProbeIsNotActive() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Probe});
  check(rig.count(StreamControl::Probe) == 1 && !rig.hub.active(), "a probe alone leaves the hub inactive");
  rig.hub.control(b.id, {2, StreamControl::Start});
  check(rig.hub.active(), "a consumer that wants a stream makes it active while the hub still probes");
}

void startAndAudio() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {3, StreamControl::Start});
  check(rig.count(StreamControl::Probe) == 1, "a start while idle probes first");
  rig.upstream(StreamEvent::Kind::Support);
  check(rig.count(StreamControl::Start) == 1, "an available microphone is started");
  rig.upstream(StreamEvent::Kind::Started);
  check(!a.events.empty() && is(a.events.back(), StreamEvent::Kind::Started, 3), "the consumer hears Started");
  rig.block();
  check(is(a.events.back(), StreamEvent::Kind::Audio, 3) && a.events.back().firstSample == 0 &&
            a.events.back().samples.size() == 480,
        "audio arrives under the consumer's epoch from sample 0");
  check(rig.tapped.size() == 1, "the tap sees the block");
}

void noAnswerInsideControl() {
  Rig rig;
  Rig::Consumer a;
  bool inside = false, early = false;
  a.id = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    early = early || inside;
    a.events.push_back(e);
  });
  const auto control = [&](StreamControl c) {
    inside = true;
    rig.hub.control(a.id, c);
    inside = false;
  };
  control({1, StreamControl::Start});
  rig.stream();
  rig.block();
  const auto before = a.events.size();
  control({2, StreamControl::Probe});
  control({1, StreamControl::Stop});
  control({3, StreamControl::Start});
  check(a.events.size() == before, "control() only queues its answers");
  rig.hub.tick(rig.now);
  check(!early && a.events.size() == before + 3 && is(a.events[before], StreamEvent::Kind::Support, 2) &&
            is(a.events[before + 1], StreamEvent::Kind::Ended, 1) &&
            is(a.events[before + 2], StreamEvent::Kind::Started, 3),
        "the next tick delivers them in order");
}

void answerToCallbackWaits() {
  Rig rig;
  Rig::Consumer a;
  int epoch = 1;
  a.id = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    a.events.push_back(e);
    if (e.kind == StreamEvent::Kind::Ended) rig.hub.control(a.id, {++epoch, StreamControl::Start});
  });
  rig.linkUp = false;
  rig.hub.control(a.id, {epoch, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.hub.tick(rig.now);
  check(a.events.size() == 2 && is(a.events[1], StreamEvent::Kind::Ended, 2),
        "a consumer restarting inside each end hears one answer per tick, not an endless chain");
}

void twoConsumersShareOneStream() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  rig.hub.control(b.id, {9, StreamControl::Start});
  rig.hub.tick(rig.now);
  check(rig.count(StreamControl::Start) == 1, "the second consumer starts no second upstream stream");
  check(is(b.events.back(), StreamEvent::Kind::Started, 9), "a consumer joining a running stream starts at once");
  rig.block();
  check(a.events.back().firstSample == 480 && b.events.back().firstSample == 0,
        "each consumer counts samples from its own start");
}

void lingerAfterLastStop() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  rig.hub.control(a.id, {1, StreamControl::Stop});
  rig.hub.tick(rig.now);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 1) && a.events.back().error.empty() &&
            a.events.back().firstSample == 480,
        "a stop is answered with a clean end after the consumer's last sample");
  rig.now += 500;
  rig.hub.tick(rig.now);
  check(rig.count(StreamControl::Stop) == 0 && rig.hub.active(), "the stream lingers after the last stop");
  rig.hub.control(b.id, {4, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(rig.count(StreamControl::Probe) == 1 && b.events.size() == 2 && b.events[1].firstSample == 0,
        "a start during the linger joins the running stream");
  rig.hub.control(b.id, {4, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  check(rig.count(StreamControl::Stop) == 1, "after the linger the upstream stream stops");
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active(), "its end leaves the hub idle");
}

void leaseExpires() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.wait(750, a, 1);
  check(a.events.back().kind == StreamEvent::Kind::Started, "keepalives every 150 ms keep the stream");
  check(rig.count(StreamControl::Keepalive) >= 4, "the hub renews its own upstream lease");
  rig.now += tc002::kStreamLeaseMs + 1;
  rig.hub.tick(rig.now);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 1) && a.events.back().error == "consumer lease expired",
        "a consumer without keepalive loses its stream");
}

void oneLeaseExpires() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.stream();
  rig.wait(tc002::kStreamLeaseMs + 150, b, 2);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 1) && a.events.back().error == "consumer lease expired",
        "one consumer without keepalive loses its stream");
  rig.block();
  check(is(b.events.back(), StreamEvent::Kind::Audio, 2) && rig.count(StreamControl::Stop) == 0,
        "while the other one keeps it");
}

void upstreamEndReachesAll() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.stream();
  rig.upstream(StreamEvent::Kind::Ended, true, "microphone UART frame lost or corrupt");
  check(a.events.back().error == "microphone UART frame lost or corrupt" &&
            b.events.back().error == "microphone UART frame lost or corrupt",
        "an upstream failure ends every consumer with its reason");
  check(!rig.hub.active() && rig.count(StreamControl::Probe) == 1, "the hub does not retry on its own");
}

void unavailableMicrophone() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {5, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support, false);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 5) && !a.events.back().error.empty() && !rig.hub.active(),
        "a start on an MCU without the stream ends with a reason");
}

void gapIsPassedOn() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  rig.block(240);
  check(a.events.back().firstSample == 720, "a gap upstream is a gap for the consumer");
}

void sampleCountWraps() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.upstreamSample = UINT32_MAX - 959;
  rig.block();
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  for (int i = 0; i < 3; ++i) rig.block();
  check(heard(b) == 1440 && gapless(b), "the consumer's count runs on where the upstream count wraps past 2^32");
}

void callbackCallsBack() {
  Rig rig;
  Rig::Consumer probeThenStart;
  probeThenStart.id = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    probeThenStart.events.push_back(e);
    if (e.kind == StreamEvent::Kind::Support && e.available)
      rig.hub.control(probeThenStart.id, {e.epoch, StreamControl::Start});
  });
  rig.hub.control(probeThenStart.id, {8, StreamControl::Probe});
  rig.upstream(StreamEvent::Kind::Support);
  check(rig.count(StreamControl::Start) == 1, "a start from inside the support answer starts the stream");
  rig.upstream(StreamEvent::Kind::Started);
  check(is(probeThenStart.events.back(), StreamEvent::Kind::Started, 8), "and the consumer hears it");
}

void selfDetach() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  int self = 0;
  unsigned deliveries = 0;
  self = rig.hub.attach([&](const StreamEvent&, int64_t) {
    ++deliveries;
    rig.hub.detach(self);
  });
  rig.hub.control(self, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(deliveries == 1 && !rig.hub.control(self, {2, StreamControl::Stop}),
        "a consumer that detaches itself inside its delivery hears nothing after");
  check(is(a.events.back(), StreamEvent::Kind::Audio, 1), "and the stream goes on for the others");
}

void probeWhileStarting() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.hub.control(b.id, {6, StreamControl::Probe});
  rig.hub.tick(rig.now);
  check(b.events.size() == 1 && is(b.events[0], StreamEvent::Kind::Support, 6) && b.events[0].available &&
            rig.count(StreamControl::Probe) == 1,
        "a probe while the stream starts is answered at once, without asking upstream");
}

void probeLeavesStreamEpoch() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {5, StreamControl::Start});
  rig.stream();
  rig.hub.control(a.id, {6, StreamControl::Probe});
  rig.block();
  const auto n = a.events.size();
  check(n >= 2 && is(a.events[n - 2], StreamEvent::Kind::Support, 6) && a.events[n - 2].available &&
            is(a.events[n - 1], StreamEvent::Kind::Audio, 5),
        "a probe while streaming is answered under its own epoch and leaves the stream's");
  rig.wait(600, a, 5);
  rig.block();
  check(is(a.events.back(), StreamEvent::Kind::Audio, 5), "the stream's keepalive still renews it");
  rig.hub.control(a.id, {5, StreamControl::Stop});
  rig.hub.tick(rig.now);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 5) && a.events.back().error.empty(),
        "and the stream's stop still ends it");
}

void startUnderNewEpoch() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  rig.block();
  rig.hub.control(a.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  const auto n = a.events.size();
  check(is(a.events[n - 2], StreamEvent::Kind::Ended, 1) && a.events[n - 2].firstSample == 960 &&
            is(a.events[n - 1], StreamEvent::Kind::Started, 2),
        "a start under a new epoch first ends the running stream at its position");
}

void restartWhileStopping() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.hub.control(a.id, {1, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.hub.control(a.id, {2, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Ended);
  check(rig.count(StreamControl::Probe) == 1 && rig.count(StreamControl::Start) == 2 &&
            rig.up.back().operation == StreamControl::Start && rig.up.back().epoch != rig.up.front().epoch,
        "a start while stopping starts a new upstream stream once the old one ended, without a probe");
  rig.upstream(StreamEvent::Kind::Started);
  check(is(a.events.back(), StreamEvent::Kind::Started, 2), "which reaches the consumer");
}

void holdKeepsTheStreamOpen() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.hold(true);
  check(rig.up.empty() && !rig.hub.active(), "a hold opens no stream");
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  rig.hub.control(a.id, {1, StreamControl::Stop});
  rig.now += 2 * MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  check(rig.count(StreamControl::Stop) == 0 && rig.hub.active(), "while held, the stream outlives its linger");
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(rig.count(StreamControl::Start) == 1 && is(b.events[0], StreamEvent::Kind::Started, 2) &&
            b.events.back().kind == StreamEvent::Kind::Audio && b.events.back().firstSample == 0,
        "a consumer that starts meanwhile joins it");
  rig.hub.control(b.id, {2, StreamControl::Stop});
  rig.hub.hold(false);
  rig.now += MicrophoneHub::kLingerMs - 1;
  rig.hub.tick(rig.now);
  check(rig.count(StreamControl::Stop) == 0, "after the release the stream lingers");
  rig.now += 1;
  rig.hub.tick(rig.now);
  check(rig.count(StreamControl::Stop) == 1, "and then stops");
}

void holdWhileStarting() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.hub.hold(true);
  rig.hub.control(a.id, {1, StreamControl::Stop});
  rig.upstream(StreamEvent::Kind::Started);
  check(rig.count(StreamControl::Stop) == 0 && rig.hub.active(), "a held stream that is still starting goes on");
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(is(b.events[0], StreamEvent::Kind::Started, 2) && b.events.back().kind == StreamEvent::Kind::Audio &&
            b.events.back().firstSample == 0,
        "and a consumer joins it from its first block");
}

void closeStopsAtOnce() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  rig.hub.control(b.id, {2, StreamControl::Probe});
  rig.hub.hold(true);
  rig.hub.close();
  check(rig.count(StreamControl::Stop) == 1, "closing stops the upstream stream at once, held or not");
  rig.hub.tick(rig.now);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 1) && a.events.back().error == "microphone closed" &&
            a.events.back().firstSample == 480,
        "and ends each consumer's stream at its position");
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active() && rig.count(StreamControl::Probe) == 1 && rig.count(StreamControl::Start) == 1,
        "nothing restarts");
}

void probeWhileStopping() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.hub.control(a.id, {1, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.hub.control(b.id, {3, StreamControl::Probe});
  rig.hub.tick(rig.now);
  check(b.events.size() == 1 && is(b.events[0], StreamEvent::Kind::Support, 3) && b.events[0].available &&
            rig.count(StreamControl::Probe) == 1,
        "a probe while the stream stops is answered at once, without asking upstream again");
}

void lastConsumerLeavesWhileStarting() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.hub.control(a.id, {1, StreamControl::Stop});
  check(rig.count(StreamControl::Stop) == 1, "the last consumer stopping while the stream starts stops it upstream");
  rig.upstream(StreamEvent::Kind::Started);
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active() && rig.count(StreamControl::Probe) == 1 && a.events.size() == 1 &&
            is(a.events[0], StreamEvent::Kind::Ended, 1),
        "its end restarts nothing");
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.hub.detach(b.id);
  check(rig.count(StreamControl::Stop) == 2, "a detach stops it as well");
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active() && rig.count(StreamControl::Probe) == 2, "again without a restart");
}

void upstreamTimeouts() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.tick(rig.now);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.wait(MicrophoneHub::kAnswerMs, a, 1);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 1) && a.events.back().error == "microphone did not answer" &&
            !rig.hub.active(),
        "an unanswered probe ends the attempt");
  rig.hub.control(a.id, {2, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.wait(MicrophoneHub::kAnswerMs, a, 2);
  check(rig.count(StreamControl::Stop) == 1 && is(a.events.back(), StreamEvent::Kind::Ended, 2) &&
            a.events.back().error == "microphone did not answer" && !rig.hub.active(),
        "an unanswered start is stopped upstream and ends the attempt");
  rig.hub.control(a.id, {3, StreamControl::Start});
  rig.stream();
  rig.hub.control(a.id, {3, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.hub.control(a.id, {4, StreamControl::Start});
  rig.wait(MicrophoneHub::kAnswerMs, a, 4);
  check(rig.count(StreamControl::Probe) == 3 && rig.count(StreamControl::Start) == 3 && rig.hub.active(),
        "a stop that never ends gives way to a waiting start after its timeout");
  rig.upstream(StreamEvent::Kind::Started);
  check(is(a.events.back(), StreamEvent::Kind::Started, 4), "which gets its stream");
}

void refusedLink() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.linkUp = false;
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.hub.tick(rig.now);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 1) && a.events.back().error == "supervisor link refused" &&
            !rig.hub.active(),
        "a probe the link refuses ends the attempt at once");
  rig.linkUp = true;
  rig.hub.control(a.id, {2, StreamControl::Start});
  rig.linkUp = false;
  rig.upstream(StreamEvent::Kind::Support);
  check(is(a.events.back(), StreamEvent::Kind::Ended, 2) && a.events.back().error == "supervisor link refused" &&
            !rig.hub.active(),
        "so does a start the link refuses");
}

void probeOfUnansweredAttemptIsDropped() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.tick(rig.now);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.hub.control(b.id, {6, StreamControl::Probe});
  rig.wait(MicrophoneHub::kAnswerMs, a, 1);
  check(b.events.empty() && !rig.hub.active(), "an attempt the daemon never answers drops the waiting probe");
  rig.hub.control(a.id, {2, StreamControl::Start});
  rig.stream();
  check(b.events.empty(), "and a later stream does not answer it");
}

void probeFromInsideSupportIsAnswered() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(b);
  a.id = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    a.events.push_back(e);
    if (e.kind == StreamEvent::Kind::Support) rig.hub.control(b.id, {4, StreamControl::Probe});
  });
  rig.hub.control(a.id, {3, StreamControl::Probe});
  rig.upstream(StreamEvent::Kind::Support);
  check(b.events.size() == 1 && is(b.events[0], StreamEvent::Kind::Support, 4) && b.events[0].available &&
            a.events.size() == 1 && !rig.hub.active(),
        "a probe from inside an answer is answered too");
}

// The upstream samples a consumer heard, by their value: block() fills each with its upstream index.
std::vector<int16_t> heardSamples(const Rig::Consumer& c) {
  std::vector<int16_t> out;
  for (const auto& e : c.events)
    if (e.kind == StreamEvent::Kind::Audio) out.insert(out.end(), e.samples.begin(), e.samples.end());
  return out;
}

void startFromAMark() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  const tc002::MicrophoneMark mark = rig.hub.markOf(a.id, 240);
  check(mark.epoch != 0 && mark.sample == 240, "a consumer's position is a mark of the upstream stream");
  rig.block();
  rig.block();
  rig.hub.startFrom(b.id, mark);
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  const auto heard = heardSamples(b);
  check(is(b.events[0], StreamEvent::Kind::Started, 2) && heard.size() == 1440 - 240 + 480 && heard[0] == 240 &&
            gapless(b),
        "a start from a mark gets the held audio since the mark, then the live stream, gapless");
  bool stale = true;
  for (const auto& e : b.events)
    if (e.kind == StreamEvent::Kind::Audio && e.firstSample < 1200) stale = stale && e.hostAtMs == 0;
  check(stale, "audio from the past carries no capture time");
}

void startFromBeyondTheRing() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  const tc002::MicrophoneMark mark = rig.hub.mark();
  for (int i = 0; i < 40; ++i) rig.block();
  rig.hub.startFrom(b.id, mark);
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  const auto heard = heardSamples(b);
  check(heard.size() == MicrophoneHub::kRingSamples && heard[0] == static_cast<int16_t>(40 * 480 - 16000),
        "a mark older than the ring starts at the oldest held sample");
}

void startFromAnotherStream() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  const tc002::MicrophoneMark old = rig.hub.mark();
  rig.hub.control(a.id, {1, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.upstream(StreamEvent::Kind::Ended);
  rig.hub.startFrom(b.id, old);
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.upstream(StreamEvent::Kind::Started);
  rig.block();
  check(heardSamples(b).size() == 480 && b.events.back().firstSample == 0,
        "a mark of an ended stream starts now");
  check(rig.hub.mark().epoch != old.epoch, "each upstream stream has marks of its own");
}

void gapClearsTheRing() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(a);
  rig.attach(b);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  const tc002::MicrophoneMark mark = rig.hub.markOf(a.id, 0);
  rig.block(96);
  rig.hub.startFrom(b.id, mark);
  rig.hub.control(b.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  check(heardSamples(b).size() == 480 && heardSamples(b)[0] == static_cast<int16_t>(576),
        "a gap clears the held audio: a later start begins at the gap");
  check(rig.hub.markOf(b.id, 0).sample == 576 && rig.hub.markOf(42, 0).epoch == 0,
        "markOf names the consumer's sample 0; an unknown consumer has no mark");
}

void restartFromAMark() {
  Rig rig;
  Rig::Consumer a;
  rig.attach(a);
  rig.hub.control(a.id, {1, StreamControl::Start});
  rig.stream();
  rig.block();
  rig.hub.startFrom(a.id, rig.hub.markOf(a.id, 240));
  rig.hub.control(a.id, {2, StreamControl::Start});
  rig.hub.tick(rig.now);
  const auto n = a.events.size();
  check(is(a.events[n - 3], StreamEvent::Kind::Ended, 1) && is(a.events[n - 2], StreamEvent::Kind::Started, 2) &&
            is(a.events[n - 1], StreamEvent::Kind::Audio, 2) && a.events[n - 1].samples.size() == 240 &&
            a.events[n - 1].samples[0] == 240,
        "a restart under a new epoch begins at its mark too");
}

}  // namespace

int main() {
  probeAlone();
  bareProbeIsNotActive();
  startAndAudio();
  noAnswerInsideControl();
  answerToCallbackWaits();
  twoConsumersShareOneStream();
  lingerAfterLastStop();
  leaseExpires();
  oneLeaseExpires();
  upstreamEndReachesAll();
  unavailableMicrophone();
  gapIsPassedOn();
  sampleCountWraps();
  callbackCallsBack();
  selfDetach();
  probeWhileStarting();
  probeLeavesStreamEpoch();
  startUnderNewEpoch();
  restartWhileStopping();
  holdKeepsTheStreamOpen();
  holdWhileStarting();
  closeStopsAtOnce();
  probeWhileStopping();
  lastConsumerLeavesWhileStarting();
  upstreamTimeouts();
  refusedLink();
  probeOfUnansweredAttemptIsDropped();
  probeFromInsideSupportIsAnswered();
  startFromAMark();
  startFromBeyondTheRing();
  startFromAnotherStream();
  gapClearsTheRing();
  restartFromAMark();
  return awtrix::test::finish("microphone hub");
}
