#include "platform/tc002/audio/Tc002AudioSink.h"

#include <algorithm>

#include "core/sound/VolumeCurve.h"
#include "platform/tc002/audio/Tc002AudioStream.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"

namespace awtrix {
namespace tc002 {

Tc002AudioPlayer::Tap Tc002AudioSink::analysisTap() {
  return [this](const int16_t* samples, std::size_t frames, uint8_t channels, uint32_t rate,
                int64_t leadMs) {
    const int64_t now = monotonicMs();
    if (playing_ == Playing::Mix && mixer_) markSong(now + leadMs);
    if (mixer_ && mixer_->takeOneShotStarted() && oneShot_ == Kind::System &&
        systemSeq_.load() == mixedOneShotSeq_ && systemStart_.load() == SystemStart::Pending) {
      systemAudibleAtMs_.store(now + leadMs);
      SystemStart pending = SystemStart::Pending;
      systemStart_.compare_exchange_strong(pending, SystemStart::Audible);
    }
    audio::FrameStats stats;
    if (stats_.wanted(now) &&
        analyzer_.analyze(samples, static_cast<int>(frames), channels, static_cast<int>(rate), stats))
      stats_.publish(stats, now + leadMs);
    pitch_.feed(samples, frames, channels, rate, now, now + leadMs);
  };
}

void Tc002AudioSink::startStream(const Request& request, int64_t nowMs) {
  std::unique_ptr<StreamSource> source(new StreamSource(*request.url));
  stream_ = source.get();
  player_.play(std::move(source), request.volumes.radio, nowMs, analysisTap());
  playing_ = Playing::Stream;
}

// A new mixer takes the speaker from a station, or from a mixer that already sent its last block
// and only plays out; that one had nothing left to lose.
void Tc002AudioSink::startMixer(uint8_t volume, int64_t nowMs) {
  if (playing_ == Playing::Mix) oneShotEnded();
  stream_ = nullptr;
  std::unique_ptr<MixSource> mix(new MixSource(effectCache_));
  mixer_ = mix.get();
  player_.play(std::move(mix), volume, nowMs, analysisTap());
  playing_ = Playing::Mix;
}

// The speaker plays at the loudest level that sounds and the mixer scales the quieter layers on
// the speaker's own curve. The one-shot plays at its group's level, effects and loop at the app's.
// With every level that sounds at 0 the speaker goes silent, also in the middle of a sound.
void Tc002AudioSink::applyVolumes(const Request& request) {
  if (playing_ == Playing::Stream) {
    player_.setVolume(request.volumes.radio);
    return;
  }
  if (playing_ != Playing::Mix) return;
  const uint8_t oneShot = oneShot_ != Kind::None ? request.volumes.of(oneShotGroup_) : 0;
  const uint8_t app = mixer_->voices() || mixer_->looping() ? request.volumes.app : 0;
  const uint8_t link = std::max(oneShot, app);
  mixer_->setDuckEffects(oneShot_ != Kind::None && oneShotGroup_ == sound::Group::Alert);
  if (!link) {
    player_.setVolume(0);
    return;
  }
  const int32_t appGain = sound::volumeGain(app, link);
  mixer_->setGains(sound::volumeGain(oneShot, link), appGain, appGain);
  player_.setVolume(link);
}

// The voice assistant holds the station and the loop back; the loop starts over once it lets go.
void Tc002AudioSink::reconcile(int64_t nowMs) {
  const bool voice = voiceOwned_.load();
  Request request;
  std::vector<Effect> effects;
  std::vector<std::string> released;
  bool oneShotChanged, effectsStopped, loopChanged, streamChanged;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    oneShotChanged = request_.oneShotSeq != playingOneShotSeq_;
    if (oneShotChanged) {
      request.oneShotSeq = request_.oneShotSeq;
      request.oneShot = request_.oneShot;
      request.oneShotGroup = request_.oneShotGroup;
      request.notes = request_.notes;
      request.timeUnit = request_.timeUnit;
      request.mp3Path = request_.mp3Path;
      request.mp3File = request_.mp3File;
      request.systemPath = request_.systemPath;
      request.systemSkipFrames = request_.systemSkipFrames;
      request.clip = std::move(request_.clip);
      request.speech = std::move(request_.speech);
      request.oneShotSong = std::move(request_.oneShotSong);
    }
    effects.swap(request_.effects);
    released.swap(request_.released);
    effectsStopped = request_.effectsStopSeq != playingEffectsStopSeq_;
    request.effectsStopSeq = request_.effectsStopSeq;
    loopChanged = voice ? !loopHeld_
                        : request_.loopSeq != playingLoopSeq_ || (loopHeld_ && !request_.loop.empty());
    request.loopSeq = request_.loopSeq;
    if (!voice && loopChanged) request.loop = request_.loop;
    streamChanged = request_.streamSeq != playingStreamSeq_;
    request.streamSeq = request_.streamSeq;
    request.streamWanted = request_.streamWanted;
    request.streamHeld = request_.streamHeld;
    request.url = request_.url;
    request.volumes = request_.volumes;
  }

  std::unique_ptr<PcmSource> source;
  if (oneShotChanged) {
    playingOneShotSeq_ = request.oneShotSeq;
    if (request.oneShot != Kind::System) systemGone(request.oneShotSeq);
    if (request.oneShot != Kind::None) {
      source = openOneShot(request);
      if (!source) {
        if (request.oneShot == Kind::System) systemGone(request.oneShotSeq);
        finishOneShot(request.oneShotSeq);
        request.oneShot = Kind::None;
      }
    }
  }
  playingEffectsStopSeq_ = request.effectsStopSeq;
  playingLoopSeq_ = request.loopSeq;
  loopHeld_ = voice;

  const bool wanted = source || !effects.empty() || (loopChanged && !request.loop.empty());
  if (wanted && (playing_ != Playing::Mix || player_.draining()))
    startMixer(source ? request.volumes.of(request.oneShotGroup) : request.volumes.app, nowMs);
  if (playing_ == Playing::Mix) {
    if (oneShotChanged) {
      oneShotEnded();
      mixer_->setOneShot(std::move(source));
      if (mixer_->oneShotActive()) {
        oneShot_ = request.oneShot;
        oneShotGroup_ = request.oneShotGroup;
        mixedOneShotSeq_ = request.oneShotSeq;
      }
    }
    if (effectsStopped) mixer_->stopEffects();
    bool releasedEffects = false;
    for (const std::string& place : released)
      releasedEffects = mixer_->stopEffectsFrom(place) || releasedEffects;
    for (const Effect& effect : effects) {
      if (effect.song) mixer_->addFx(effect.song);
      else if (!mixer_->addEffect(effect.mp3))
        logf("MP3 %s disappeared before playback", effect.mp3.c_str());
    }
    if (loopChanged) {
      if (request.loop.song) mixer_->setSong(request.loop.song, request.loop.nextBar);
      else if (!mixer_->setLoop(request.loop.mp3))
        logf("MP3 %s disappeared before playback", request.loop.mp3.c_str());
    }
    // A stop that leaves nothing to play silences at once instead of playing out the queue.
    const bool stopped = (oneShotChanged && request.oneShot == Kind::None) || effectsStopped ||
                         (loopChanged && request.loop.empty()) || releasedEffects;
    if (stopped && mixer_->idle()) {
      player_.stop();
      mixer_ = nullptr;
      playing_ = Playing::None;
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    effectsPending_ = effectsPending_ > effects.size()
                          ? effectsPending_ - static_cast<uint32_t>(effects.size())
                          : 0;
    effectVoices_.store(mixer_ ? static_cast<uint32_t>(mixer_->voices()) : 0);
  }

  // A one-shot that repeats keeps the station away between its plays.
  if (streamChanged || voice) {
    playingStreamSeq_ = request.streamSeq;
    if (playing_ == Playing::Stream) {
      player_.stop();
      stream_ = nullptr;
      playing_ = Playing::None;
    }
  }
  if (playing_ == Playing::None && request.streamWanted && !voice && !request.streamHeld)
    startStream(request, nowMs);
  applyVolumes(request);
}

}
}
