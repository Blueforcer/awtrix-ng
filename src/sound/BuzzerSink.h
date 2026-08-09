#pragma once

#include "core/sound/AudioSinks.h"

class MelodyPlayer;

namespace awtrix {

class BuzzerSink : public sound::IToneSink {
 public:
  void setPin(int pin) { pin_ = pin; }
  void begin() override;
  void setVolume(uint8_t percent) override;
  bool playRtttl(const std::string& rtttl) override;
  bool playMelodyFile(const std::string& name) override;
  void stop() override;
  void tick() override {}
  bool isPlaying() const override;

 private:
  int pin_ = 15;
  MelodyPlayer* player_ = nullptr;
  uint8_t volume_ = 80;
};

}
