#include "../../support.h"
// The endpointer: EndpointTimer's decisions on exact voice patterns, then the voice activity model of
// assets/tc002/vad on speech of the English voice pack and on noise.
//
// tc002-endpointer-test <vad.tflite> <voice.atts>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/speech/SpeechModelVoice.h"
#include "platform/tc002/wake/Endpointer.h"
#include "platform/tc002/wake/ModelFiles.h"

using namespace awtrix::tc002::wake;
using awtrix::test::check;
using awtrix::test::require;

namespace {

constexpr uint64_t kMs = 16;
constexpr uint64_t kStepMs = 30;

struct Event {
  Endpoint kind;
  uint64_t ms;
};

// One verdict every 30 ms up to `ms`; voice(ms) gives it. The events with the millisecond they came at.
template <typename Voice>
std::vector<Event> run(EndpointTimer& timer, uint64_t ms, Voice voice) {
  std::vector<Event> out;
  for (uint64_t at = kStepMs; at <= ms; at += kStepMs) {
    const Endpoint kind = timer.step(voice(at), at * kMs);
    if (kind != Endpoint::None) out.push_back({kind, at});
  }
  return out;
}

void timerSilence() {
  EndpointTimer timer;
  check(timer.done() && timer.step(true, kMs) == Endpoint::None, "a timer that never started decides nothing");
  timer.start({800, 5000, 15000}, 0);
  const auto events = run(timer, 8000, [](uint64_t) { return false; });
  check(events.size() == 1 && events[0].kind == Endpoint::Silence && events[0].ms == 5010,
        "no voice: Silence once the wait is over");
  check(timer.done() && timer.step(true, 9000 * kMs) == Endpoint::None, "nothing after the end");
}

void timerSentence() {
  EndpointTimer timer;
  timer.start({800, 5000, 15000}, 0);
  const auto events = run(timer, 5000, [](uint64_t ms) { return ms >= 500 && ms < 2000; });
  require(events.size() == 2, "a sentence starts and ends");
  check(events[0].kind == Endpoint::Started && events[0].ms == 510, "it starts with the first voiced step");
  check(events[1].kind == Endpoint::Ended && events[1].ms == 2790, "it ends 800 ms after the last voiced step");
  check(timer.speechStart() == 510 * kMs && timer.speechEnd() == 1980 * kMs,
        "speech start and end are the first and the last voiced step");
}

void timerPause() {
  EndpointTimer timer;
  timer.start({800, 5000, 15000}, 0);
  const auto events =
    run(timer, 6000, [](uint64_t ms) { return (ms >= 500 && ms < 1500) || (ms >= 1900 && ms < 3000); });
  check(events.size() == 2 && events[0].kind == Endpoint::Started && events[1].kind == Endpoint::Ended &&
            events[1].ms == 3780,
        "a pause of 400 ms ends nothing");
}

void timerLimit() {
  EndpointTimer timer;
  timer.start({800, 5000, 15000}, 0);
  const auto talk = run(timer, 20000, [](uint64_t ms) { return ms >= 300; });
  check(talk.size() == 2 && talk[1].kind == Endpoint::Limit && talk[1].ms == 15000, "endless speech stops at the limit");
  timer.start({800, 5000, 2000}, 0);
  const auto quiet = run(timer, 6000, [](uint64_t) { return false; });
  check(quiet.size() == 1 && quiet[0].kind == Endpoint::Silence && quiet[0].ms == 2010,
        "a limit before the wait ends a silent recording as Silence");
  timer.start({0, 5000, 3000}, 0);
  const auto eager = run(timer, 4000, [](uint64_t ms) { return ms < 2000; });
  check(eager.size() == 2 && eager[0].kind == Endpoint::Started && eager[1].kind == Endpoint::Ended &&
            eager[1].ms == 2010,
        "without any silence to wait for, speech ends at its first step without voice, not before");
}

void timerFrom() {
  EndpointTimer timer;
  timer.start({800, 5000, 15000}, 1000 * kMs);
  const auto events = run(timer, 8000, [](uint64_t ms) { return ms < 700; });
  check(events.size() == 1 && events[0].kind == Endpoint::Silence && events[0].ms == 6000,
        "voice before the start does not count, and the wait counts from it");
}

using awtrix::speech::ModelVoice;

// Speech of the voice pack, from its 24 kHz down to 16 kHz: every second output sample lies between two
// input samples.
std::vector<int16_t> say(ModelVoice& voice, const char* text) {
  auto plan = std::make_unique<awtrix::speech::Plan>();
  require(voice.prepare(text, *plan), std::string("prepare: ") + text);
  const auto utterance = voice.start(*plan);
  require(utterance != nullptr, std::string("start: ") + text);
  std::vector<int16_t> high, block(512);
  while (const std::size_t n = utterance->render(block.data(), block.size()))
    high.insert(high.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n));
  std::vector<int16_t> out(high.size() * 2 / 3);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const std::size_t j = i * 3 / 2;
    out[i] = i % 2 ? static_cast<int16_t>((high[j] + high[j + 1]) / 2) : high[j];
  }
  return out;
}

// White noise at `dbfs` RMS from a fixed seed: the room the clock stands in.
std::vector<int16_t> noise(uint64_t ms, double dbfs, uint32_t seed) {
  std::vector<int16_t> out(ms * kMs);
  const double peak = 32768.0 * std::pow(10.0, dbfs / 20.0) * std::sqrt(3.0);
  uint32_t x = seed;
  for (int16_t& sample : out) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sample = static_cast<int16_t>(std::lround((static_cast<double>(x) / 4294967295.0 * 2.0 - 1.0) * peak));
  }
  return out;
}

std::vector<int16_t> join(std::initializer_list<std::vector<int16_t>> parts) {
  std::vector<int16_t> out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

struct Run {
  std::vector<Event> events;
  uint64_t startMs = 0, endMs = 0;
};

// The recording in blocks of `block` samples; event times in ms of the whole stream.
Run record(Endpointer& endpointer, const std::vector<int16_t>& pcm, std::size_t block,
           const EndpointSettings& settings, uint64_t fromMs = 0) {
  Run out;
  endpointer.start(settings, fromMs * kMs);
  for (std::size_t at = 0; at < pcm.size(); at += block) {
    const int16_t* samples = pcm.data() + at;
    std::size_t count = std::min(block, pcm.size() - at);
    for (Endpoint kind; (kind = endpointer.next(samples, count)) != Endpoint::None;)
      out.events.push_back({kind, endpointer.position() / kMs});
  }
  out.startMs = endpointer.speechStart() / kMs;
  out.endMs = endpointer.speechEnd() / kMs;
  return out;
}

void modelSentence(Endpointer& endpointer, ModelVoice& voice) {
  const std::vector<int16_t> lead = noise(1500, -60, 1);
  const std::vector<int16_t> words = say(voice, "What will the weather be like in Berlin tomorrow?");
  const std::vector<int16_t> pcm = join({lead, words, noise(2500, -60, 2)});
  const uint64_t begin = lead.size() / kMs, end = (lead.size() + words.size()) / kMs;
  const uint64_t stepMs = endpointer.stepMs();
  for (const std::size_t block : {std::size_t{1}, std::size_t{480}, pcm.size()}) {
    const Run r = record(endpointer, pcm, block, {800, 5000, 15000}, 1000);
    const std::string label = " (blocks of " + std::to_string(block) + ")";
    require(r.events.size() == 2 && r.events[0].kind == Endpoint::Started && r.events[1].kind == Endpoint::Ended,
            "a sentence starts and ends" + label);
    std::printf("sentence%s: speech %llu..%llu ms, heard %llu..%llu ms, ended at %llu ms\n", label.c_str(),
                static_cast<unsigned long long>(begin), static_cast<unsigned long long>(end),
                static_cast<unsigned long long>(r.startMs), static_cast<unsigned long long>(r.endMs),
                static_cast<unsigned long long>(r.events[1].ms));
    check(r.startMs >= begin && r.startMs <= begin + 800, "speech is heard within 800 ms of its start" + label);
    check(r.endMs + 200 >= end && r.endMs <= end + 700, "the last voice lies near the end of the speech" + label);
    check(r.events[1].ms >= r.endMs + 800 && r.events[1].ms <= r.endMs + 800 + stepMs,
          "the recording ends 800 ms after the last voice" + label);
  }
}

void modelPause(Endpointer& endpointer, ModelVoice& voice) {
  const std::vector<int16_t> first = say(voice, "Turn on the light."), second = say(voice, "In the kitchen, please.");
  const std::vector<int16_t> pcm =
    join({noise(1500, -60, 3), first, noise(400, -60, 4), second, noise(2500, -60, 5)});
  const uint64_t end = (pcm.size() - 2500 * kMs) / kMs;
  const Run r = record(endpointer, pcm, 480, {800, 5000, 15000}, 1000);
  check(r.events.size() == 2 && r.events[0].kind == Endpoint::Started && r.events[1].kind == Endpoint::Ended &&
            r.endMs + 200 >= end,
        "a pause of 400 ms between two phrases ends nothing");
}

void modelNoise(Endpointer& endpointer) {
  const Run r = record(endpointer, noise(8000, -40, 6), 480, {800, 5000, 15000});
  check(r.events.size() == 1 && r.events[0].kind == Endpoint::Silence && r.events[0].ms >= 5000,
        "room noise at -40 dBFS is no speech");
  const Run loud = record(endpointer, noise(3000, -20, 7), 480, {800, 2000, 15000});
  check(loud.events.size() == 1 && loud.events[0].kind == Endpoint::Silence,
        "a fresh stream on loud noise at -20 dBFS starts no speech, with no warm-up");
}

}  // namespace

int main(int argc, char** argv) {
  require(argc == 3, "usage: tc002-endpointer-test <vad.tflite> <voice.atts>");
  timerSilence();
  timerSentence();
  timerPause();
  timerLimit();
  timerFrom();

  std::string error;
  ModelFiles files;
  require(readModelFiles(argv[1], files, error), error);
  check(files.cutoff == 0.5 && files.window == 5, "the manifest's settings under micro");
  Endpointer endpointer;
  check(!endpointer.load(files.model, 1.5f, 5, error) && !error.empty(), "a cutoff above 1 is refused");
  require(endpointer.load(files.model, static_cast<float>(files.cutoff), static_cast<std::size_t>(files.window), error),
          error);
  const std::shared_ptr<const awtrix::speech::SpeechModel> model = awtrix::speech::SpeechModel::load(argv[2], error);
  require(model != nullptr, error);
  const auto voice = ModelVoice::create(model, error);
  require(voice != nullptr, error);
  modelSentence(endpointer, *voice);
  modelPause(endpointer, *voice);
  modelNoise(endpointer);
  return awtrix::test::finish("tc002-endpointer");
}
