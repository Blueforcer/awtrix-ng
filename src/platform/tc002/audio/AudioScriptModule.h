#pragma once

namespace awtrix::tc002 {
inline constexpr const char* kAudioScriptModule = R"BERRY(
import global
import json
var sound = global.sound
var music = global.music

def _sound_effect(x) # sound.effect(x)
  var result = _native_sound_effect(json.dump(x))
  if type(result) == 'string' raise 'value_error', result end
  return result
end

def _sound_beat() # sound.beat()
  return _native_song_beat()
end

def _music_pitch() # music.pitch()
  return _native_music_pitch()
end

sound.effect = _sound_effect
sound.beat = _sound_beat
music.pitch = _music_pitch
return sound
)BERRY";
}
