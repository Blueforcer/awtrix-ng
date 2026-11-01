#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "platform/tc002/wake/ModelStream.h"
#include "platform/tc002/wake/WakeDetector.h"

namespace awtrix::tc002::wake {

struct EndpointSettings {
  uint32_t silenceMs = 800;
  uint32_t waitMs = 5000;
  uint32_t maxMs = 15000;
};

enum class Endpoint { None, Started, Ended, Silence, Limit };

class EndpointTimer {
 public:
  void start(const EndpointSettings& settings);
  Endpoint step(bool voice, uint64_t at);
  bool done() const { return done_; }

 private:
  Endpoint finish(Endpoint kind);

  EndpointSettings settings_;
  uint64_t end_ = 0;
  bool speaking_ = false, done_ = true;
};

class Endpointer {
 public:
  bool load(std::string_view model, std::string& error);
  bool configure(float cutoff, std::size_t window, std::string& error);
  void start(const EndpointSettings& settings);
  Endpoint next(const int16_t*& samples, std::size_t& count);
  uint64_t stepMs() const { return stream_.model() ? stream_.model()->frames() * WakeFrontend::kStepMs : 0; }

 private:
  ModelStream stream_;
  WakeWindow window_;
  EndpointTimer timer_;
};

}
