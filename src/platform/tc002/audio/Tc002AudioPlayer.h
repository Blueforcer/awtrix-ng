#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "platform/tc002/audio/Tc002AudioLink.h"
#include "platform/tc002/audio/Tc002AudioSources.h"

namespace awtrix {
namespace tc002 {

// Plays one source at a time through the link: opens a generation once the format is known,
// sends within the helper's credit, drains at the end. No threads, no clocks of its own.
class Tc002AudioPlayer {
 public:
  enum class Outcome : uint8_t { None, Finished, Failed };
  static constexpr int64_t kReadyTimeoutMs = 5000;
  static constexpr int64_t kDrainSlackMs = 3000;

  // Called with every decoded block and how far ahead of the speaker it is.
  using Tap = std::function<void(const int16_t* samples, std::size_t frames, uint8_t channels,
                                 uint32_t rate, int64_t leadMs)>;

  explicit Tc002AudioPlayer(Tc002AudioLink& link) : link_(link) {}

  void play(std::unique_ptr<PcmSource> source, uint8_t volume, int64_t nowMs, Tap tap = nullptr);
  void stop();
  void setVolume(uint8_t percent);
  void pump(int64_t nowMs);

  bool active() const { return source_ != nullptr; }
  // The source ended and what it sent is playing out.
  bool draining() const { return draining_; }
  PcmSource* source() const { return source_.get(); }
  // The source that just finished stays readable until its outcome is taken.
  Outcome takeOutcome(std::unique_ptr<PcmSource>* finished = nullptr);
  int waitMs() const;

 private:
  void finish(Outcome outcome);
  bool fill(int64_t nowMs);

  Tc002AudioLink& link_;
  std::unique_ptr<PcmSource> source_;
  std::unique_ptr<PcmSource> finished_;
  Tap tap_;
  uint8_t volume_ = 0;
  uint32_t generation_ = 0;
  uint32_t rate_ = 0;
  uint8_t channels_ = 0;
  std::vector<int16_t> pending_;
  std::size_t offset_ = 0;
  bool draining_ = false;
  bool waiting_ = false;
  Outcome ending_ = Outcome::Finished;
  Outcome outcome_ = Outcome::None;
  int64_t readyDeadlineMs_ = 0;
  int64_t drainDeadlineMs_ = 0;
};

}
}
