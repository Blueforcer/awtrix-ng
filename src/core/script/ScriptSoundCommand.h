#pragma once

#include <string>

#include "core/Command.h"
#include "core/script/ScriptServices.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {

inline Command scriptSoundCommand(script::SoundAction action, const std::string& payload) {
  Command c;
  c.source = Source::Internal;
  switch (action) {
    case script::SoundAction::Play:
      c.type = CommandType::PlayAudio;
      c.arg = static_cast<int>(sound::Source::Auto);
      c.payload = payload;
      break;
    case script::SoundAction::Mp3:
      c.type = CommandType::PlayAudio;
      c.arg = static_cast<int>(sound::Source::Mp3);
      c.payload = payload;
      break;
    case script::SoundAction::Melody:
      c.type = CommandType::PlayAudio;
      c.arg = static_cast<int>(sound::Source::Melody);
      c.payload = payload;
      break;
    case script::SoundAction::Track:
      c.type = CommandType::PlayAudio;
      c.arg = static_cast<int>(sound::Source::Track);
      c.payload = payload;
      break;
    case script::SoundAction::Rtttl:
      c.type = CommandType::PlayAudio;
      c.arg = static_cast<int>(sound::Source::Rtttl);
      c.payload = payload;
      break;
    // sound.stop() must not kill the user's radio.
    case script::SoundAction::Stop:
      c.type = CommandType::StopAudio;
      c.arg = static_cast<int>(sound::StopScope::Sounds);
      break;
  }
  return c;
}

}
