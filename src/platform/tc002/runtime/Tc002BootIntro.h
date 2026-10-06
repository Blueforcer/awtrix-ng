#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/render/BootScreen.h"
#include "core/render/Canvas.h"
#include "core/render/Font.h"

namespace awtrix {

// The boot sound as the intro's clock.
class BootSoundClock {
 public:
  enum class Start : uint8_t { Pending, Audible, Failed };
  virtual ~BootSoundClock() = default;
  // Audible sets audibleAtMs to the monotonic time the sound's first sample reaches the speaker.
  virtual Start poll(int64_t& audibleAtMs) = 0;
  virtual void cancel() = 0;
};

// The power-on sequence: the intro, then the info screen with the firmware version and the device
// address when one is known by the time the intro ends. With a boot sound the intro holds its first
// frame until the sound's first sample is audible; a sound that is not audible after kSoundWaitMs
// is cancelled.
class Tc002BootIntro {
 public:
  static constexpr int64_t kSoundWaitMs = 1500;

  Tc002BootIntro(std::string version, std::function<std::string()> address, BootSoundClock* sound = nullptr)
      : version_(std::move(version)), address_(std::move(address)), sound_(sound) {}

  // Draws the frame for nowMs and returns true while the sequence owns the panel, false once the
  // apps take over.
  bool draw(Canvas& canvas, const GfxFont& font, int64_t nowMs);

  // Monotonic time of the intro's t = 0; -1 while it still waits for the sound.
  int64_t introStartMs() const { return phase_ == Phase::Waiting ? -1 : originMs_; }
  bool soundSynchronized() const { return synchronized_; }
  const std::string& shownAddress() const { return info_.address; }

 private:
  enum class Phase : uint8_t { Waiting, Intro, Info, Done };

  void start(int64_t originMs, bool synchronized);

  std::string version_;
  std::function<std::string()> address_;
  BootSoundClock* sound_;
  Phase phase_ = Phase::Waiting;
  int64_t firstMs_ = -1;
  int64_t originMs_ = 0;
  int64_t infoMs_ = 0;
  bool synchronized_ = false;
  render::BootInfo info_;
};

}
