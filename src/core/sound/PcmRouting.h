#pragma once

#include "core/sound/PcmPlayback.h"
#include "core/sound/SoundMp3.h"

namespace awtrix::sound {

// Render-thread routing for a PCM sink's one-shot, effect and music layers. A sink that does not
// mix plays every sound as its one-shot, and music repeats as the router repeats any sound.
class PcmRouting {
 public:
  template<class Sink>
  static sound::Caps caps(const Sink& sink) {
    sound::Caps result;
    result.mp3 = result.radio = true;
    result.song = sink.synthesizes();
    result.speech = sink.speaks();
    result.url = sink.fetches();
    result.effect = sink.mixes();
    result.clip = sink.clips();
    return result;
  }

  template<class Sink>
  static bool check(Sink& sink, const sound::Spec& spec, DispatchDetail& detail) {
    if (spec.kind == sound::Kind::Song && sink.synthesizes() &&
        !sink.checkSong(spec.text, detail.message)) {
      detail.field = "song";
      return false;
    }
    if (spec.kind == sound::Kind::Speech && sink.speaks() &&
        !sink.checkSpeech(spec.text, detail)) {
      detail.field = "speech";
      return false;
    }
    return true;
  }

  template<class Sink>
  sound::PcmPlay play(Sink& sink, const sound::PcmRequest& request, DispatchDetail& detail) {
    using namespace sound;
    const Spec& spec = request.spec;
    const bool effect = request.as == PlayAs::Effect;
    const bool music = request.group == Group::App && spec.loop && !effect && sink.mixes();
    const bool layered = music || (effect && sink.mixes());
    bool started = false;
    bool layer = false;
    if (spec.kind == sound::Kind::Song) {
      if (!sink.checkSong(spec.text, detail.message)) {
        detail.field = "song";
        return PcmPlay::Invalid;
      }
      if (!layered && !request.allowOneShot) return PcmPlay::Ignored;
      started = music ? sink.playSong(spec.text, spec.nextBar)
                      : layered ? sink.playFx(spec.text) : sink.playSongOnce(spec.text, request.group);
      layer = layered;
    } else if (spec.kind == sound::Kind::Speech) {
      if (!request.allowOneShot) return PcmPlay::Ignored;
      const auto result = sink.playSpeech(spec.text, request.group, detail);
      if (result == DispatchResult::ValidationError) {
        if (detail.field.empty()) detail.field = "speech";
        return PcmPlay::Invalid;
      }
      started = result == DispatchResult::Ok;
    } else if (isUrl(spec.text)) {
      if (!music && !request.allowOneShot) return PcmPlay::Ignored;
      started = sink.playUrl(spec.text, request.group, music);
      layer = music;
    } else {
      if (!layered && !request.allowOneShot) return PcmPlay::Ignored;
      started = music ? sink.playLoop(request.path)
                      : layered ? sink.playEffect(request.path) : sink.playMp3(request.path, request.group);
      layer = layered;
    }
    if (!started) return PcmPlay::Unavailable;
    if (!layer) return PcmPlay::OneShot;
    (music ? musicOwner_ : effectsOwner_) = request.owner;
    return PcmPlay::Layer;
  }

  template<class Sink>
  void stop(Sink& sink, sound::Stop what, const std::string& owner) {
    using sound::Stop;
    const bool all = what == Stop::All || what == Stop::App;
    const bool own = !owner.empty() && (what == Stop::ScriptSounds || what == Stop::ScriptMusic);
    if (all || (own && musicOwner_ == owner)) {
      sink.stopLoop();
      musicOwner_.clear();
    }
    if (all || (own && what == Stop::ScriptSounds && effectsOwner_ == owner)) {
      sink.stopEffects();
      effectsOwner_.clear();
    }
  }

  template<class Sink>
  sound::PcmState state(Sink& sink) {
    sound::PcmState result;
    result.oneShot = sink.oneShotPlaying();
    result.groupKnown = sink.oneShotGroup(result.group);
    result.effects = sink.effectsPlaying();
    result.music = sink.loopPlaying();
    observe(result);
    return result;
  }

  void observe(const sound::PcmState& result) {
    if (!result.effects) effectsOwner_.clear();
    if (!result.music) musicOwner_.clear();
  }

  void clear() { musicOwner_.clear(); effectsOwner_.clear(); }

 private:
  std::string musicOwner_;
  std::string effectsOwner_;
};

}
