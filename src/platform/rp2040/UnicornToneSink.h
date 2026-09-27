#pragma once
#include "core/sound/AudioSinks.h"
#include "core/sound/ToneSamples.h"
#include <I2S.h>

namespace awtrix {
class UnicornToneSink final : public sound::IToneSink {
 public:
  void begin() override;
  void setVolume(uint8_t percent) override;
  bool playRtttl(const std::string& text) override;
  bool playMelodyFile(const std::string& name) override;
  void stop() override;
  void tick() override;
  bool isPlaying() const override { return playing_; }
  bool ready() const { return ready_; }
 private:
  bool open();
  static constexpr int BufferWords = 480; // 20 ms, three buffers + library silence buffer
  static constexpr int BufferCount = 3;
  I2S i2s_{OUTPUT};
  sound::ToneSamples samples_;
  uint8_t volume_ = 80;
  bool ready_ = false, running_ = false, playing_ = false, draining_ = false;
  uint32_t drainedAt_ = 0;
  int partial_ = 0;
};
}
