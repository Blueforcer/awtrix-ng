#include "../../support.h"
#include "FakeMicrophoneDaemon.h"

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "platform/tc002/runtime/MicrophoneHub.h"
#include "platform/tc002/speech/SpeechModel.h"
#include "platform/tc002/voice/VoiceCapture.h"
#include "speech_samples.h"

using namespace awtrix;
using namespace awtrix::test::speech;
using awtrix::test::check;
using awtrix::test::require;
using tc002::MicrophoneHub;
using tc002::StreamControl;
using tc002::StreamEvent;
using tc002::voice::VoiceCapture;

namespace {

std::string vadPath;
std::shared_ptr<awtrix::speech::ModelVoice> voice;

struct Rig {
  test::FakeMicrophoneDaemon daemon;
  int64_t now = 1000;
  MicrophoneHub hub{daemon.link(), [](const StreamEvent&, int64_t) {}};
  tc002::MicrophoneHub::Consumer microphone =
      hub.attach([this](const StreamEvent& e, int64_t at) { capture.receive(e, at); });
  const std::string model;
  VoiceCapture capture{microphone.port(), [this](tc002::MicrophoneMark mark) { microphone.startFrom(mark); }, model};
  const std::string path = "/tmp/voice-capture-test-" + std::to_string(::getpid()) + ".wav";

  explicit Rig(std::string modelPath = vadPath) : model(std::move(modelPath)) { hub.tick(now); }
  ~Rig() { ::unlink(path.c_str()); }

  void upstream(StreamEvent::Kind kind, bool available = true) { hub.receive(daemon.event(kind, available), now); }
  void stream() {
    hub.tick(now);
    upstream(StreamEvent::Kind::Support);
    hub.tick(now);
    upstream(StreamEvent::Kind::Started);
    hub.tick(now);
  }
  void play(const std::vector<int16_t>& pcm) {
    for (std::vector<int16_t>& block : test::FakeMicrophoneDaemon::blocks(pcm)) {
      now += 30;
      hub.receive(daemon.audio(std::move(block), now), now);
      hub.tick(now);
      capture.tick(now);
    }
  }
  std::vector<int16_t> fileSamples() const {
    std::ifstream in(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<int16_t> samples;
    for (std::size_t at = 44; at + 1 < bytes.size(); at += 2) {
      const auto low = static_cast<uint8_t>(bytes[at]), high = static_cast<uint8_t>(bytes[at + 1]);
      samples.push_back(static_cast<int16_t>(low | high << 8));
    }
    return samples;
  }
};

void sentence() {
  Rig rig;
  const auto words = say(*voice, "What will the weather be like in Berlin tomorrow?");
  require(rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now), "a recording starts");
  check(rig.capture.starting() && !rig.capture.start(rig.path, {}, {}, rig.now), "one recording at a time");
  rig.stream();
  check(!rig.capture.starting() && !rig.capture.start(rig.path, {}, {}, rig.now) && !rig.capture.take(),
        "it records once the stream runs");
  rig.play(join({noise(500, -60, 1), words, noise(3000, -60, 2)}));
  const auto result = rig.capture.take();
  require(result.has_value(), "the recording ends by itself");
  const uint64_t speechEnd = (500 * kSamplesPerMs + words.size()) / kSamplesPerMs;
  std::printf("sentence: speech ends at %llu ms, recording ends at %u ms\n",
              static_cast<unsigned long long>(speechEnd), result->ms);
  check(result->outcome == VoiceCapture::Outcome::Recorded && result->ms >= speechEnd + 600 &&
            result->ms <= speechEnd + 1530,
        "it ends 0.6 to 1.5 s after the last word, with the speech in it");
  check(tc002::voice::wavBytes(rig.fileSamples().size()) == result->bytes &&
            rig.fileSamples().size() / kSamplesPerMs == result->ms,
        "the WAV file holds every recorded sample");
  check(rig.capture.level() == 0 && !rig.capture.take(), "then it is idle");
  rig.now += MicrophoneHub::kLingerMs;
  rig.hub.tick(rig.now);
  check(rig.daemon.last().operation == StreamControl::Stop, "the hub lets the stream go after its linger");
}

void silence() {
  Rig rig;
  rig.capture.start(rig.path, {800, 1000, 15000}, {}, rig.now);
  rig.stream();
  rig.play(noise(2000, -40, 3));
  const auto result = rig.capture.take();
  check(result && result->outcome == VoiceCapture::Outcome::Silence, "room noise until the wait ends is Silence");
}

void fromAMark() {
  Rig rig;
  const tc002::MicrophoneHub::Consumer other = rig.hub.attach([](const StreamEvent&, int64_t) {});
  other.port()({1, StreamControl::Probe});
  other.port()({1, StreamControl::Start});
  rig.stream();
  rig.hub.hold(true);
  const auto words = say(*voice, "Turn on the light in the kitchen.");
  const std::size_t early = 600 * kSamplesPerMs;
  rig.play(noise(300, -60, 4));
  const tc002::MicrophoneMark mark = rig.hub.mark();
  rig.play({words.begin(), words.begin() + static_cast<std::ptrdiff_t>(early)});
  require(rig.capture.start(rig.path, {800, 5000, 15000}, mark, rig.now), "a recording from a mark starts");
  rig.hub.tick(rig.now);
  rig.play(join({{words.begin() + static_cast<std::ptrdiff_t>(early), words.end()}, noise(2500, -60, 5)}));
  const auto result = rig.capture.take();
  require(result.has_value(), "the recording from the mark ends");
  check(mark.sample == 300 * kSamplesPerMs && mark.sample % 24 == 0, "the mark lies on the stream's grid");
  const std::vector<int16_t> recorded = rig.fileSamples();
  check(result->outcome == VoiceCapture::Outcome::Recorded &&
            recorded.size() >= words.size() + 600 * kSamplesPerMs &&
            std::equal(words.begin(), words.end(), recorded.begin()),
        "the recording begins exactly at the mark, with the words said before the start");
}

void stopAndCancel() {
  Rig rig;
  rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now);
  rig.stream();
  rig.play(noise(300, -70, 6));
  check(rig.capture.level() == 0, "quiet noise has level 0");
  rig.capture.stop();
  const auto stopped = rig.capture.take();
  check(stopped && stopped->outcome == VoiceCapture::Outcome::Recorded && stopped->ms == 300 &&
            stopped->bytes == tc002::voice::wavBytes(300 * kSamplesPerMs),
        "stop() keeps what was recorded");
  rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now);
  rig.capture.stop();
  const auto empty = rig.capture.take();
  check(empty && empty->outcome == VoiceCapture::Outcome::Silence, "stop() before any audio is Silence");
  rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now);
  rig.capture.cancel();
  check(!rig.capture.take(), "cancel() ends without a result");
  check(rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now), "and leaves the capture free");
  rig.capture.stop();
  rig.capture.cancel();
  check(!rig.capture.take(), "cancel() also drops the result of a stopped recording");
}

void stalledStream() {
  Rig rig;
  require(rig.capture.start(rig.path, {800, 5000, 1000}, {}, rig.now), "a recording of at most a second starts");
  rig.stream();
  const int64_t started = rig.now;
  rig.play(noise(300, -60, 8));
  rig.now = started + 1000 + 2000 - 1;
  rig.capture.tick(rig.now);
  check(!rig.capture.take(), "a stream without audio keeps the recording until its limit and the answer time");
  rig.now += 1;
  rig.capture.tick(rig.now);
  const auto result = rig.capture.take();
  check(result && result->outcome == VoiceCapture::Outcome::Failed &&
            result->error == "the microphone stream stalled",
        "then the recording fails as stalled");
}

void noAnswer() {
  Rig rig;
  rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now);
  rig.hub.tick(rig.now);
  rig.upstream(StreamEvent::Kind::Support, false);
  rig.hub.tick(rig.now);
  auto result = rig.capture.take();
  check(result && result->outcome == VoiceCapture::Outcome::Failed && !result->error.empty(),
        "a microphone without the stream fails the recording");
  rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now);
  rig.now += 2000;
  rig.capture.tick(rig.now);
  result = rig.capture.take();
  check(result && result->outcome == VoiceCapture::Outcome::Failed, "a microphone that never answers fails it");
}

std::string outcome(const std::optional<VoiceCapture::Result>& result) {
  if (!result) return "no result";
  const char* names[] = {"Recorded", "Silence", "Failed"};
  return std::string(names[static_cast<int>(result->outcome)]) + " (" + result->error + ")";
}

void streamFailures() {
  {
    Rig rig;
    require(rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now), "a recording starts");
    rig.stream();
    rig.play(noise(300, -60, 11));
    rig.daemon.sample += 96;
    rig.play(noise(30, -60, 12));
    const auto result = rig.capture.take();
    check(result && result->outcome == VoiceCapture::Outcome::Failed &&
              result->error == "the microphone stream skipped audio",
          "audio that skips samples fails the recording: " + outcome(result));
    check(rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now), "and leaves the capture free");
  }
  {
    Rig rig;
    require(rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now), "a recording starts");
    rig.stream();
    rig.play(noise(300, -60, 13));
    rig.hub.receive(rig.daemon.end("microphone UART frame lost or corrupt", rig.now), rig.now);
    const auto result = rig.capture.take();
    check(result && result->outcome == VoiceCapture::Outcome::Failed &&
              result->error == "microphone UART frame lost or corrupt",
          "a stream that ends upstream during the recording fails it with the reason: " + outcome(result));
  }
}

struct FileSizeLimit {
  rlimit saved{};
  void (*handler)(int) = SIG_DFL;

  explicit FileSizeLimit(rlim_t bytes) {
    require(::getrlimit(RLIMIT_FSIZE, &saved) == 0, "the file size limit");
    handler = std::signal(SIGXFSZ, SIG_IGN);
    const rlimit limit{bytes, saved.rlim_max};
    require(::setrlimit(RLIMIT_FSIZE, &limit) == 0, "a lower file size limit");
  }
  ~FileSizeLimit() {
    ::setrlimit(RLIMIT_FSIZE, &saved);
    std::signal(SIGXFSZ, handler);
  }
};

void writeFailure() {
  Rig rig;
  std::optional<VoiceCapture::Result> result;
  {
    const FileSizeLimit limit(tc002::voice::wavBytes(5 * tc002::kStreamBlockSamples));
    require(rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now), "a recording starts");
    rig.stream();
    rig.play(noise(300, -60, 14));
    result = rig.capture.take();
  }
  check(result && result->outcome == VoiceCapture::Outcome::Failed && result->error == "could not write the recording",
        "a recording whose file cannot grow fails: " + outcome(result));
}

void modelComesAndGoes() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / ("voice-capture-model-" + std::to_string(::getpid()));
  fs::remove_all(dir);
  fs::create_directories(dir);
  const fs::path source(vadPath);
  Rig rig((dir / source.filename()).string());
  std::string error;
  check(!rig.capture.prepare(error) && !rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now),
        "the capture is made without its model and reads it only for a recording: " + error);
  const fs::path settings = fs::path(source).replace_extension(".json");
  fs::copy_file(source, dir / source.filename());
  fs::copy_file(settings, dir / settings.filename());
  const auto begun = std::chrono::steady_clock::now();
  require(rig.capture.prepare(error), error);
  const std::chrono::duration<double, std::milli> loading = std::chrono::steady_clock::now() - begun;
  std::printf("model load: %.2f ms\n", loading.count());
  require(rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now), "a recording starts with the model");
  rig.stream();
  rig.play(noise(300, -60, 9));
  rig.capture.stop();
  const auto result = rig.capture.take();
  check(result && result->outcome == VoiceCapture::Outcome::Recorded, "it records");
  fs::remove_all(dir);
  check(!rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now),
        "the recording let go of the model: the next one reads it again");
  check(!rig.capture.prepare(error) && !error.empty(), "and finds it gone: " + error);
}

void level() {
  Rig rig;
  rig.capture.start(rig.path, {800, 5000, 15000}, {}, rig.now);
  rig.stream();
  rig.play(noise(60, -6, 7));
  check(rig.capture.level() > 200, "loud audio has a high level: " + std::to_string(rig.capture.level()));
  rig.capture.cancel();
}

}

int main(int argc, char** argv) {
  require(argc == 3, "usage: tc002-voice-capture-test <vad.tflite> <voice.atts>");
  vadPath = argv[1];
  std::string error;
  const auto model = awtrix::speech::SpeechModel::load(argv[2], error);
  require(model != nullptr, error);
  voice = awtrix::speech::ModelVoice::create(model, error);
  require(voice != nullptr, error);
  sentence();
  silence();
  fromAMark();
  stopAndCancel();
  noAnswer();
  stalledStream();
  streamFailures();
  writeFailure();
  modelComesAndGoes();
  level();
  return awtrix::test::finish("tc002-voice-capture");
}
