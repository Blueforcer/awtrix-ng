#include "../../support.h"
#include <cmath>
#include <cstdio>
#include <vector>

#include "platform/tc002/runtime/MicrophoneInput.h"
#include "core/Settings.h"

using namespace awtrix;
namespace {
int& failures = awtrix::test::failures();
using awtrix::test::check;
struct Playback : audio::IAnalysisSource {
  bool active = false;
  bool analysis(int64_t, audio::FrameStats& out) override { out = {}; out.level = 123; return active; }
};
}
int main() {
  std::vector<std::string> requests;
  tc002::MicrophoneInput mic([&](std::string data) { requests.push_back(std::move(data)); return true; });
  Playback playback;
  audio::AnalysisRouter router(&playback, &mic);
  audio::FrameStats stats;
  check(requests.empty(), "constructing input does not acquire audio");
  check(!router.analysis(audio::AnalysisSource::Automatic, 1000, stats) && requests.size() == 1, "automatic requests input when playback is idle");
  router.analysis(audio::AnalysisSource::Microphone, 1020, stats);
  check(requests.size() == 1, "one pending request");
  tc002::SupervisorMessage message;
  check(tc002::decodeSupervisorMessage(requests.back(), message), "request decodes");
  tc002::MicrophonePcm pcm;
  pcm.id = message.microphonePcm.id;
  for (unsigned i = 0; i < tc002::kMicrophonePcmSamples; ++i)
    pcm.samples.push_back(static_cast<int16_t>(14000 * std::sin(i * 2 * 3.141592653589793 * 1000 / 16000)));
  const auto wire = tc002::encodeMicrophonePcm(pcm);
  check(wire.size() < tc002::kMaxSupervisorMessage && tc002::decodeSupervisorMessage(wire, message) &&
      message.microphonePcm.samples == pcm.samples, "signed PCM round-trips in one bounded datagram");
  mic.receive(message.microphonePcm, 1070);
  check(router.analysis(audio::AnalysisSource::Microphone, 1080, stats), "input produces spectrum");
  audio::SpectrumAnalyzer reference(audio::SpectrumAnalyzer::Scaling::Fixed);
  audio::FrameStats expected;
  reference.analyze(pcm.samples.data(), pcm.samples.size(), 1, 16000, expected);
  check(stats.bands[reference.bandForHz(1000)] > 200, "1 kHz tone reaches the expected frequency band");
  check(stats.level == expected.level, "level comes from PCM analysis");
  mic.analysis(1090, stats);
  check(!stats.beat, "cached analysis never repeats a beat on later frames");
  playback.active = true;
  check(router.analysis(audio::AnalysisSource::Automatic, 1100, stats) && stats.level == 123 && requests.size() == 1,
        "automatic gives playback priority without microphone acquisition");
  check(!router.analysis(audio::AnalysisSource::Microphone, 1200, stats) && requests.size() == 2,
        "explicit microphone overrides playback and discards stale cached audio");
  mic.receive(pcm, 1250);
  check(!mic.analysis(1260, stats), "old request response cannot complete the current request");
  check(tc002::decodeSupervisorMessage(requests.back(), message), "second request decodes");
  pcm.id = message.microphonePcm.id; pcm.samples.clear(); pcm.error = "capture failed";
  mic.receive(pcm, 1270);
  check(!mic.analysis(1280, stats), "failed capture never becomes spectrum");
  const auto count = requests.size();
  mic.analysis(7000, stats); mic.analysis(7020, stats);
  check(requests.size() == count + 1, "timeout recovery remains bounded to one request");
  mic.deactivate();
  playback.active = false;
  router.analysis(audio::AnalysisSource::Playback, 7100, stats);
  check(requests.size() == count + 1, "playback-only never falls back to microphone");
  tc002::decodeSupervisorMessage(requests.back(), message);
  pcm.id = message.microphonePcm.id;
  pcm.error.clear();
  pcm.samples.assign(tc002::kMicrophonePcmSamples, 1234);
  mic.analysis(7120, stats);
  mic.receive(pcm, 7140);
  check(!mic.analysis(7160, stats), "reactivating does not accept a capture from before deactivation");
  Settings settings;
  check(settings.musicSource == 0, "automatic is the default source");
  settings.applyRead(api::JsonReader("{\"musicSource\":\"microphone\"}"));
  check(settings.musicSource == 2, "persistable source uses named settings");
  SettingsError error;
  check(!Settings::validateRead(api::JsonReader("{\"musicSource\":\"unknown\"}"), error), "invalid sources rejected");
  tc002::MicrophoneInput contested([](std::string) { return true; });
  contested.analysis(1000, stats);
  pcm.id = 1;
  contested.receive(pcm, 1070);
  check(contested.analysis(1080, stats), "fresh PCM is available before UART contention");
  const auto cachedLevel = stats.level;
  contested.analysis(1100, stats);
  contested.receive({2, {}, "microphone unavailable or busy"}, 1110);
  check(contested.analysis(1120, stats) && stats.level == cachedLevel,
        "a busy battery query does not blank the last valid audio window");
  check(!contested.analysis(1600, stats), "retained audio still expires after 500 ms without fresh PCM");
  {
    std::vector<std::string> sent;
    tc002::MicrophoneInput pitched([&](std::string data) { sent.push_back(std::move(data)); return true; });
    audio::AnalysisRouter playbackOnly(nullptr, &pitched);
    tc002::MicrophonePcm tone;
    for (unsigned i = 0; i < tc002::kMicrophonePcmSamples; ++i)
      tone.samples.push_back(static_cast<int16_t>(8000 * std::sin(i * 2 * 3.141592653589793 * 440 / 16000)));
    auto answer = [&](int64_t at) {
      tc002::SupervisorMessage request;
      check(tc002::decodeSupervisorMessage(sent.back(), request), "pitch request decodes");
      tone.id = request.microphonePcm.id;
      pitched.receive(tone, at);
    };
    check(pitched.pitch(1000) == 0.f && sent.size() == 1, "the first pitch call answers 0 and starts the snapshots");
    playbackOnly.analysis(audio::AnalysisSource::Playback, 1010, stats);
    answer(1070);
    const float hz = pitched.pitch(1080);
    check(std::fabs(1200 * std::log2(hz / 440.0)) < 5, "a 440 Hz window answers 440 Hz");
    check(!pitched.analysis(1090, stats), "a window taken for pitch alone does not feed switched-off analysis");
    playbackOnly.analysis(audio::AnalysisSource::Playback, 1100, stats);
    check(pitched.pitch(1180) == hz && sent.size() == 2, "switching microphone analysis off does not stop pitch");
    answer(1190);
    check(pitched.pitch(1200) == hz, "the next window answers too");
    check(pitched.pitch(1490) == hz && pitched.pitch(1491) == 0.f, "a window older than 300 ms answers 0");
    answer(1500);
    check(pitched.pitch(1510) == hz, "a fresh window answers again");
    pitched.pitch(1600);
    pitched.setStreaming(true);
    const auto before = sent.size();
    check(pitched.pitch(1620) == 0.f && pitched.pitch(1700) == 0.f && sent.size() == before,
          "while the voice assistant streams, pitch answers 0 and requests nothing");
    answer(1710);
    pitched.setStreaming(false);
    check(pitched.pitch(1720) == 0.f && sent.size() == before + 1,
          "a window from the stream is never used; the next call requests a new one");
    answer(1780);
    check(pitched.pitch(1790) == hz, "pitch resumes after the stream");
    const auto count = sent.size();
    pitched.pitch(1900);
    pitched.pitch(1910);
    check(sent.size() == count + 1, "one request in flight at a time");
    const auto sine = tone.samples;
    tone.samples.pop_back();
    answer(1950);
    check(pitched.pitch(1960) == hz, "an incomplete window never replaces the last good one");
    tone.samples.assign(tc002::kMicrophonePcmSamples, 0);
    pitched.pitch(2010);
    answer(2020);
    check(pitched.pitch(2030) == 0.f, "silence answers 0");
    tc002::MicrophoneInput idle([&](std::string) { return true; });
    idle.analysis(1000, stats);
    tone.id = 1;
    tone.samples = sine;
    idle.receive(tone, 1050);
    check(idle.pitch(1060) == 0.f, "a window nobody asked pitch of is not kept for it");
  }
  audio::AnalysisEnvelope envelope;
  audio::FrameStats target;
  target.bands[0] = target.level = 200;
  envelope.process(target, 0);
  target = {};
  const auto falling = envelope.process(target, 20);
  check(falling.bands[0] > 180 && falling.bands[0] < 200, "bands fall smoothly between PCM windows");
  const auto later = envelope.process(target, 220);
  check(later.bands[0] > 0 && later.bands[0] < 100, "envelope releases with elapsed time");
  target.bands[0] = 200;
  check(envelope.process(target, 240).bands[0] > later.bands[0] + 30, "attack is faster than release");
  audio::AnalysisEnvelope fast, slow;
  target.bands[0] = 200; fast.process(target, 0); slow.process(target, 0);
  target = {};
  for (int t = 10; t < 100; t += 10) fast.process(target, t);
  check(fast.process(target, 100).bands[0] == slow.process(target, 100).bands[0],
        "envelope is independent of render frame rate");
  audio::SpectrumAnalyzer fixed(audio::SpectrumAnalyzer::Scaling::Fixed);
  std::vector<int16_t> quiet(1024), loud(1024), offset(1024);
  for (int i = 0; i < 1024; ++i) {
    const double wave = std::sin(i * 2 * 3.141592653589793 * 1000 / 16000);
    quiet[i] = static_cast<int16_t>(150 * wave);
    loud[i] = static_cast<int16_t>(1500 * wave);
    offset[i] = static_cast<int16_t>(quiet[i] + 8000);
  }
  audio::FrameStats low, high, repeated;
  fixed.analyze(quiet.data(), 1024, 1, 16000, low);
  fixed.analyze(loud.data(), 1024, 1, 16000, high);
  check(high.level > low.level + 100, "absolute input scale distinguishes quiet and loud audio");
  for (int i = 0; i < 400; ++i) fixed.analyze(quiet.data(), 1024, 1, 16000, repeated);
  check(repeated.level == low.level, "quiet input never rises after a loud passage");
  for (int i = 0; i < audio::kBandCount; ++i)
    check(repeated.bands[i] == low.bands[i], "quiet frequency bands never auto-amplify");
  fixed.analyze(offset.data(), 1024, 1, 16000, repeated);
  check(std::abs(int(repeated.level) - low.level) <= 1, "microphone DC offset is not sound energy");
  fixed.reset();
  std::vector<int16_t> silence(32768);
  for (int i = 0; i < 40000; ++i) fixed.analyze(silence.data(), silence.size(), 1, 16000, repeated);
  for (int i = 0; i < 1024; ++i)
    loud[i] = static_cast<int16_t>(16000 * std::sin(i * 2 * 3.141592653589793 * 100 / 16000));
  fixed.analyze(loud.data(), loud.size(), 1, 16000, repeated);
  check(repeated.beat, "beat detection survives more than 22 hours of continuous silence without counter overflow");
  std::printf("microphone input: %s\n", failures ? "FAILED" : "ok");
  return failures ? 1 : 0;
}
