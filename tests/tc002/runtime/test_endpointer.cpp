#include "../../support.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/speech/SpeechModelVoice.h"
#include "platform/tc002/wake/Endpointer.h"
#include "platform/tc002/wake/ModelFiles.h"
#include "speech_samples.h"

using namespace awtrix::tc002::wake;
using awtrix::test::check;
using awtrix::test::require;

namespace {

using namespace awtrix::test::speech;

constexpr uint64_t kMs = 16;
constexpr uint64_t kStepMs = 30;

struct Event {
  Endpoint kind;
  uint64_t ms;
};

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
  timer.start({800, 5000, 15000});
  const auto events = run(timer, 8000, [](uint64_t) { return false; });
  check(events.size() == 1 && events[0].kind == Endpoint::Silence && events[0].ms == 5010,
        "no voice: Silence once the wait is over");
  check(timer.done() && timer.step(true, 9000 * kMs) == Endpoint::None, "nothing after the end");
}

void timerSentence() {
  EndpointTimer timer;
  timer.start({800, 5000, 15000});
  const auto events = run(timer, 5000, [](uint64_t ms) { return ms >= 500 && ms < 2000; });
  require(events.size() == 2, "a sentence starts and ends");
  check(events[0].kind == Endpoint::Started && events[0].ms == 510, "it starts with the first voiced step");
  check(events[1].kind == Endpoint::Ended && events[1].ms == 2790, "it ends 800 ms after the last voiced step");
}

void timerPause() {
  EndpointTimer timer;
  timer.start({800, 5000, 15000});
  const auto events =
    run(timer, 6000, [](uint64_t ms) { return (ms >= 500 && ms < 1500) || (ms >= 1900 && ms < 3000); });
  check(events.size() == 2 && events[0].kind == Endpoint::Started && events[1].kind == Endpoint::Ended &&
            events[1].ms == 3780,
        "a pause of 400 ms ends nothing");
}

void timerLimit() {
  EndpointTimer timer;
  timer.start({800, 5000, 15000});
  const auto talk = run(timer, 20000, [](uint64_t ms) { return ms >= 300; });
  check(talk.size() == 2 && talk[1].kind == Endpoint::Limit && talk[1].ms == 15000, "endless speech stops at the limit");
  timer.start({800, 5000, 2000});
  const auto quiet = run(timer, 6000, [](uint64_t) { return false; });
  check(quiet.size() == 1 && quiet[0].kind == Endpoint::Silence && quiet[0].ms == 2010,
        "a limit before the wait ends a silent recording as Silence");
  timer.start({0, 5000, 3000});
  const auto eager = run(timer, 4000, [](uint64_t ms) { return ms < 2000; });
  check(eager.size() == 2 && eager[0].kind == Endpoint::Started && eager[1].kind == Endpoint::Ended &&
            eager[1].ms == 2010,
        "without any silence to wait for, speech ends at its first step without voice, not before");
}

using awtrix::speech::ModelVoice;

std::vector<Event> record(Endpointer& endpointer, const std::vector<int16_t>& pcm, std::size_t block,
                          const EndpointSettings& settings) {
  std::vector<Event> out;
  endpointer.start(settings);
  for (std::size_t at = 0; at < pcm.size(); at += block) {
    const int16_t* samples = pcm.data() + at;
    std::size_t count = std::min(block, pcm.size() - at);
    for (Endpoint kind; (kind = endpointer.next(samples, count)) != Endpoint::None;)
      out.push_back({kind, static_cast<uint64_t>(samples - pcm.data()) / kMs});
  }
  return out;
}

void modelSentence(Endpointer& endpointer, ModelVoice& voice) {
  const std::vector<int16_t> lead = noise(1500, -60, 1);
  const std::vector<int16_t> words = say(voice, "What will the weather be like in Berlin tomorrow?");
  const std::vector<int16_t> pcm = join({lead, words, noise(2500, -60, 2)});
  const uint64_t begin = lead.size() / kMs, end = (lead.size() + words.size()) / kMs;
  const uint64_t stepMs = endpointer.stepMs();
  for (const std::size_t block : {std::size_t{1}, std::size_t{480}, pcm.size()}) {
    const std::vector<Event> events = record(endpointer, pcm, block, {800, 5000, 15000});
    const std::string label = " (blocks of " + std::to_string(block) + ")";
    require(events.size() == 2 && events[0].kind == Endpoint::Started && events[1].kind == Endpoint::Ended,
            "a sentence starts and ends" + label);
    std::printf("sentence%s: speech %llu..%llu ms, heard from %llu ms, ended at %llu ms\n", label.c_str(),
                static_cast<unsigned long long>(begin), static_cast<unsigned long long>(end),
                static_cast<unsigned long long>(events[0].ms), static_cast<unsigned long long>(events[1].ms));
    check(events[0].ms >= begin && events[0].ms <= begin + 800, "speech is heard within 800 ms of its start" + label);
    check(events[1].ms + 200 >= end + 800 && events[1].ms <= end + 700 + 800 + stepMs,
          "the recording ends 800 ms after the last voice, which lies near the end of the speech" + label);
  }
}

void modelPause(Endpointer& endpointer, ModelVoice& voice) {
  const std::vector<int16_t> first = say(voice, "Turn on the light."), second = say(voice, "In the kitchen, please.");
  const std::vector<int16_t> pcm =
    join({noise(1500, -60, 3), first, noise(400, -60, 4), second, noise(2500, -60, 5)});
  const uint64_t end = (pcm.size() - 2500 * kMs) / kMs;
  const std::vector<Event> events = record(endpointer, pcm, 480, {800, 5000, 15000});
  check(events.size() == 2 && events[0].kind == Endpoint::Started && events[1].kind == Endpoint::Ended &&
            events[1].ms + 200 >= end + 800,
        "a pause of 400 ms between two phrases ends nothing");
}

void modelNoise(Endpointer& endpointer) {
  const std::vector<Event> quiet = record(endpointer, noise(8000, -40, 6), 480, {800, 5000, 15000});
  check(quiet.size() == 1 && quiet[0].kind == Endpoint::Silence && quiet[0].ms >= 5000,
        "room noise at -40 dBFS is no speech");
  const std::vector<Event> loud = record(endpointer, noise(3000, -20, 7), 480, {800, 2000, 15000});
  check(loud.size() == 1 && loud[0].kind == Endpoint::Silence,
        "a fresh stream on loud noise at -20 dBFS starts no speech, with no warm-up");
}

}

int main(int argc, char** argv) {
  require(argc == 3, "usage: tc002-endpointer-test <vad.tflite> <voice.atts>");
  timerSilence();
  timerSentence();
  timerPause();
  timerLimit();

  std::string error;
  ModelFiles files;
  require(readModelFiles(argv[1], files, error), error);
  check(files.cutoff == 0.5 && files.window == 5, "the manifest's settings under micro");
  Endpointer endpointer;
  require(endpointer.load(files.model->bytes(), error), error);
  check(!endpointer.configure(1.5f, 5, error) && error == "the cutoff must be within 0..1",
        "a cutoff above 1 is refused");
  check(!endpointer.configure(0.5f, 65, error) && error == "the window must be within 1..64",
        "a window above 64 is refused");
  require(endpointer.configure(static_cast<float>(files.cutoff), files.window, error), error);
  const std::shared_ptr<const awtrix::speech::SpeechModel> model = awtrix::speech::SpeechModel::load(argv[2], error);
  require(model != nullptr, error);
  const auto voice = ModelVoice::create(model, error);
  require(voice != nullptr, error);
  modelSentence(endpointer, *voice);
  modelPause(endpointer, *voice);
  modelNoise(endpointer);
  return awtrix::test::finish("tc002-endpointer");
}
