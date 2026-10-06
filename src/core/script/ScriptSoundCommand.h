#pragma once

#include <string>

#include "core/Command.h"
#include "core/script/ScriptServices.h"
#include "core/sound/AudioRouter.h"

namespace awtrix {

// script is the one asking: its own sounds are found first, and it only ever stops what it started.
inline Command scriptSoundCommand(script::SoundAction action, const std::string& json,
                                  const std::string& script) {
  Command c;
  c.source = Source::Internal;
  c.name = script;
  switch (action) {
    case script::SoundAction::Play:
    case script::SoundAction::Effect:
      c.type = CommandType::PlayAudio;
      c.arg = static_cast<int>(action == script::SoundAction::Effect ? sound::PlayAs::Effect
                                                                     : sound::PlayAs::Once);
      c.payload = json;
      break;
    case script::SoundAction::Stop:
    case script::SoundAction::Release:
      c.type = CommandType::StopAudio;
      c.arg = static_cast<int>(sound::Stop::ScriptSounds);
      break;
    case script::SoundAction::StopMusic:
      c.type = CommandType::StopAudio;
      c.arg = static_cast<int>(sound::Stop::ScriptMusic);
      break;
  }
  return c;
}

}
