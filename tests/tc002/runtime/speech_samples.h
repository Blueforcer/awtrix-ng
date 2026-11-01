#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "../../support.h"
#include "platform/tc002/speech/SpeechModelVoice.h"

namespace awtrix::test::speech {

constexpr uint64_t kSamplesPerMs = 16;

inline std::vector<int16_t> say(awtrix::speech::ModelVoice& voice, const char* text) {
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

inline std::vector<int16_t> noise(uint64_t ms, double dbfs, uint32_t seed) {
  std::vector<int16_t> out(ms * kSamplesPerMs);
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

inline std::vector<int16_t> join(std::initializer_list<std::vector<int16_t>> parts) {
  std::vector<int16_t> out;
  for (const auto& part : parts) out.insert(out.end(), part.begin(), part.end());
  return out;
}

}
