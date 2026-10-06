#include "../../support.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/tc002/runtime/MusicPitch.h"

using namespace awtrix;

namespace {

int& failures = awtrix::test::failures();
using awtrix::test::check;

bool near(float hz, double target) { return hz > 0.f && std::fabs(1200 * std::log2(hz / target)) < 10; }

// A sine of hz at rate, interleaved over channels, in blocks of `block` frames handed to the pitch
// as the audio thread does: block n is fed at startMs + n * block / rate, heard leadMs later.
struct Player {
  tc002::PlaybackPitch& pitch;
  uint32_t rate;
  uint8_t channels;
  double hz;
  int amplitude = 12000;
  std::size_t block = 441;
  int64_t leadMs = 130;
  uint64_t frame = 0;
  int64_t startMs = 0;

  int64_t nowMs() const { return startMs + static_cast<int64_t>(frame * 1000 / rate); }

  void play(int64_t ms) {
    std::vector<int16_t> pcm(block * channels);
    const uint64_t end = frame + static_cast<uint64_t>(ms) * rate / 1000;
    while (frame < end) {
      for (std::size_t i = 0; i < block; ++i) {
        const double t = static_cast<double>(frame + i) / rate;
        const auto v = static_cast<int16_t>(amplitude * std::sin(2 * 3.141592653589793 * hz * t));
        for (uint8_t c = 0; c < channels; ++c) pcm[i * channels + c] = v;
      }
      const int64_t now = nowMs();
      pitch.feed(pcm.data(), block, channels, rate, now, now + leadMs);
      frame += block;
    }
  }
};

void test_playback_pitch() {
  float hz = -1.f;
  {
    tc002::PlaybackPitch pitch;
    Player player{pitch, 44100, 2, 440.0};
    player.play(500);
    check(!pitch.pitch(player.nowMs(), hz) && hz == 0.f, "nothing is detected before anyone asks");
  }
  {
    tc002::PlaybackPitch pitch;
    Player player{pitch, 44100, 2, 440.0};
    check(!pitch.pitch(0, hz), "the first question has no answer yet");
    player.play(100);
    check(!pitch.pitch(player.nowMs(), hz), "a window is not answered before the speaker plays it");
    player.play(200);
    check(pitch.pitch(player.nowMs(), hz) && near(hz, 440), "44.1 kHz stereo playback answers 440 Hz");
    const int64_t end = player.nowMs();
    check(pitch.pitch(end + 250, hz) && near(hz, 440), "the last window heard still answers");
    check(!pitch.pitch(end + 130 + 300, hz) && hz == 0.f, "a window older than 300 ms answers nothing");
  }
  {
    tc002::PlaybackPitch pitch;
    Player player{pitch, 44100, 1, 220.0};
    pitch.pitch(0, hz);
    player.play(400);
    const int64_t now = player.nowMs();
    check(pitch.pitch(now, hz) && near(hz, 220), "the window heard now, not one still queued");
    player.hz = 660.0;
    player.play(60);
    check(pitch.pitch(now, hz) && near(hz, 220), "a later window waits until it is heard");
    player.play(300);
    check(pitch.pitch(player.nowMs(), hz) && near(hz, 660), "and then answers");
  }
  {
    tc002::PlaybackPitch pitch;
    Player player{pitch, 44100, 2, 440.0};
    pitch.pitch(0, hz);
    player.amplitude = 0;
    player.play(400);
    check(pitch.pitch(player.nowMs(), hz) && hz == 0.f, "silence is playback without a note");
  }
  for (uint32_t rate : {8000u, 22050u, 24000u, 48000u}) {
    tc002::PlaybackPitch pitch;
    Player player{pitch, rate, 2, 330.0};
    player.block = 1152;
    pitch.pitch(0, hz);
    player.play(600);
    check(pitch.pitch(player.nowMs(), hz) && near(hz, 330), "every speaker rate is folded to the detector");
  }
  {
    tc002::PlaybackPitch pitch;
    Player first{pitch, 48000, 2, 440.0};
    pitch.pitch(0, hz);
    first.play(40);
    Player second{pitch, 44100, 1, 523.25};
    second.startMs = first.nowMs();
    second.play(400);
    check(pitch.pitch(second.nowMs(), hz) && near(hz, 523.25), "a new format starts a fresh window");
  }
}

struct Microphone {
  std::vector<std::string> sent;
  tc002::MicrophoneInput input{[this](std::string data) {
    sent.push_back(std::move(data));
    return true;
  }};

  // Answers the request in flight with a sine of hz.
  void answer(double hz, int64_t at) {
    tc002::SupervisorMessage request;
    check(!sent.empty() && tc002::decodeSupervisorMessage(sent.back(), request), "microphone request decodes");
    tc002::MicrophonePcm pcm;
    pcm.id = request.microphonePcm.id;
    for (unsigned i = 0; i < tc002::kMicrophonePcmSamples; ++i)
      pcm.samples.push_back(static_cast<int16_t>(8000 * std::sin(i * 2 * 3.141592653589793 * hz / 16000)));
    input.receive(pcm, at);
  }
};

void test_music_pitch_follows_the_source() {
  using audio::AnalysisSource;
  {
    tc002::PlaybackPitch playback;
    Microphone mic;
    Player player{playback, 44100, 2, 440.0};
    check(tc002::musicPitch(AnalysisSource::Playback, 0, &playback, &mic.input) == 0.f,
          "Playback with nothing heard answers 0");
    player.play(400);
    const int64_t now = player.nowMs();
    check(near(tc002::musicPitch(AnalysisSource::Playback, now, &playback, &mic.input), 440),
          "Playback follows what the speaker plays");
    check(mic.sent.empty(), "Playback never switches the microphone on");
    check(near(tc002::musicPitch(AnalysisSource::Automatic, now, &playback, &mic.input), 440),
          "Automatic follows the speaker while something plays");
    check(mic.sent.empty(), "and leaves the microphone off meanwhile");
    check(tc002::musicPitch(AnalysisSource::Microphone, now, &playback, &mic.input) == 0.f && mic.sent.size() == 1,
          "Microphone asks the microphone even while the speaker plays");
    mic.answer(262, now + 60);
    check(near(tc002::musicPitch(AnalysisSource::Microphone, now + 70, &playback, &mic.input), 262),
          "and answers the voice, not the speaker");
    const int64_t silent = now + 130 + 300 + 1;
    check(tc002::musicPitch(AnalysisSource::Playback, silent, &playback, &mic.input) == 0.f,
          "Playback answers 0 once the speaker fell silent");
    const auto requests = mic.sent.size();
    check(tc002::musicPitch(AnalysisSource::Automatic, silent + 100, &playback, &mic.input) == 0.f &&
              mic.sent.size() == requests + 1,
          "Automatic turns to the microphone once the speaker fell silent");
    mic.answer(330, silent + 160);
    check(near(tc002::musicPitch(AnalysisSource::Automatic, silent + 170, &playback, &mic.input), 330),
          "and hears the voice");
  }
  {
    tc002::PlaybackPitch playback;
    Microphone mic;
    tc002::musicPitch(AnalysisSource::Microphone, 0, &playback, &mic.input);
    Player player{playback, 44100, 2, 440.0};
    player.play(400);
    float hz = 0.f;
    check(!playback.pitch(player.nowMs(), hz), "Microphone leaves the playback detector idle");
  }
  {
    Microphone mic;
    check(tc002::musicPitch(AnalysisSource::Automatic, 1000, nullptr, &mic.input) == 0.f && mic.sent.size() == 1,
          "without a speaker Automatic is the microphone");
    mic.answer(440, 1060);
    check(near(tc002::musicPitch(AnalysisSource::Automatic, 1070, nullptr, &mic.input), 440), "and answers it");
    mic.input.setStreaming(true);
    check(tc002::musicPitch(AnalysisSource::Automatic, 1080, nullptr, &mic.input) == 0.f,
          "while the voice assistant owns the microphone, pitch answers 0");
  }
  {
    tc002::PlaybackPitch playback;
    Player player{playback, 44100, 2, 440.0};
    tc002::musicPitch(AnalysisSource::Automatic, 0, &playback, nullptr);
    player.play(400);
    check(near(tc002::musicPitch(AnalysisSource::Automatic, player.nowMs(), &playback, nullptr), 440),
          "a TC002 without a microphone still hears the speaker");
    check(tc002::musicPitch(AnalysisSource::Microphone, player.nowMs(), &playback, nullptr) == 0.f,
          "and Microphone answers 0 there");
  }
}

}

int main() {
  test_playback_pitch();
  test_music_pitch_follows_the_source();
  std::printf("music pitch: %s\n", failures ? "FAILED" : "ok");
  return failures ? 1 : 0;
}
