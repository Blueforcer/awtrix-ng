#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace awtrix::tc002::wake {

class ProbabilityWindow {
 public:
  static constexpr std::size_t kMaxWindow = 64;

  bool configure(float cutoff, std::size_t window, std::string& error);
  void setCutoff(float cutoff);
  bool push(uint8_t probability);
  float score() const { return static_cast<float>(sum_ / (255.0 * static_cast<double>(window_))); }
  void reset();

 private:
  std::array<uint8_t, kMaxWindow> ring_{};
  std::size_t window_ = 1;
  std::size_t filled_ = 0;
  std::size_t next_ = 0;
  uint32_t sum_ = 0;
  uint32_t threshold_ = 256;
};

}
