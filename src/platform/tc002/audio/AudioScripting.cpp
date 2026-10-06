#include "platform/tc002/audio/AudioScripting.h"

#include "berry.h"
#include "core/apps/IApp.h"
#include "platform/tc002/audio/AudioScriptModule.h"

namespace awtrix::tc002 {

void AudioScripting::install(script::ScriptExtensionHost& host) {
  host.defineNative("_native_song_beat", beat, this);
  host.defineNative("_native_music_pitch", pitch, this);
  host.defineNative("_native_sound_effect", effect, this);
  host.defineModule("_tc002_audio", kAudioScriptModule);
}

int64_t AudioScripting::now() const {
  const auto* context = script::ScriptExtensionHost::context();
  return context ? context->nowMs : (clock_ ? clock_() : 0);
}

int AudioScripting::beat(bvm* vm) {
  auto& self = *static_cast<AudioScripting*>(script::BerryVM::nativeSelf(vm));
  double value = 0;
  if (!self.beat_ || !self.beat_(self.now(), value)) be_return_nil(vm);
  be_pushreal(vm, static_cast<breal>(value));
  be_return(vm);
}

int AudioScripting::pitch(bvm* vm) {
  auto& self = *static_cast<AudioScripting*>(script::BerryVM::nativeSelf(vm));
  be_pushreal(vm, static_cast<breal>(self.pitch_ ? self.pitch_(self.now()) : 0));
  be_return(vm);
}

int AudioScripting::effect(bvm* vm) {
  auto& self = *static_cast<AudioScripting*>(script::BerryVM::nativeSelf(vm));
  int answer = 0;
  std::string error;
  if (self.sound_ && be_top(vm) >= 1 && be_isstring(vm, 1))
    answer = self.sound_(script::SoundAction::Effect, be_tostring(vm, 1),
                         script::ScriptExtensionHost::caller(), error);
  if (answer < 0) be_pushstring(vm, error.c_str());
  else be_pushbool(vm, answer > 0);
  be_return(vm);
}

}
