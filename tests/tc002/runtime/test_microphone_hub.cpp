#include "../../support.h"
#include "FakeMicrophoneDaemon.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "platform/tc002/runtime/MicrophoneHub.h"

using namespace awtrix;
using awtrix::test::check;
using tc002::MicrophoneHub;
using tc002::StreamControl;
using tc002::StreamEvent;

namespace {

std::string describe(const StreamEvent& e) {
  switch (e.kind) {
    case StreamEvent::Kind::Support:
      return "Support/" + std::to_string(e.epoch) + (e.available ? "" : " unavailable");
    case StreamEvent::Kind::Started:
      return "Started/" + std::to_string(e.epoch);
    case StreamEvent::Kind::Audio:
      return "Audio/" + std::to_string(e.epoch) + "@" + std::to_string(e.firstSample) + "+" +
             std::to_string(e.samples.size());
    case StreamEvent::Kind::Ended:
      return "Ended/" + std::to_string(e.epoch) + "@" + std::to_string(e.firstSample) +
             (e.error.empty() ? "" : " (" + e.error + ")");
  }
  return "?";
}

struct Rig {
  test::FakeMicrophoneDaemon daemon;
  std::vector<StreamEvent> tapped;
  int64_t now = 1000;
  MicrophoneHub hub{daemon.link(), [this](const StreamEvent& e, int64_t) { tapped.push_back(e); }};

  struct Consumer {
    MicrophoneHub::Consumer handle;
    std::vector<StreamEvent> events;

    std::string last(std::size_t count = 1) const {
      std::string out;
      for (std::size_t i = events.size() - std::min(count, events.size()); i < events.size(); ++i)
        out += (out.empty() ? "" : ", ") + describe(events[i]);
      return out;
    }
    std::string all() const { return last(events.size()); }
    bool control(const StreamControl& c) const { return handle.port()(c); }
  };

  void attach(Consumer& c) {
    c.handle = hub.attach([&c](const StreamEvent& e, int64_t) { c.events.push_back(e); });
  }
  void upstream(StreamEvent::Kind kind, bool available = true, std::string error = {}) {
    hub.receive(daemon.event(kind, available, std::move(error)), now);
  }
  void block(uint32_t jump = 0) {
    daemon.sample += jump;
    now += 30;
    hub.receive(daemon.ramp(now), now);
  }
  void stream() {
    upstream(StreamEvent::Kind::Support);
    upstream(StreamEvent::Kind::Started);
  }
  void wait(int64_t ms, const Consumer& keep, int epoch) {
    for (const int64_t end = now + ms; now < end;) {
      now = std::min(now + 150, end);
      hub.tick(now);
      keep.control({epoch, StreamControl::Keepalive});
    }
  }
  std::string sent() const {
    std::string out;
    for (const StreamControl& c : daemon.controls)
      out += (out.empty() ? "" : " ") + std::to_string(static_cast<int>(c.operation)) + "/" + std::to_string(c.epoch);
    return out;
  }
};

struct One : Rig {
  Consumer a;

  One() { attach(a); }
  void running(int epoch = 1) {
    a.control({epoch, StreamControl::Start});
    stream();
  }
};

struct Two : One {
  Consumer b;

  Two() { attach(b); }
  void joinFrom(tc002::MicrophoneMark mark, int epoch = 2) {
    b.handle.startFrom(mark);
    b.control({epoch, StreamControl::Start});
    hub.tick(now);
  }
};

uint64_t heard(const Rig::Consumer& c) {
  uint64_t samples = 0;
  for (const auto& e : c.events)
    if (e.kind == StreamEvent::Kind::Audio) samples += e.samples.size();
  return samples;
}

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

std::vector<int16_t> heardSamples(const Rig::Consumer& c) {
  std::vector<int16_t> out;
  for (const auto& e : c.events)
    if (e.kind == StreamEvent::Kind::Audio) out.insert(out.end(), e.samples.begin(), e.samples.end());
  return out;
}

std::string startOf(const std::vector<int16_t>& samples) {
  return std::to_string(samples.size()) + " samples from " + (samples.empty() ? "-" : std::to_string(samples[0]));
}

void probeAlone() {
  Two rig;
  rig.a.control({7, StreamControl::Probe});
  check(rig.sent() == "0/1", "a probe while idle probes upstream: " + rig.sent());
  check(!rig.hub.active(), "a probe alone leaves the hub inactive");
  rig.upstream(StreamEvent::Kind::Support);
  check(rig.a.all() == "Support/7", "the answer reaches the consumer under its own epoch: " + rig.a.all());
  check(rig.daemon.controls.size() == 1 && !rig.hub.active(), "a probe alone starts no stream: " + rig.sent());
  rig.a.control({8, StreamControl::Probe});
  rig.b.control({2, StreamControl::Start});
  check(rig.daemon.count(StreamControl::Probe) == 2 && rig.daemon.count(StreamControl::Start) == 0 && rig.hub.active(),
        "a consumer that wants a stream makes it active while the hub still probes");
}

void startAndAudio() {
  One rig;
  rig.a.control({3, StreamControl::Start});
  check(rig.sent() == "0/1", "a start while idle probes first: " + rig.sent());
  rig.upstream(StreamEvent::Kind::Support);
  check(rig.sent() == "0/1 1/1", "an available microphone is started: " + rig.sent());
  rig.upstream(StreamEvent::Kind::Started);
  check(rig.a.last() == "Started/3", "the consumer hears Started: " + rig.a.last());
  rig.block();
  check(rig.a.last() == "Audio/3@0+480", "audio arrives under the consumer's epoch from sample 0: " + rig.a.last());
  check(rig.tapped.size() == 1, "the tap sees the block");
}

void noAnswerInsideControl() {
  Rig rig;
  Rig::Consumer a;
  bool inside = false, early = false;
  a.handle = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    early = early || inside;
    a.events.push_back(e);
  });
  const auto control = [&](StreamControl c) {
    inside = true;
    a.control(c);
    inside = false;
  };
  control({1, StreamControl::Start});
  rig.stream();
  rig.block();
  const auto before = a.events.size();
  control({2, StreamControl::Probe});
  control({1, StreamControl::Stop});
  control({3, StreamControl::Start});
  check(a.events.size() == before, "control() only queues its answers: " + a.last(3));
  rig.hub.tick(rig.now);
  check(!early && a.events.size() == before + 3 && a.last(3) == "Support/2, Ended/1@480, Started/3",
        "the next tick delivers them in order: " + a.last(3));
}

void answerToCallbackWaits() {
  Rig rig;
  Rig::Consumer a;
  int epoch = 1;
  a.handle = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    a.events.push_back(e);
    if (e.kind == StreamEvent::Kind::Ended) a.control({++epoch, StreamControl::Start});
  });
  rig.daemon.linkUp = false;
  a.control({epoch, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.hub.tick(rig.now);
  check(a.all() == "Ended/1@0 (supervisor link refused), Ended/2@0 (supervisor link refused)",
        "a consumer restarting inside each end hears one answer per tick, not an endless chain: " + a.all());
}

void twoConsumersShareOneStream() {
  Two rig;
  rig.running();
  rig.block();
  rig.b.control({9, StreamControl::Start});
  rig.hub.tick(rig.now);
  check(rig.daemon.count(StreamControl::Start) == 1, "the second consumer starts no second upstream stream");
  check(rig.b.last() == "Started/9", "a consumer joining a running stream starts at once: " + rig.b.last());
  rig.block();
  check(rig.a.last() == "Audio/1@480+480" && rig.b.last() == "Audio/9@0+480",
        "each consumer counts samples from its own start: " + rig.a.last() + " / " + rig.b.last());
}

void lingerAfterLastStop() {
  Two rig;
  rig.running();
  rig.block();
  rig.a.control({1, StreamControl::Stop});
  rig.hub.tick(rig.now);
  check(rig.a.last() == "Ended/1@480",
        "a stop is answered with a clean end after the consumer's last sample: " + rig.a.last());
  rig.now += 500;
  rig.hub.tick(rig.now);
  check(rig.daemon.count(StreamControl::Stop) == 0 && rig.hub.active(), "the stream lingers after the last stop");
  rig.b.control({4, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(rig.daemon.count(StreamControl::Probe) == 1 && rig.b.all() == "Started/4, Audio/4@0+480",
        "a start during the linger joins the running stream: " + rig.b.all());
  rig.b.control({4, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  check(rig.daemon.count(StreamControl::Stop) == 1, "after the linger the upstream stream stops");
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active(), "its end leaves the hub idle");
}

void leaseExpires() {
  One rig;
  rig.running();
  rig.wait(750, rig.a, 1);
  check(rig.a.last() == "Started/1", "keepalives every 150 ms keep the stream: " + rig.a.last());
  check(rig.daemon.count(StreamControl::Keepalive) >= 4, "the hub renews its own upstream lease: " + rig.sent());
  rig.now += tc002::kStreamLeaseMs + 1;
  rig.hub.tick(rig.now);
  check(rig.a.last() == "Ended/1@0 (consumer lease expired)",
        "a consumer without keepalive loses its stream: " + rig.a.last());
}

void oneLeaseExpires() {
  Two rig;
  rig.a.control({1, StreamControl::Start});
  rig.b.control({2, StreamControl::Start});
  rig.stream();
  for (const int64_t end = rig.now + tc002::kStreamLeaseMs + 150; rig.now < end;) {
    rig.block();
    rig.wait(120, rig.b, 2);
  }
  check(rig.a.last().find("Ended/1@") == 0 && rig.a.last().find("(consumer lease expired)") != std::string::npos,
        "one consumer without keepalive loses its stream: " + rig.a.last());
  rig.block();
  check(rig.b.last().find("Audio/2") == 0 && rig.daemon.count(StreamControl::Stop) == 0,
        "while the other one keeps it: " + rig.b.last());
}

void aStalledLoopEndsNoConsumer() {
  One rig;
  rig.running();
  rig.block();
  rig.now += 1000;
  rig.block();
  rig.hub.tick(rig.now);
  check(rig.a.last() == "Audio/1@480+480", "a second without a tick costs no consumer: " + rig.a.last());
  check(rig.daemon.count(StreamControl::Stop) == 0, "and leaves the stream open: " + rig.sent());
}

void silentStreamEnds() {
  One rig;
  rig.running();
  for (int64_t end = rig.now + 2 * tc002::kStreamAnswerMs; rig.now < end;) {
    rig.block();
    rig.hub.tick(rig.now);
    rig.a.control({1, StreamControl::Keepalive});
  }
  check(rig.a.last().find("Audio/1") == 0 && rig.daemon.count(StreamControl::Stop) == 0,
        "a stream that delivers runs on: " + rig.a.last());
  const uint32_t position = rig.daemon.sample;
  rig.wait(tc002::kStreamAnswerMs - 150, rig.a, 1);
  check(rig.a.last().find("Audio/1") == 0, "a short silence ends nothing: " + rig.a.last());
  rig.wait(150, rig.a, 1);
  check(rig.a.last() == "Ended/1@" + std::to_string(position) + " (microphone stream stalled)",
        "a silent stream ends its consumers: " + rig.a.last());
  check(rig.daemon.count(StreamControl::Stop) == 1, "and asks the daemon to stop: " + rig.sent());
  check(!rig.hub.active(), "nothing is left open");

  rig.a.control({2, StreamControl::Start});
  check(rig.daemon.last().operation == StreamControl::Probe && rig.daemon.last().epoch == 2,
        "the next start opens a new stream: " + rig.sent());
}

void upstreamEndReachesAll() {
  Two rig;
  rig.a.control({1, StreamControl::Start});
  rig.b.control({2, StreamControl::Start});
  rig.stream();
  rig.upstream(StreamEvent::Kind::Ended, true, "microphone UART frame lost or corrupt");
  check(rig.a.last() == "Ended/1@0 (microphone UART frame lost or corrupt)" &&
            rig.b.last() == "Ended/2@0 (microphone UART frame lost or corrupt)",
        "an upstream failure ends every consumer with its reason: " + rig.a.last() + " / " + rig.b.last());
  check(!rig.hub.active() && rig.daemon.count(StreamControl::Probe) == 1, "the hub does not retry on its own");
}

void unavailableMicrophone() {
  One rig;
  rig.a.control({5, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support, false);
  check(rig.a.last() == std::string("Ended/5@0 (") + tc002::kStreamUnavailable + ")" && !rig.hub.active(),
        "a start on an MCU without the stream ends with a reason: " + rig.a.last());
}

void gapIsPassedOn() {
  One rig;
  rig.running();
  rig.block();
  rig.block(240);
  check(rig.a.last() == "Audio/1@720+480", "a gap upstream is a gap for the consumer: " + rig.a.last());
}

void sampleCountWraps() {
  Two rig;
  rig.running();
  rig.daemon.sample = UINT32_MAX - 959;
  rig.block();
  rig.b.control({2, StreamControl::Start});
  rig.hub.tick(rig.now);
  for (int i = 0; i < 3; ++i) rig.block();
  check(heard(rig.b) == 1440 && gapless(rig.b),
        "the consumer's count runs on where the upstream count wraps past 2^32: " + rig.b.all());
}

void callbackCallsBack() {
  Rig rig;
  Rig::Consumer probeThenStart;
  probeThenStart.handle = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    probeThenStart.events.push_back(e);
    if (e.kind == StreamEvent::Kind::Support && e.available)
      probeThenStart.control({e.epoch, StreamControl::Start});
  });
  probeThenStart.control({8, StreamControl::Probe});
  rig.upstream(StreamEvent::Kind::Support);
  check(rig.daemon.count(StreamControl::Start) == 1, "a start from inside the support answer starts the stream");
  rig.upstream(StreamEvent::Kind::Started);
  check(probeThenStart.last() == "Started/8", "and the consumer hears it: " + probeThenStart.last());
}

void selfDetach() {
  One rig;
  rig.running();
  MicrophoneHub::Consumer self;
  unsigned deliveries = 0;
  self = rig.hub.attach([&](const StreamEvent&, int64_t) {
    ++deliveries;
    self.detach();
  });
  const tc002::MicrophonePort port = self.port();
  port({2, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(deliveries == 1 && !self.port()({2, StreamControl::Start}) && !port({2, StreamControl::Stop}),
        "a consumer that detaches itself inside its delivery hears nothing after: " + std::to_string(deliveries));
  check(rig.a.last() == "Audio/1@0+480", "and the stream goes on for the others: " + rig.a.last());
}

void probeWhileStarting() {
  Two rig;
  rig.a.control({1, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.b.control({6, StreamControl::Probe});
  rig.hub.tick(rig.now);
  check(rig.b.all() == "Support/6" && rig.daemon.count(StreamControl::Probe) == 1,
        "a probe while the stream starts is answered at once, without asking upstream: " + rig.b.all());
}

void probeLeavesStreamEpoch() {
  One rig;
  rig.running(5);
  rig.a.control({6, StreamControl::Probe});
  rig.block();
  check(rig.a.last(2) == "Support/6, Audio/5@0+480",
        "a probe while streaming is answered under its own epoch and leaves the stream's: " + rig.a.last(2));
  rig.wait(600, rig.a, 5);
  rig.block();
  check(rig.a.last() == "Audio/5@480+480", "the stream's keepalive still renews it: " + rig.a.last());
  rig.a.control({5, StreamControl::Stop});
  rig.hub.tick(rig.now);
  check(rig.a.last() == "Ended/5@960", "and the stream's stop still ends it: " + rig.a.last());
}

void startUnderNewEpoch() {
  One rig;
  rig.running();
  rig.block();
  rig.block();
  rig.a.control({2, StreamControl::Start});
  rig.hub.tick(rig.now);
  check(rig.a.last(2) == "Ended/1@960, Started/2",
        "a start under a new epoch first ends the running stream at its position: " + rig.a.last(2));
}

void restartWhileStopping() {
  One rig;
  rig.running();
  rig.a.control({1, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.a.control({2, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Ended);
  check(rig.sent() == "0/1 1/1 2/1 1/2",
        "a start while stopping starts a new upstream stream once the old one ended, without a probe: " + rig.sent());
  rig.upstream(StreamEvent::Kind::Started);
  check(rig.a.last() == "Started/2", "which reaches the consumer: " + rig.a.last());
}

void holdKeepsTheStreamOpen() {
  Two rig;
  rig.hub.hold(true);
  check(rig.daemon.controls.empty() && !rig.hub.active(), "a hold opens no stream");
  rig.running();
  rig.block();
  rig.a.control({1, StreamControl::Stop});
  rig.now += 2 * MicrophoneHub::kLingerMs;
  rig.block();
  rig.hub.tick(rig.now);
  check(rig.daemon.count(StreamControl::Stop) == 0 && rig.hub.active(), "while held, the stream outlives its linger");
  rig.b.control({2, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(rig.daemon.count(StreamControl::Start) == 1 && rig.b.all() == "Started/2, Audio/2@0+480",
        "a consumer that starts meanwhile joins it: " + rig.b.all());
  rig.b.control({2, StreamControl::Stop});
  rig.hub.hold(false);
  rig.now += MicrophoneHub::kLingerMs - 1;
  rig.hub.tick(rig.now);
  check(rig.daemon.count(StreamControl::Stop) == 0, "after the release the stream lingers");
  rig.now += 1;
  rig.hub.tick(rig.now);
  check(rig.daemon.count(StreamControl::Stop) == 1, "and then stops");
}

void holdWhileStarting() {
  Two rig;
  rig.a.control({1, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.hub.hold(true);
  rig.a.control({1, StreamControl::Stop});
  rig.upstream(StreamEvent::Kind::Started);
  check(rig.daemon.count(StreamControl::Stop) == 0 && rig.hub.active(), "a held stream that is still starting goes on");
  rig.b.control({2, StreamControl::Start});
  rig.hub.tick(rig.now);
  rig.block();
  check(rig.b.all() == "Started/2, Audio/2@0+480", "and a consumer joins it from its first block: " + rig.b.all());
}

void closeStopsAtOnce() {
  Two rig;
  rig.running();
  rig.block();
  rig.b.control({2, StreamControl::Probe});
  rig.hub.hold(true);
  rig.hub.close();
  check(rig.daemon.count(StreamControl::Stop) == 1, "closing stops the upstream stream at once, held or not");
  rig.hub.tick(rig.now);
  check(rig.a.last() == "Ended/1@480 (microphone closed)",
        "and ends each consumer's stream at its position: " + rig.a.last());
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active() && rig.sent() == "0/1 1/1 2/1", "nothing restarts: " + rig.sent());
}

void probeWhileStopping() {
  Two rig;
  rig.running();
  rig.a.control({1, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.b.control({3, StreamControl::Probe});
  rig.hub.tick(rig.now);
  check(rig.b.all() == "Support/3" && rig.daemon.count(StreamControl::Probe) == 1,
        "a probe while the stream stops is answered at once, without asking upstream again: " + rig.b.all());
}

void lastConsumerLeavesWhileStarting() {
  Two rig;
  rig.a.control({1, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.a.control({1, StreamControl::Stop});
  check(rig.daemon.count(StreamControl::Stop) == 1,
        "the last consumer stopping while the stream starts stops it upstream: " + rig.sent());
  rig.upstream(StreamEvent::Kind::Started);
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active() && rig.daemon.count(StreamControl::Probe) == 1 && rig.a.all() == "Ended/1@0",
        "its end restarts nothing: " + rig.a.all());
  rig.b.control({2, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.b.handle.detach();
  check(rig.daemon.count(StreamControl::Stop) == 2, "a detach stops it as well: " + rig.sent());
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.active() && rig.daemon.count(StreamControl::Probe) == 2, "again without a restart: " + rig.sent());
}

void upstreamTimeouts() {
  One rig;
  rig.hub.tick(rig.now);
  rig.a.control({1, StreamControl::Start});
  rig.wait(tc002::kStreamAnswerMs, rig.a, 1);
  const std::string noAnswer = std::string(" (") + tc002::kStreamNoAnswer + ")";
  check(rig.a.last() == "Ended/1@0" + noAnswer && !rig.hub.active(),
        "an unanswered probe ends the attempt: " + rig.a.last());
  rig.a.control({2, StreamControl::Start});
  rig.upstream(StreamEvent::Kind::Support);
  rig.wait(tc002::kStreamAnswerMs, rig.a, 2);
  check(rig.daemon.count(StreamControl::Stop) == 1 && rig.a.last() == "Ended/2@0" + noAnswer && !rig.hub.active(),
        "an unanswered start is stopped upstream and ends the attempt: " + rig.a.last());
  rig.running(3);
  rig.a.control({3, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.a.control({4, StreamControl::Start});
  rig.wait(tc002::kStreamAnswerMs, rig.a, 4);
  check(rig.daemon.count(StreamControl::Probe) == 3 && rig.daemon.count(StreamControl::Start) == 3 &&
            rig.hub.active(),
        "a stop that never ends gives way to a waiting start after its timeout: " + rig.sent());
  rig.upstream(StreamEvent::Kind::Started);
  check(rig.a.last() == "Started/4", "which gets its stream: " + rig.a.last());
}

void refusedLink() {
  One rig;
  rig.daemon.linkUp = false;
  rig.a.control({1, StreamControl::Start});
  rig.hub.tick(rig.now);
  check(rig.a.last() == "Ended/1@0 (supervisor link refused)" && !rig.hub.active(),
        "a probe the link refuses ends the attempt at once: " + rig.a.last());
  rig.daemon.linkUp = true;
  rig.a.control({2, StreamControl::Start});
  rig.daemon.linkUp = false;
  rig.upstream(StreamEvent::Kind::Support);
  check(rig.a.last() == "Ended/2@0 (supervisor link refused)" && !rig.hub.active(),
        "so does a start the link refuses: " + rig.a.last());
}

void probeOfUnansweredAttemptIsDropped() {
  Two rig;
  rig.hub.tick(rig.now);
  rig.a.control({1, StreamControl::Start});
  rig.b.control({6, StreamControl::Probe});
  rig.wait(tc002::kStreamAnswerMs, rig.a, 1);
  check(rig.b.events.empty() && !rig.hub.active(),
        "an attempt the daemon never answers drops the waiting probe: " + rig.b.all());
  rig.running(2);
  check(rig.b.events.empty(), "and a later stream does not answer it: " + rig.b.all());
}

void probeFromInsideSupportIsAnswered() {
  Rig rig;
  Rig::Consumer a, b;
  rig.attach(b);
  a.handle = rig.hub.attach([&](const StreamEvent& e, int64_t) {
    a.events.push_back(e);
    if (e.kind == StreamEvent::Kind::Support) b.control({4, StreamControl::Probe});
  });
  a.control({3, StreamControl::Probe});
  rig.upstream(StreamEvent::Kind::Support);
  check(b.all() == "Support/4" && a.all() == "Support/3" && !rig.hub.active(),
        "a probe from inside an answer is answered too: " + a.all() + " / " + b.all());
}

void startFromAMark() {
  Two rig;
  rig.running();
  rig.block();
  const tc002::MicrophoneMark mark = rig.a.handle.markOf(240);
  check(mark.epoch != 0 && mark.sample == 240, "a consumer's position is a mark of the upstream stream");
  rig.block();
  rig.block();
  rig.joinFrom(mark);
  rig.block();
  const auto heard = heardSamples(rig.b);
  check(!rig.b.events.empty() && describe(rig.b.events.front()) == "Started/2" && heard.size() == 1440 - 240 + 480 &&
            heard[0] == 240 && gapless(rig.b),
        "a start from a mark gets the held audio since the mark, then the live stream, gapless: " + startOf(heard));
  bool stale = true;
  for (const auto& e : rig.b.events)
    if (e.kind == StreamEvent::Kind::Audio && e.firstSample < 1200) stale = stale && e.hostAtMs == 0;
  check(stale, "audio from the past carries no capture time");
}

void startFromOffTheGrid() {
  Two rig;
  rig.running();
  rig.block();
  rig.block();
  rig.joinFrom(rig.a.handle.markOf(250));
  const auto heard = heardSamples(rig.b);
  check(heard.size() == 960 - 240 && heard[0] == 240 && gapless(rig.b),
        "a mark off the 24-sample grid starts at the grid point before it: " + startOf(heard));
}

void startFromBeyondTheRing() {
  Two rig;
  rig.running();
  const tc002::MicrophoneMark mark = rig.hub.mark();
  for (int i = 0; i < 40; ++i) rig.block();
  rig.joinFrom(mark);
  const auto heard = heardSamples(rig.b);
  check(heard.size() == MicrophoneHub::kRingSamples &&
            heard[0] == static_cast<int16_t>(40 * 480 - MicrophoneHub::kRingSamples),
        "a mark older than the ring starts at the oldest held sample: " + startOf(heard));
  bool onGrid = true;
  for (const auto& e : rig.b.events)
    if (e.kind == StreamEvent::Kind::Audio) onGrid = onGrid && e.firstSample % 24 == 0 && e.samples.size() % 24 == 0;
  check(onGrid, "the held audio keeps the contract's grid of 24 samples: " + rig.b.all());
}

void startFromAMarkAcrossTheWrap() {
  Two rig;
  rig.running();
  rig.daemon.sample = UINT32_MAX - 959;
  rig.block();
  const tc002::MicrophoneMark mark = rig.hub.mark();
  rig.block();
  rig.block();
  rig.joinFrom(mark);
  rig.block();
  const auto heard = heardSamples(rig.b);
  check(heard.size() == 960 + 480 && heard[0] == static_cast<int16_t>((UINT32_MAX - 479) % 32768) && gapless(rig.b),
        "a start from a mark before the wrap past 2^32 gets the held audio across it, gapless: " + startOf(heard));
}

void startFromAMarkAhead() {
  Two rig;
  rig.running();
  rig.block();
  rig.block();
  rig.joinFrom(rig.a.handle.markOf(1440));
  rig.block();
  const auto heard = heardSamples(rig.b);
  check(heard.size() == 480 && heard[0] == 960, "a mark ahead of the stream starts now: " + startOf(heard));
}

void startFromAnotherStream() {
  Two rig;
  rig.running();
  rig.block();
  const tc002::MicrophoneMark old = rig.hub.mark();
  check(rig.hub.live(old) && !rig.hub.live({}), "a mark of the running stream is live, no mark is not");
  rig.a.control({1, StreamControl::Stop});
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  rig.upstream(StreamEvent::Kind::Ended);
  check(!rig.hub.live(old), "a mark is not live once its stream ended");
  rig.running(2);
  check(!rig.hub.live(old) && rig.hub.live(rig.hub.mark()), "nor in the next stream, whose marks are");
  for (int i = 0; i < 4; ++i) rig.block();
  rig.joinFrom(old, 3);
  rig.block();
  const auto heard = heardSamples(rig.b);
  check(heard.size() == 480 && heard[0] == 1920 && rig.b.handle.markOf(0).sample == 1920,
        "a mark of an ended stream starts now, even where the new stream holds a sample of that number: " +
            startOf(heard));
  check(rig.hub.mark().epoch != old.epoch, "each upstream stream has marks of its own");
}

void gapClearsTheRing() {
  Two rig;
  rig.running();
  rig.block();
  const tc002::MicrophoneMark mark = rig.a.handle.markOf(0);
  rig.block(96);
  rig.joinFrom(mark);
  const auto heard = heardSamples(rig.b);
  check(heard.size() == 480 && heard[0] == static_cast<int16_t>(576),
        "a gap clears the held audio: a later start begins at the gap: " + startOf(heard));
  check(rig.b.handle.markOf(0).sample == 576 && MicrophoneHub::Consumer().markOf(0).epoch == 0,
        "markOf names the consumer's sample 0; a detached consumer has no mark");
}

void restartFromAMark() {
  One rig;
  rig.running();
  rig.block();
  rig.a.handle.startFrom(rig.a.handle.markOf(240));
  rig.a.control({2, StreamControl::Start});
  rig.hub.tick(rig.now);
  const bool fromTheMark =
    !rig.a.events.empty() && !rig.a.events.back().samples.empty() && rig.a.events.back().samples[0] == 240;
  check(rig.a.last(3) == "Ended/1@480, Started/2, Audio/2@0+240" && fromTheMark,
        "a restart under a new epoch begins at its mark too: " + rig.a.last(3));
}

}

int main() {
  probeAlone();
  startAndAudio();
  noAnswerInsideControl();
  answerToCallbackWaits();
  twoConsumersShareOneStream();
  lingerAfterLastStop();
  leaseExpires();
  oneLeaseExpires();
  aStalledLoopEndsNoConsumer();
  silentStreamEnds();
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
  startFromOffTheGrid();
  startFromBeyondTheRing();
  startFromAMarkAcrossTheWrap();
  startFromAMarkAhead();
  startFromAnotherStream();
  gapClearsTheRing();
  restartFromAMark();
  return awtrix::test::finish("microphone hub");
}
