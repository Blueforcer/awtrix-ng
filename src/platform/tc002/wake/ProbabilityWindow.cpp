#include "platform/tc002/wake/ProbabilityWindow.h"

#include <cmath>

namespace awtrix::tc002::wake {

bool ProbabilityWindow::configure(float cutoff, std::size_t window, std::string& error) {
  if (!(cutoff >= 0.0f && cutoff <= 1.0f)) {
    error = "the cutoff must be within 0..1";
    return false;
  }
  if (window < 1 || window > kMaxWindow) {
    error = "the window must be within 1.." + std::to_string(kMaxWindow);
    return false;
  }
  window_ = window;
  setCutoff(cutoff);
  reset();
  return true;
}

void ProbabilityWindow::setCutoff(float cutoff) {
  const double scale = 255.0 * static_cast<double>(window_);
  const double wanted = static_cast<double>(cutoff);
  double smallestFiringSum = std::ceil(wanted * scale);
  while (smallestFiringSum > 0 && (smallestFiringSum - 1) / scale >= wanted) --smallestFiringSum;
  while (smallestFiringSum / scale < wanted) ++smallestFiringSum;
  threshold_ = static_cast<uint32_t>(smallestFiringSum);
}

void ProbabilityWindow::reset() {
  filled_ = 0;
  next_ = 0;
  sum_ = 0;
}

bool ProbabilityWindow::push(uint8_t probability) {
  if (filled_ == window_) sum_ -= ring_[next_];
  ring_[next_] = probability;
  sum_ += probability;
  next_ = (next_ + 1) % window_;
  if (filled_ < window_) ++filled_;
  return filled_ == window_ && sum_ >= threshold_;
}

}
