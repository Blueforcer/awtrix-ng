#include "../../support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "platform/tc002/wake/WakeListener.h"

using namespace awtrix;
using awtrix::test::check;
using tc002::StreamControl;
using tc002::StreamEvent;

namespace {
namespace fs = std::filesystem;

void writeFile(const fs::path& path, const std::string& bytes) {
  std::ofstream(path, std::ios::binary) << bytes;
}

struct Rig {
  std::vector<StreamControl> sent;
  std::unique_ptr<tc002::wake::WakeListener> listener;
  std::string error;

  explicit Rig(const fs::path& model) {
    listener = tc002::wake::WakeListener::open(model.string(), [this](StreamControl control) {
      sent.push_back(control);
      return true;
    }, error);
  }
  StreamEvent event(StreamEvent::Kind kind, int epoch) const {
    StreamEvent e;
    e.epoch = epoch;
    e.kind = kind;
    e.available = true;
    return e;
  }
  int listen(int64_t now) {
    listener->tick(now, true);
    const int epoch = sent.back().epoch;
    listener->receive(event(StreamEvent::Kind::Support, epoch), now);
    listener->receive(event(StreamEvent::Kind::Started, epoch), now);
    return epoch;
  }
  // Paced like the stream, so the worker queue never overflows. The detections the ticks reported.
  std::vector<tc002::wake::WakeHeard> feed(int epoch, uint32_t& position, int blocks, int64_t& now) {
    std::vector<tc002::wake::WakeHeard> heard;
    for (int i = 0; i < blocks; ++i) {
      now += 30;
      auto e = event(StreamEvent::Kind::Audio, epoch);
      e.firstSample = position;
      e.samples.assign(480, 0);
      position += 480;
      listener->receive(e, now);
      if (const auto h = listener->tick(now, true)) heard.push_back(*h);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return heard;
  }
  unsigned count(StreamControl::Operation operation) const {
    unsigned n = 0;
    for (const auto& c : sent) n += c.operation == operation;
    return n;
  }
};
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fputs("usage: tc002-wake-listener-test <fixture dir>\n", stderr);
    return 2;
  }
  const fs::path dir = fs::temp_directory_path() / ("wake-listener-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path model = dir / "word.tflite", settings = dir / "word.json";
  fs::copy_file(fs::path(argv[1]) / "smoke.tflite", model);

  {
    Rig rig(model);
    check(!rig.listener && rig.error.find("word.json") != std::string::npos, "the settings file is required");
    writeFile(settings, R"({"probability_cutoff":0.9})");
    Rig partial(model);
    check(!partial.listener && partial.error.find("sliding_window_size") != std::string::npos,
          "the settings need cutoff and window");
    writeFile(settings, R"({"probability_cutoff":0.9,"sliding_window_size":0})");
    Rig zero(model);
    check(!zero.listener && zero.error.find("window") != std::string::npos, "a window of 0 is refused");
    writeFile(settings, R"({"probability_cutoff":1.5,"sliding_window_size":3})");
    Rig high(model);
    check(!high.listener && high.error.find("cutoff") != std::string::npos, "a cutoff above 1 is refused");
    writeFile(dir / "broken.tflite", "not a model");
    writeFile(dir / "broken.json", R"({"probability_cutoff":0.9,"sliding_window_size":3})");
    Rig broken(dir / "broken.tflite");
    check(!broken.listener && !broken.error.empty(), "a broken model is refused: " + broken.error);
    writeFile(settings, R"({"type":"micro","micro":{"probability_cutoff":0.5,"sliding_window_size":5,"feature_step_size":10}})");
    check(Rig(model).listener != nullptr, "an ESPHome manifest's settings under micro are read");
    writeFile(settings, R"({"micro":{"probability_cutoff":0.5,"sliding_window_size":5,"feature_step_size":20}})");
    Rig coarse(model);
    check(!coarse.listener && coarse.error.find("feature_step_size") != std::string::npos,
          "a model on another feature step is refused: " + coarse.error);
    writeFile(settings, R"({"micro":{"probability_cutoff":0.5}})");
    Rig nested(model);
    check(!nested.listener && nested.error.find("needs probability_cutoff and sliding_window_size") != std::string::npos,
          "an incomplete micro block is refused: " + nested.error);
  }

  // A cutoff of 0 fires on any audio once the warm-up has passed, then once per cool-off.
  writeFile(settings, R"({"ID":"word","probability_cutoff":0,"sliding_window_size":1})");
  Rig rig(model);
  awtrix::test::require(rig.listener != nullptr, "listener with the smoke model: " + rig.error);
  int64_t now = 1000;
  check(!rig.listener->tick(now, false).has_value() && rig.sent.empty(), "not wanted: no microphone traffic");

  rig.listener->tick(now, true);
  awtrix::test::require(rig.sent.size() == 1 && rig.sent[0].operation == StreamControl::Probe, "probe first");
  const int epoch = rig.sent[0].epoch;
  rig.listener->receive(rig.event(StreamEvent::Kind::Support, epoch), now);
  check(rig.sent.size() == 2 && rig.sent[1].operation == StreamControl::Start, "an available stream starts");
  rig.listener->receive(rig.event(StreamEvent::Kind::Started, epoch), now);

  uint32_t position = 0;
  check(rig.feed(epoch, position, 30, now).empty(), "nothing fires within the first second");
  auto heard = rig.feed(epoch, position, 40, now);
  for (int i = 0; i < 50 && heard.empty(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (const auto h = rig.listener->tick(now, true)) heard.push_back(*h);
  }
  check(!heard.empty() && heard.size() <= 2, "a detection is reported once per cool-off: " + std::to_string(heard.size()));
  check(!heard.empty() && heard[0].sample >= 1000 * 16 && heard[0].sample <= position,
        "a detection names its position in the listener's stream");
  const unsigned keepalives = rig.count(StreamControl::Keepalive);
  check(keepalives >= 12 && keepalives <= 15, "keepalive about every 150 ms: " + std::to_string(keepalives));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  rig.listener->tick(now, true);
  position += 96;
  const uint32_t jumped = position;
  check(rig.feed(epoch, position, 25, now).empty(), "a jump in sample position starts the warm-up over");

  const auto after = rig.feed(epoch, position, 60, now);
  check(!after.empty() && after[0].sample >= jumped + 1000 * 16 && after[0].sample <= position,
        "after a jump, positions continue in the listener's stream, not from 0");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(!rig.listener->tick(now, false).has_value(), "Voice busy: a pending detection is dropped");
  check(rig.sent.back().operation == StreamControl::Stop, "Voice busy: the stream is stopped");
  rig.listener->receive(rig.event(StreamEvent::Kind::Ended, epoch), now);
  const auto before = rig.sent.size();
  rig.listener->tick(now, false);
  check(rig.sent.size() == before, "nothing starts while Voice is busy");

  const int second = rig.listen(now);
  check(second != epoch && rig.sent[before].operation == StreamControl::Probe,
        "listening resumes with a new epoch after Voice");
  auto failed = rig.event(StreamEvent::Kind::Ended, second);
  failed.error = "microphone UART frame lost or corrupt";
  rig.listener->receive(failed, now);
  const auto afterFailure = rig.sent.size();
  rig.listener->tick(now + 900, true);
  check(rig.sent.size() == afterFailure, "a failed stream waits before it restarts");
  rig.listener->tick(now + 1000, true);
  check(rig.sent.size() == afterFailure + 1 && rig.sent.back().operation == StreamControl::Probe,
        "a failed stream restarts after 1 s");

  auto unavailable = rig.event(StreamEvent::Kind::Support, rig.sent.back().epoch);
  unavailable.available = false;
  rig.listener->receive(unavailable, now + 1000);
  rig.listener->receive(rig.event(StreamEvent::Kind::Started, rig.sent.back().epoch), now + 1000);
  const auto afterUnavailable = rig.sent.size();
  rig.listener->tick(now + 30000, true);
  check(rig.sent.size() == afterUnavailable, "late events of an old epoch are ignored; it asks again later");
  rig.listener->tick(now + 31000, true);
  check(rig.sent.size() == afterUnavailable + 1 && rig.sent.back().operation == StreamControl::Probe,
        "an MCU without the stream is asked again after 30 s");

  rig.listener->receive(rig.event(StreamEvent::Kind::Support, rig.sent.back().epoch), now + 31000);
  check(rig.sent.back().operation == StreamControl::Start, "an available stream starts again");
  rig.listener->cancel();
  check(rig.sent.back().operation == StreamControl::Stop, "cancel stops the stream");

  rig.listener->tick(now + 32000, true);
  const int probing = rig.sent.back().epoch;
  check(rig.sent.back().operation == StreamControl::Probe, "listening again probes");
  rig.listener->tick(now + 32000, false);
  check(rig.sent.back().operation == StreamControl::Stop && rig.sent.back().epoch == probing,
        "a probe no longer wanted is withdrawn");
  rig.listener->receive(rig.event(StreamEvent::Kind::Support, probing), now + 32000);
  check(rig.sent.back().operation == StreamControl::Stop, "and its late answer starts nothing");

  rig.listener.reset();
  fs::remove_all(dir);
  return awtrix::test::finish("wake listener");
}
