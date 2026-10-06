#include "../../support.h"
// The speech worker with a fake voice: the ring between them, cancelling, a voice that is missing,
// fails or falls behind, and the mixer pulling speech over a running loop in real time.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/synth/SongParser.h"
#include "platform/tc002/audio/Tc002AudioMixer.h"
#include "platform/tc002/speech/SpeechLimiter.h"
#include "platform/tc002/speech/SpeechRing.h"
#include "platform/tc002/speech/SpeechSource.h"

using namespace awtrix;
using namespace awtrix::speech;
using Clock = std::chrono::steady_clock;
using Read = tc002::PcmSource::Read;

namespace {

int& failures = awtrix::test::failures();

using awtrix::test::check;

// Never zero, so a gap shows.
int16_t sample(std::size_t i) { return static_cast<int16_t>(1 + i % 1000); }

// The voice's samples through a limiter of their own, its tail included.
std::vector<int16_t> spoken(std::size_t count) {
  SpeechLimiter limiter(24000);
  std::vector<int16_t> out(count + limiter.lookahead());
  for (std::size_t i = 0; i < count; ++i) out[i] = sample(i);
  limiter.process(out.data(), count);
  limiter.flush(out.data() + count, limiter.lookahead());
  return out;
}

// Renders `samples` in blocks of at most `block`, each taking `delay`. A closed gate holds render()
// until it opens or the utterance is cancelled.
struct FakeVoice : SpeechVoice {
  std::size_t samples = 4800;
  std::size_t block = 480;
  std::chrono::milliseconds delay{0};
  std::chrono::milliseconds startDelay{0};
  bool refuse = false;
  std::atomic<bool> gate{true};
  std::atomic<int> started{0};
  std::atomic<int> cancels{0};
  std::atomic<int> ended{0};
  std::atomic<std::size_t> rendered{0};
  std::mutex threadsMutex;
  std::vector<std::thread::id> threads;

  uint32_t rate() const override { return 24000; }
  std::unique_ptr<SpeechUtterance> start(const Plan&) override;
};

class FakeUtterance final : public SpeechUtterance {
 public:
  explicit FakeUtterance(FakeVoice& voice) : voice_(voice) {}
  ~FakeUtterance() override { ++voice_.ended; }
  std::size_t render(int16_t* out, std::size_t max) override {
    {
      std::lock_guard<std::mutex> lock(voice_.threadsMutex);
      voice_.threads.push_back(std::this_thread::get_id());
    }
    while (!voice_.gate && !cancelled_) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (cancelled_) return 0;
    std::this_thread::sleep_for(voice_.delay);
    const std::size_t count = std::min({max, voice_.block, voice_.samples - at_});
    for (std::size_t i = 0; i < count; ++i) out[i] = sample(at_ + i);
    at_ += count;
    voice_.rendered += count;
    return count;
  }
  void cancel() override {
    cancelled_ = true;
    ++voice_.cancels;
  }

 private:
  FakeVoice& voice_;
  std::size_t at_ = 0;
  std::atomic<bool> cancelled_{false};
};

std::unique_ptr<SpeechUtterance> FakeVoice::start(const Plan&) {
  ++started;
  std::this_thread::sleep_for(startDelay);
  if (refuse) return nullptr;
  return std::unique_ptr<SpeechUtterance>(new FakeUtterance(*this));
}

std::shared_ptr<const Plan> plan() { return std::make_shared<Plan>(); }

// Everything the source hands out until it ends; Wait is retried for up to two seconds.
Read drain(SpeechSource& source, std::vector<int16_t>& out) {
  const auto giveUp = Clock::now() + std::chrono::seconds(2);
  for (;;) {
    const int16_t* samples = nullptr;
    std::size_t frames = 0;
    const Read read = source.next(samples, frames);
    if (read == Read::Data) out.insert(out.end(), samples, samples + frames);
    else if (read != Read::Wait || Clock::now() > giveUp) return read;
    else std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

bool waitFor(const std::function<bool()>& done) {
  const auto giveUp = Clock::now() + std::chrono::seconds(2);
  while (!done()) {
    if (Clock::now() > giveUp) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

void test_ring() {
  SpeechRing ring(8);
  const int16_t in[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  int16_t out[10] = {};
  check(ring.write(in, 6) == 6 && ring.size() == 6 && ring.room() == 2, "a ring takes what fits");
  check(ring.read(out, 4) == 4 && out[0] == 1 && out[3] == 4, "and gives the oldest first");
  check(ring.write(in + 6, 4) == 4 && ring.size() == 6, "writing wraps around the end");
  check(ring.write(in, 5) == 2 && ring.room() == 0, "a full ring takes no more");
  check(ring.read(out, 10) == 8 && out[0] == 5 && out[5] == 10 && out[6] == 1 && out[7] == 2,
        "reading wraps around the end in order");
  check(ring.read(out, 1) == 0 && ring.size() == 0, "an empty ring gives nothing");
}

void test_limiter() {
  const int16_t quiet = static_cast<int16_t>(std::lround(1000 * kSpeechGain));
  {
    SpeechLimiter limiter(24000);
    const std::size_t lookahead = limiter.lookahead();
    check(lookahead == 48, "the lookahead is 2 ms");
    std::vector<int16_t> samples(200, 0);
    samples[0] = 1000;
    limiter.process(samples.data(), samples.size());
    check(std::all_of(samples.begin(), samples.begin() + lookahead, [](int16_t s) { return s == 0; }) &&
              samples[lookahead] == quiet,
          "quiet speech gets the gain, lookahead frames late");
  }
  {
    SpeechLimiter limiter(24000);
    std::vector<int16_t> samples(24000);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = i / 24 % 2 ? 32767 : -32768;
    limiter.process(samples.data(), samples.size());
    check(*std::max_element(samples.begin(), samples.end()) <= 29204 &&
              *std::min_element(samples.begin(), samples.end()) >= -29204,
          "full scale on both halves of the wave stays below the ceiling");
  }
  {
    SpeechLimiter limiter(24000);
    const std::size_t lookahead = limiter.lookahead(), peak = 4800;
    std::vector<int16_t> samples(24000, 1000);
    samples[peak] = 30000;
    limiter.process(samples.data(), samples.size());
    const int16_t top = samples[peak + lookahead];
    check(top >= 29200 && top <= 29204, "a peak is turned down to the ceiling");
    check(samples[peak + lookahead - 1] < samples[peak] && samples[peak] < quiet && samples[peak - 1] == quiet,
          "the gain ramps down over the lookahead before the peak");
    check(samples[peak + lookahead + 7200] >= quiet * 0.95, "and recovers after it");
  }
  {
    SpeechLimiter limiter(24000);
    std::vector<int16_t> samples(10, 1000), tail(512, -1);
    limiter.process(samples.data(), samples.size());
    check(limiter.flush(tail.data(), tail.size()) == limiter.lookahead() && tail[limiter.lookahead() - 1] == quiet &&
              tail[limiter.lookahead()] == -1,
          "flush hands over exactly what is held back");
  }
  {
    SpeechLimiter limiter(0);
    std::vector<int16_t> samples(4, 1000);
    limiter.process(samples.data(), samples.size());
    check(limiter.lookahead() == 1 && samples[1] == quiet, "a voice without a usable rate still plays");
  }
}

void test_plays_what_the_voice_renders() {
  auto voice = std::make_shared<FakeVoice>();
  voice->samples = 10000;
  auto source = SpeechSource::open(voice, plan());
  check(source && source->rate() == 24000 && source->channels() == 1, "a source opens at the voice's rate");
  std::vector<int16_t> heard;
  check(drain(*source, heard) == Read::End, "the source ends with the utterance");
  check(heard == spoken(10000), "every sample arrives once, in order, through the limiter");
  check(SpeechSource::waitForWorkers(2000) && voice->ended == 1, "the utterance is released");
}

void test_waits_for_a_head_start() {
  auto voice = std::make_shared<FakeVoice>();
  voice->gate = false;
  auto source = SpeechSource::open(voice, plan());
  const int16_t* samples = nullptr;
  std::size_t frames = 0;
  check(source->next(samples, frames) == Read::Wait, "nothing to hand out before the voice speaks");
  voice->gate = true;
  std::vector<int16_t> heard;
  check(drain(*source, heard) == Read::End && heard == spoken(4800), "the utterance follows once it does");

  auto brief = std::make_shared<FakeVoice>();
  brief->samples = 100;
  auto shortSource = SpeechSource::open(brief, plan());
  heard.clear();
  check(drain(*shortSource, heard) == Read::End && heard == spoken(100),
        "an utterance shorter than the head start plays once it is done");
}

void test_holds_at_most_its_buffer() {
  auto voice = std::make_shared<FakeVoice>();
  voice->samples = 72000;
  auto source = SpeechSource::open(voice, plan());
  waitFor([&] { return voice->rendered >= 24000; });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  check(voice->rendered <= 24000 + voice->block, "a voice ahead of the speaker waits for room");
  std::vector<int16_t> heard;
  check(drain(*source, heard) == Read::End && heard == spoken(72000), "and goes on as the speaker catches up");
}

void test_cancels_mid_utterance() {
  auto voice = std::make_shared<FakeVoice>();
  voice->samples = 240000;
  auto source = SpeechSource::open(voice, plan());
  check(waitFor([&] { return voice->rendered > 0; }), "the voice starts");
  voice->gate = false;
  const auto before = Clock::now();
  source.reset();
  check(Clock::now() - before < std::chrono::milliseconds(50), "dropping the source never waits for the voice");
  check(SpeechSource::waitForWorkers(2000), "the worker stops");
  check(voice->cancels == 1 && voice->ended == 1, "the utterance is cancelled and released");
  check(voice->rendered < 240000, "it stops before its end");
}

void test_cancels_before_the_voice_started() {
  auto voice = std::make_shared<FakeVoice>();
  voice->startDelay = std::chrono::milliseconds(100);
  auto source = SpeechSource::open(voice, plan());
  source.reset();
  check(SpeechSource::waitForWorkers(2000), "the worker stops");
  check(voice->started == 1 && voice->rendered == 0 && voice->ended == 1, "nothing is rendered for a dropped source");
}

void test_a_voice_that_fails() {
  auto voice = std::make_shared<FakeVoice>();
  voice->refuse = true;
  auto source = SpeechSource::open(voice, plan());
  std::vector<int16_t> heard;
  check(drain(*source, heard) == Read::Error && heard.empty(), "a voice that cannot start fails the source");
}

// Records what the mixer was handed; a Wait between the first sample and the end is a gap.
class Probe final : public tc002::PcmSource {
 public:
  Probe(std::unique_ptr<PcmSource> inner, int& gaps) : inner_(std::move(inner)), gaps_(gaps) {}
  Read next(const int16_t*& samples, std::size_t& frames) override {
    const Read read = inner_->next(samples, frames);
    if (read == Read::Data) started_ = true;
    if (read == Read::Wait && started_) ++gaps_;
    return read;
  }
  uint32_t rate() const override { return inner_->rate(); }
  uint8_t channels() const override { return inner_->channels(); }

 private:
  std::unique_ptr<PcmSource> inner_;
  int& gaps_;
  bool started_ = false;
};

// A voice at twice real time: 20 ms of speech every 10 ms, while a song loops underneath and the
// mixer is pulled at the speaker's pace.
void test_speech_over_a_loop_in_real_time() {
  const synth::ParseResult song = synth::parse("bpm 120\ninst a wave=sine attack=0 sustain=100 gate=100\na: a4:16");
  check(song.ok(), "the loop's song parses");
  auto voice = std::make_shared<FakeVoice>();
  voice->samples = 24000;
  voice->delay = std::chrono::milliseconds(10);
  tc002::EffectCache cache;
  tc002::MixSource mix(cache);
  mix.setSong(song.song, false);
  int gaps = 0;
  mix.setOneShot(std::unique_ptr<tc002::PcmSource>(new Probe(SpeechSource::open(voice, plan()), gaps)));
  const auto block = std::chrono::microseconds(1000000 * tc002::MixSource::kBlockFrames / tc002::MixSource::kRate);
  auto due = Clock::now();
  tc002::MixSource::End end = tc002::MixSource::End::None;
  bool steady = true;
  for (int i = 0; i < 300 && end == tc002::MixSource::End::None; ++i) {
    std::this_thread::sleep_until(due += block);
    const int16_t* samples = nullptr;
    std::size_t frames = 0;
    steady = mix.next(samples, frames) == Read::Data && frames == tc002::MixSource::kBlockFrames && steady;
    end = mix.takeOneShotEnd();
  }
  check(end == tc002::MixSource::End::Finished, "the speech plays to its end");
  check(steady, "the mixer hands out a full block every time");
  check(gaps == 0, "once started, the speech never runs dry");
  std::lock_guard<std::mutex> lock(voice->threadsMutex);
  check(!voice->threads.empty() &&
            std::none_of(voice->threads.begin(), voice->threads.end(),
                         [](std::thread::id id) { return id == std::this_thread::get_id(); }),
        "the voice never renders on the mixer's thread");
}

}

int main() {
  test_ring();
  test_limiter();
  test_plays_what_the_voice_renders();
  test_waits_for_a_head_start();
  test_holds_at_most_its_buffer();
  test_cancels_mid_utterance();
  test_cancels_before_the_voice_started();
  test_a_voice_that_fails();
  test_speech_over_a_loop_in_real_time();
  check(SpeechSource::waitForWorkers(2000), "every worker is gone");
  return failures == 0 ? 0 : 1;
}
