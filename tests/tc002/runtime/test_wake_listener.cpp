#include "../../support.h"
#include "../../ManualExecutor.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "platform/tc002/wake/WakeFrontend.h"
#include "platform/tc002/wake/WakeListener.h"

using namespace awtrix;
using awtrix::test::check;
using awtrix::test::require;
using tc002::StreamControl;
using tc002::StreamEvent;
using tc002::wake::WakeHeard;
using tc002::wake::WakeListener;
using tc002::wake::WakeSensitivity;
using Listen = WakeListener::Listen;

namespace {
namespace fs = std::filesystem;

constexpr uint32_t kSecond = tc002::wake::WakeFrontend::kSampleRate;
constexpr int64_t kFrameMs = 30;

void writeFile(const fs::path& path, const std::string& bytes) { std::ofstream(path, std::ios::binary) << bytes; }

struct Rig {
  std::vector<StreamControl> sent;
  test::ManualExecutor* worker = nullptr;
  std::unique_ptr<WakeListener> listener;
  std::string error;
  int64_t now = 1000;
  uint32_t position = 0;

  explicit Rig(const fs::path& model) {
    auto executor = std::make_unique<test::ManualExecutor>();
    worker = executor.get();
    listener = WakeListener::open(model.string(), [this](StreamControl control) {
      sent.push_back(control);
      return true;
    }, std::move(executor), error);
  }
  const StreamControl& last() const {
    require(!sent.empty(), "the listener has sent a control");
    return sent.back();
  }
  StreamEvent event(StreamEvent::Kind kind, bool available = true) const {
    StreamEvent e;
    e.epoch = last().epoch;
    e.kind = kind;
    e.available = available;
    return e;
  }
  void answer(StreamEvent::Kind kind, bool available = true) { listener->receive(event(kind, available), now); }
  void fail(const char* error) {
    StreamEvent e = event(StreamEvent::Kind::Ended);
    e.error = error;
    listener->receive(e, now);
  }
  int listen() {
    listener->tick(now, Listen::On);
    require(last().operation == StreamControl::Probe, "listening probes the microphone");
    answer(StreamEvent::Kind::Support);
    answer(StreamEvent::Kind::Started);
    position = 0;
    return last().epoch;
  }
  void block() {
    StreamEvent e = event(StreamEvent::Kind::Audio);
    e.firstSample = position;
    e.samples.assign(tc002::kStreamBlockSamples, 0);
    position += tc002::kStreamBlockSamples;
    listener->receive(e, now);
  }
  std::vector<WakeHeard> feed(int blocks, Listen listen = Listen::On) {
    std::vector<WakeHeard> heard;
    for (int i = 0; i < blocks; ++i) {
      now += kFrameMs;
      block();
      worker->run();
      if (const auto h = listener->tick(now, listen)) heard.push_back(*h);
    }
    return heard;
  }
  unsigned count(StreamControl::Operation operation) const {
    unsigned n = 0;
    for (const auto& c : sent) n += c.operation == operation;
    return n;
  }
};

struct Fixture {
  fs::path dir, model, settings;

  explicit Fixture(const fs::path& shipped)
      : dir(fs::temp_directory_path() / ("wake-listener-" + std::to_string(::getpid()))),
        model(dir / "word.tflite"),
        settings(dir / "word.json") {
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(shipped, model);
  }
  ~Fixture() { fs::remove_all(dir); }
  void firesAlways() const { writeFile(settings, R"({"ID":"word","probability_cutoff":0,"sliding_window_size":1})"); }
};

struct Firing : Rig {
  explicit Firing(const Fixture& f) : Rig((f.firesAlways(), f.model)) { require(listener != nullptr, error); }
};

std::string positions(const std::vector<WakeHeard>& heard) {
  std::string out;
  for (const WakeHeard& h : heard) out += std::to_string(h.sample) + " ";
  return out;
}

bool apartByTheCooloff(const std::vector<WakeHeard>& heard) {
  for (std::size_t i = 1; i < heard.size(); ++i)
    if (heard[i].sample - heard[i - 1].sample < kSecond) return false;
  return true;
}

void settingsAndModelsAreChecked(const Fixture& f) {
  Rig missing(f.model);
  check(!missing.listener && missing.error.find("word.json") != std::string::npos, "the settings file is required");
  writeFile(f.settings, R"({"probability_cutoff":0.9})");
  Rig partial(f.model);
  check(!partial.listener && partial.error.find("sliding_window_size") != std::string::npos,
        "the settings need cutoff and window");
  writeFile(f.settings, R"({"probability_cutoff":0.9,"sliding_window_size":0})");
  Rig zero(f.model);
  check(!zero.listener && zero.error == f.settings.string() + ": the window must be within 1..64",
        "a window of 0 is refused: " + zero.error);
  writeFile(f.settings, R"({"probability_cutoff":0.9,"sliding_window_size":-3})");
  Rig negative(f.model);
  check(!negative.listener && negative.error.find("window") != std::string::npos,
        "a negative window is refused: " + negative.error);
  writeFile(f.settings, R"({"probability_cutoff":1.5,"sliding_window_size":3})");
  Rig high(f.model);
  check(!high.listener && high.error.find("cutoff") != std::string::npos, "a cutoff above 1 is refused: " + high.error);
  writeFile(f.dir / "broken.tflite", "not a model");
  writeFile(f.dir / "broken.json", R"({"probability_cutoff":0.9,"sliding_window_size":3})");
  Rig broken(f.dir / "broken.tflite");
  check(!broken.listener && !broken.error.empty(), "a broken model is refused: " + broken.error);
  writeFile(f.settings,
            R"({"type":"micro","micro":{"probability_cutoff":0.5,"sliding_window_size":5,"feature_step_size":10}})");
  check(Rig(f.model).listener != nullptr, "an ESPHome manifest's settings under micro are read");
  writeFile(f.settings, R"({"micro":{"probability_cutoff":0.5,"sliding_window_size":5,"feature_step_size":20}})");
  Rig coarse(f.model);
  check(!coarse.listener && coarse.error.find("feature_step_size") != std::string::npos,
        "a model on another feature step is refused: " + coarse.error);
  writeFile(f.settings, R"({"micro":{"probability_cutoff":0.5}})");
  Rig nested(f.model);
  check(!nested.listener && nested.error.find("needs probability_cutoff and sliding_window_size") != std::string::npos,
        "an incomplete micro block is refused: " + nested.error);
}

void listensWhenAsked(const Fixture& f) {
  Firing rig(f);
  check(!rig.listener->tick(rig.now, Listen::Off) && rig.sent.empty(), "not wanted: no microphone traffic");
  rig.listener->tick(rig.now, Listen::On);
  check(rig.sent.size() == 1 && rig.last().operation == StreamControl::Probe, "wanted: it probes first");
  rig.answer(StreamEvent::Kind::Support);
  check(rig.sent.size() == 2 && rig.last().operation == StreamControl::Start, "an available stream starts");
  rig.answer(StreamEvent::Kind::Started);
  const auto heard = rig.feed(30);
  check(heard.empty(), "nothing fires within the first second: " + positions(heard));
  const unsigned keepalives = rig.count(StreamControl::Keepalive);
  check(keepalives == 30 * kFrameMs / tc002::kStreamKeepaliveMs,
        "a keepalive every 150 ms: " + std::to_string(keepalives));
}

void detectionsKeepTheirCooloff(const Fixture& f) {
  Firing rig(f);
  rig.listen();
  const auto heard = rig.feed(110);
  check(heard.size() >= 2 && heard[0].sample >= kSecond && heard.back().sample <= rig.position &&
            apartByTheCooloff(heard),
        "detections come after the warm-up, a cool-off apart, at their place in the stream: " + positions(heard));
}

void aJumpStartsTheWarmUpOver(const Fixture& f) {
  Firing rig(f);
  rig.listen();
  rig.feed(40);
  rig.position += 96;
  const uint32_t jumped = rig.position;
  const auto during = rig.feed(30);
  check(during.empty(), "a jump in sample position starts the warm-up over: " + positions(during));
  const auto after = rig.feed(40);
  check(!after.empty() && after[0].sample >= jumped + kSecond && after[0].sample <= rig.position,
        "after a jump, positions continue in the listener's stream: " + positions(after));
}

void aFullQueueStartsTheWarmUpOver(const Fixture& f) {
  Firing rig(f);
  rig.listen();
  rig.worker->run();
  for (std::size_t i = 0; i < WakeListener::kQueueBlocks; ++i) rig.block();
  const uint32_t dropped = rig.position;
  rig.block();
  check(rig.worker->pending() == 1, "the blocks wait for one run of the worker");
  rig.worker->run();
  const auto heard = rig.feed(70);
  check(!heard.empty() && heard[0].sample >= dropped + kSecond,
        "a block that finds the queue full drops the waiting ones and starts the detector over from itself: " +
            positions(heard));
}

void aWorkerThatRefusesIsAskedAgain(const Fixture& f) {
  Firing rig(f);
  rig.worker->refusing = true;
  rig.listen();
  rig.block();
  check(rig.worker->pending() == 0, "a worker that cannot start leaves the blocks waiting");
  rig.worker->refusing = false;
  rig.block();
  check(rig.worker->pending() == 1, "the next block asks it again");
  rig.worker->run();
  check(!rig.feed(70).empty(), "and the listener hears as before");
}

void mutedKeepsTheStream(const Fixture& f) {
  Firing rig(f);
  rig.listen();
  rig.feed(40);
  const std::size_t controls = rig.sent.size();
  const auto muted = rig.feed(60, Listen::Muted);
  check(muted.empty(), "muted: nothing is reported: " + positions(muted));
  unsigned others = 0;
  for (std::size_t i = controls; i < rig.sent.size(); ++i) others += rig.sent[i].operation != StreamControl::Keepalive;
  check(others == 0 && rig.sent.size() > controls, "muted: the stream stays and is kept alive");
  const uint32_t unmuted = rig.position;
  const auto resumed = rig.feed(40);
  check(!resumed.empty() && resumed[0].sample > unmuted && rig.count(StreamControl::Probe) == 1,
        "unmuted: detections resume on the same stream, none from audio heard while muted: " + positions(resumed));
}

void offStopsTheStream(const Fixture& f) {
  Firing rig(f);
  const int epoch = rig.listen();
  for (int i = 0; i < 40; ++i) {
    rig.now += kFrameMs;
    rig.block();
  }
  rig.worker->run();
  check(!rig.listener->tick(rig.now, Listen::Off), "Voice busy: a pending detection is dropped");
  check(rig.last().operation == StreamControl::Stop && rig.last().epoch == epoch, "Voice busy: the stream is stopped");
  rig.answer(StreamEvent::Kind::Ended);
  const std::size_t before = rig.sent.size();
  rig.listener->tick(rig.now, Listen::Off);
  check(rig.sent.size() == before, "nothing starts while Voice is busy");
  rig.listener->tick(rig.now, Listen::On);
  check(rig.last().operation == StreamControl::Probe && rig.last().epoch != epoch,
        "listening resumes with a new epoch after Voice");
}

void theModelLeavesMemoryWhileOff(const Fixture& f) {
  const fs::path moved = f.dir / "moved.tflite";
  const auto withoutTheFile = [&](auto act) {
    fs::rename(f.model, moved);
    act();
    fs::rename(moved, f.model);
  };
  Firing rig(f);
  rig.listener->tick(rig.now, Listen::On);
  rig.listener->cancel();
  rig.sent.clear();
  rig.now += WakeListener::kModelOffMs - 1;
  rig.listener->tick(rig.now, Listen::Off);
  withoutTheFile([&] {
    rig.listener->tick(rig.now, Listen::On);
    check(!rig.sent.empty() && rig.last().operation == StreamControl::Probe,
          "off for a shorter time it keeps the model, the file is not read");
  });
  rig.listener->cancel();
  rig.sent.clear();

  rig.now += WakeListener::kModelOffMs;
  rig.listener->tick(rig.now, Listen::Off);
  withoutTheFile([&] {
    rig.listener->tick(rig.now, Listen::On);
    check(rig.sent.empty(), "after a minute off the model is gone, and without its file nothing starts");
    rig.now += 30000;
    rig.listener->tick(rig.now, Listen::On);
    check(rig.sent.empty(), "it stays that way");
  });
  rig.now += 30000;
  rig.listener->tick(rig.now, Listen::On);
  check(!rig.sent.empty() && rig.last().operation == StreamControl::Probe, "with the file back it loads and listens");
  rig.answer(StreamEvent::Kind::Support);
  rig.answer(StreamEvent::Kind::Started);
  rig.position = 0;
  rig.feed(40);
  check(!rig.feed(40).empty(), "and hears with the new model");

  Firing pending(f);
  pending.listen();
  pending.block();
  pending.listener->cancel();
  pending.now += 2 * WakeListener::kModelOffMs;
  pending.listener->tick(pending.now, Listen::Off);
  pending.worker->run();
  check(pending.worker->pending() == 0, "a block still queued finds the model");
}

void theLevelApplies(const Fixture& f) {
  writeFile(f.settings, R"({"probability_cutoff":0.44,"sliding_window_size":1})");
  Rig rig(f.model);
  require(rig.listener != nullptr, rig.error);
  rig.listen();
  check(rig.feed(100).empty(), "silence stays under the model's cutoff");
  rig.listener->setSensitivity(WakeSensitivity::High);
  check(!rig.feed(60).empty(), "on high it fires, the cutoff of this model drops to 0");
  check(rig.count(StreamControl::Probe) == 1 && rig.count(StreamControl::Stop) == 0, "on the same stream");
  rig.listener->setSensitivity(WakeSensitivity::Normal);
  check(rig.feed(60).empty(), "back on normal it stays silent");
  rig.listener->setSensitivity(WakeSensitivity::High);
  rig.listener->cancel();
  rig.now += WakeListener::kModelOffMs;
  rig.listener->tick(rig.now, Listen::Off);
  rig.listen();
  check(!rig.feed(60).empty(), "a model loaded again takes the level");
}

void aStopWithoutAnswerEnds(const Fixture& f) {
  Firing rig(f);
  const int epoch = rig.listen();
  rig.listener->tick(rig.now, Listen::Off);
  require(rig.last().operation == StreamControl::Stop, "the stream is asked to stop");
  rig.listener->tick(rig.now + tc002::kStreamAnswerMs - 1, Listen::On);
  check(rig.last().operation == StreamControl::Stop, "a stop waits for its answer");
  rig.now += tc002::kStreamAnswerMs;
  rig.listener->tick(rig.now, Listen::On);
  check(rig.last().operation == StreamControl::Stop, "after 2 s the stop counts as done");
  rig.listener->tick(rig.now, Listen::On);
  check(rig.last().operation == StreamControl::Probe && rig.last().epoch != epoch,
        "and the next frame probes again");
}

void failuresBackOff(const Fixture& f) {
  Firing rig(f);
  const auto retriesAfter = [&rig](int64_t ms) {
    const std::size_t before = rig.sent.size();
    rig.listener->tick(rig.now + ms - 1, Listen::On);
    const bool waited = rig.sent.size() == before;
    rig.now += ms;
    rig.listener->tick(rig.now, Listen::On);
    return waited && rig.sent.size() == before + 1 && rig.last().operation == StreamControl::Probe;
  };
  rig.listener->tick(rig.now, Listen::On);
  rig.fail("microphone UART frame lost or corrupt");
  bool steps = true;
  for (const int64_t ms : {1000, 2000, 5000, 10000, 30000}) {
    steps = steps && retriesAfter(ms);
    rig.fail("microphone UART frame lost or corrupt");
  }
  check(steps && retriesAfter(30000), "failed streams retry after 1, 2, 5, 10 and then every 30 s");
  rig.answer(StreamEvent::Kind::Support);
  rig.answer(StreamEvent::Kind::Started);
  rig.fail("microphone UART frame lost or corrupt");
  check(retriesAfter(1000), "a stream that started resets the back-off to 1 s");
  rig.now += tc002::kStreamAnswerMs;
  rig.listener->tick(rig.now, Listen::On);
  check(retriesAfter(2000), "a probe without an answer counts as a failure");
}

void anUnavailableStreamWaits(const Fixture& f) {
  Firing rig(f);
  rig.listener->tick(rig.now, Listen::On);
  const int epoch = rig.last().epoch;
  rig.answer(StreamEvent::Kind::Support, false);
  StreamEvent late = rig.event(StreamEvent::Kind::Started);
  late.epoch = epoch;
  rig.listener->receive(late, rig.now);
  const std::size_t before = rig.sent.size();
  rig.listener->tick(rig.now + 29999, Listen::On);
  check(rig.sent.size() == before, "late events of an old epoch are ignored; it asks again later");
  rig.listener->tick(rig.now + 30000, Listen::On);
  check(rig.sent.size() == before + 1 && rig.last().operation == StreamControl::Probe,
        "an MCU without the stream is asked again after 30 s");
}

void theShippedWakeWordOpens(const fs::path& model) {
  Rig rig(model);
  check(rig.listener != nullptr, "the wake word the release ships opens with its settings: " + rig.error);
}

void cancelAndWithdraw(const Fixture& f) {
  Firing rig(f);
  rig.listen();
  rig.listener->cancel();
  check(rig.last().operation == StreamControl::Stop, "cancel stops the stream");
  rig.listener->tick(rig.now, Listen::On);
  const int probing = rig.last().epoch;
  check(rig.last().operation == StreamControl::Probe, "listening again probes");
  rig.listener->tick(rig.now, Listen::Off);
  check(rig.last().operation == StreamControl::Stop && rig.last().epoch == probing,
        "a probe no longer wanted is withdrawn");
  rig.answer(StreamEvent::Kind::Support);
  check(rig.last().operation == StreamControl::Stop, "and its late answer starts nothing");
}

}

int main(int argc, char** argv) {
  require(argc == 2, "usage: tc002-wake-listener-test <shipped wake word model>");
  const Fixture fixture(argv[1]);
  settingsAndModelsAreChecked(fixture);
  listensWhenAsked(fixture);
  detectionsKeepTheirCooloff(fixture);
  aJumpStartsTheWarmUpOver(fixture);
  aFullQueueStartsTheWarmUpOver(fixture);
  aWorkerThatRefusesIsAskedAgain(fixture);
  mutedKeepsTheStream(fixture);
  offStopsTheStream(fixture);
  theModelLeavesMemoryWhileOff(fixture);
  theLevelApplies(fixture);
  aStopWithoutAnswerEnds(fixture);
  failuresBackOff(fixture);
  anUnavailableStreamWaits(fixture);
  cancelAndWithdraw(fixture);
  theShippedWakeWordOpens(argv[1]);
  return awtrix::test::finish("wake listener");
}
