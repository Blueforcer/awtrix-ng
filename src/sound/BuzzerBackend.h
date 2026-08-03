#pragma once

#include "hal/ISoundBackend.h"

class MelodyPlayer;

namespace awtrix {

class BuzzerBackend : public ISoundBackend {
 public:
  void setPin(int pin) { pin_ = pin; }
  void begin() override;
  void setVolume(uint8_t volume) override;
  bool playFile(const std::string& id) override;
  void playRtttl(const std::string& rtttl) override;
  void stop() override;
  void tick() override {}
  bool isPlaying() const override;

 private:
  int pin_ = 15;
  MelodyPlayer* player_ = nullptr;
  uint8_t volume_ = 25;
};

}
