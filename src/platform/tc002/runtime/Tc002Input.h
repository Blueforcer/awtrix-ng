#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace awtrix {

// Ordered physical edges. The three top buttons are the logical left, select and right buttons;
// the knob reports its detents and its push on their own.
class Tc002InputDecoder {
 public:
  enum class Knob : uint8_t { Clockwise, Counterclockwise, Down, Up };
  struct Sink {
    std::function<void(int, bool)> button;
    std::function<void(Knob)> knob;
  };
  bool event(bool knob, uint16_t type, uint16_t code, int32_t value, const Sink& sink);
  uint64_t presses() const { return presses_; }
  uint64_t clockwise() const { return clockwise_; }
  uint64_t counterclockwise() const { return counterclockwise_; }
 private:
  bool keys_[3]{};
  bool knobDown_ = false;
  int pendingTurn_ = 0;
  uint64_t presses_ = 0, clockwise_ = 0, counterclockwise_ = 0;
};

class Tc002Input {
 public:
  Tc002Input() = default;
  Tc002Input(const Tc002Input&) = delete;
  Tc002Input& operator=(const Tc002Input&) = delete;
  ~Tc002Input();
  bool begin();
  bool poll(const Tc002InputDecoder::Sink& sink);
  const std::string& error() const { return error_; }
 private:
  int keys_ = -1, knob_ = -1;
  Tc002InputDecoder decoder_;
  std::string error_;
  uint64_t records_ = 0;
};

}
