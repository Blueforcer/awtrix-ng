#pragma once

#include <cstdint>
#include <string>

namespace awtrix {

class ISoundBackend {
 public:
  virtual ~ISoundBackend() = default;
  virtual void begin() = 0;
  virtual void setVolume(uint8_t volume) = 0;
  virtual bool playFile(const std::string& id) = 0;
  virtual void playRtttl(const std::string& rtttl) = 0;
  virtual void stop() = 0;
  virtual void tick() = 0;
  virtual bool isPlaying() const = 0;
  virtual bool supportsRtttl() const { return true; }
};

}
