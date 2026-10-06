#include "../../support.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "platform/tc002/voice/VoiceOverlay.h"
using awtrix::Canvas;
using awtrix::tc002::voice::VoiceOverlay;
constexpr auto check = awtrix::test::require;
int extent(const Canvas& c) {
  int n = 0;
  for (int y = 0; y < c.height(); ++y)
    for (int x = 0; x < c.width(); ++x) n += c.getPixel(x, y) != 0;
  return n;
}
int main(int argc, char** argv) {
  VoiceOverlay overlay;
  overlay.reset(0);
  Canvas quiet(52, 16), loud(52, 16);
  overlay.draw(quiet, VoiceOverlay::Phase::Listening, 1600);
  std::array<int16_t, 480> pcm{};
  for (int t = 1030; t <= 1600; t += 30) {
    for (int i = 0; i < 480; ++i) pcm[i] = i % 2 ? 12000 : -12000;
    overlay.audio(pcm.data(), pcm.size(), t);
  }
  overlay.draw(loud, VoiceOverlay::Phase::Listening, 1600);
  check(extent(loud) > extent(quiet),
        "Real PCM must expand the listening waveform");
  overlay.draw(loud, VoiceOverlay::Phase::Listening, 3600);
  check(extent(loud) == extent(quiet),
        "Old levels must decay without fresh microphone data");
  overlay.reset(0);
  pcm.fill(12000);
  overlay.audio(pcm.data(), pcm.size(), 1600);
  overlay.draw(loud, VoiceOverlay::Phase::Listening, 1600);
  check(std::equal(quiet.data(), quiet.data() + quiet.size(), loud.data()),
        "DC offset must not look like speech");
  for (auto phase :
       {VoiceOverlay::Phase::Starting, VoiceOverlay::Phase::Listening,
        VoiceOverlay::Phase::Processing, VoiceOverlay::Phase::Speaking,
        VoiceOverlay::Phase::Error}) {
    overlay.reset(0);
    overlay.draw(quiet, phase, 100);
    overlay.draw(loud, phase, 400);
    check(!std::equal(quiet.data(), quiet.data() + quiet.size(), loud.data()),
          "Active phases must animate");
    std::array<uint32_t, 18> guarded{};
    guarded.front() = guarded.back() = 0xdeadbeef;
    Canvas tiny(4, 4, guarded.data() + 1);
    overlay.draw(tiny, phase, 600);
    check(guarded.front() == 0xdeadbeef && guarded.back() == 0xdeadbeef,
          "Small canvas bounds");
  }
  // Optional native render strip for visual review; synthetic PCM only.
  if (argc == 2) {
    overlay.reset(0);
    for (int frame = 0; frame < 220; ++frame) {
      const int now = frame * 33;
      const float amp =
          300.f + 15000.f * std::pow(.5f + .5f * std::sin(now / 260.f), 3.f);
      for (int i = 0; i < 480; ++i)
        pcm[i] = static_cast<int16_t>(amp * std::sin(i * .2f));
      overlay.audio(pcm.data(), pcm.size(), now);
      const auto phase = frame < 20    ? VoiceOverlay::Phase::Starting
                         : frame < 100 ? VoiceOverlay::Phase::Listening
                         : frame < 140 ? VoiceOverlay::Phase::Processing
                         : frame < 190 ? VoiceOverlay::Phase::Speaking
                                       : VoiceOverlay::Phase::Error;
      overlay.draw(loud, phase, now);
      auto path =
          std::string(argv[1]) + "/frame-" + std::to_string(frame) + ".ppm";
      FILE* file = std::fopen(path.c_str(), "wb");
      check(file != nullptr, "Preview path writable");
      std::fprintf(file, "P6\n52 16\n255\n");
      for (std::size_t i = 0; i < loud.size(); ++i) {
        const auto p = loud.data()[i];
        const unsigned char rgb[] = {static_cast<unsigned char>(p >> 16),
                                     static_cast<unsigned char>(p >> 8),
                                     static_cast<unsigned char>(p)};
        std::fwrite(rgb, 1, 3, file);
      }
      std::fclose(file);
    }
  }
  std::puts(
      "Voice overlay: PCM response, DC rejection, decay, phase animation and "
      "bounds passed");
}
