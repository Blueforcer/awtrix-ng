#pragma once

#include <string>

#include "platform/posix/Hold.h"

namespace awtrix::tc002::voice {

class VoiceSpeaker {
 public:
  virtual ~VoiceSpeaker() = default;
  virtual bool available() const = 0;
  virtual posix::Hold claimVoiceChannel() = 0;
  virtual posix::Hold holdMusic() = 0;
  virtual bool quiet() const = 0;
  virtual bool play(const std::string& path) = 0;
  virtual void stop() = 0;
  virtual bool finished() const = 0;
  virtual bool heard() const = 0;
};

}
