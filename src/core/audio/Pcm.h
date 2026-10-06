#pragma once

#include <cstdint>

namespace awtrix::audio {

// Borrowed interleaved signed PCM16. The owner keeps samples alive for the call;
// frames counts samples per channel. Separate blocks do not imply gapless capture.
// Raw-audio consumers can use this descriptor without depending on FFT analysis.
struct PcmView {
  const int16_t* samples = nullptr;
  int frames = 0;
  int channels = 1;
  int sampleRate = 16000;
};

}
