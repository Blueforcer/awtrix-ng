#pragma once

#include "core/sound/AudioSinks.h"

class MelodyPlayer;

namespace awtrix {

class BuzzerSink : public sound::IToneSink {
 public:
  void setPin(int pin) { pin_ = pin; }
  void begin() override;
  void setVolumes(const sound::Volumes& volumes) override;
  bool playRtttl(const std::string& rtttl, sound::Group group) override;
  bool playMelodyFile(const std::string& name, sound::Group group) override;
  void stop() override;
  void tick() override {}
  bool isPlaying() const override;

 private:
  void applyVolume();

  int pin_ = 15;
  MelodyPlayer* player_ = nullptr;
  sound::Volumes volumes_;
  sound::Group group_ = sound::Group::Alert;
};

}
