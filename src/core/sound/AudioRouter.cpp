#include "core/sound/AudioRouter.h"

#include "core/sound/Rtttl.h"
#include "core/sound/SoundMp3.h"

namespace awtrix {
namespace sound {
namespace {

bool trackNumber(const std::string& value, int& out) {
  if (value.empty() || value.size() > 4) return false;
  int n = 0;
  for (char c : value) {
    if (c < '0' || c > '9') return false;
    n = n * 10 + (c - '0');
  }
  if (n < kMinTrack || n > kMaxTrack) return false;
  out = n;
  return true;
}

const char* kNoBuzzer = "this device has no buzzer";
const char* kNoTrackSink = "this device has no DFPlayer";
const char* kNoPcm = "this device has no MP3 output";
const char* kBadTrack = "must be a number between 1 and 2999";

}

PlayResult AudioRouter::play(Source source, const std::string& value, DispatchDetail& detail) {
  if (muted_) return PlayResult::Muted;

  switch (source) {
    case Source::Auto:
      return playAuto(value, detail);

    case Source::Mp3: {
      if (!pcm_) {
        detail.message = kNoPcm;
        return PlayResult::NoSink;
      }
      const std::string path = mp3PathFor(value);
      if (path.empty() || !assets_ || !assets_->hasMp3(value) || !pcm_->playMp3(path)) {
        detail.message = "no MP3 called \"" + value + "\"";
        return PlayResult::NotFound;
      }
      stopOthersThan(Sink::Pcm);
      return PlayResult::Ok;
    }

    case Source::Melody: {
      if (!tone_) {
        detail.message = kNoBuzzer;
        return PlayResult::NoSink;
      }
      if (!tone_->playMelodyFile(value)) {
        detail.message = "no melody called \"" + value + "\"";
        return PlayResult::NotFound;
      }
      stopOthersThan(Sink::Tone);
      return PlayResult::Ok;
    }

    // Payload before hardware, so a malformed request reads the same on every panel.
    case Source::Track: {
      int number = 0;
      if (!trackNumber(value, number)) {
        detail = {"track", kBadTrack};
        return PlayResult::Invalid;
      }
      if (!track_) {
        detail.message = kNoTrackSink;
        return PlayResult::NoSink;
      }
      if (!track_->playTrack(number)) {
        detail = {"track", kBadTrack};
        return PlayResult::Invalid;
      }
      stopOthersThan(Sink::Track);
      return PlayResult::Ok;
    }

    case Source::Rtttl: {
      const rtttl::Parse parsed = rtttl::parse(value);
      if (!parsed.ok) {
        detail = {"rtttl", parsed.describe()};
        return PlayResult::Invalid;
      }
      if (!tone_) {
        detail.message = kNoBuzzer;
        return PlayResult::NoSink;
      }
      if (!tone_->playRtttl(value)) {
        detail = {"rtttl", "the melody could not be played"};
        return PlayResult::Invalid;
      }
      stopOthersThan(Sink::Tone);
      return PlayResult::Ok;
    }
  }
  return PlayResult::NotFound;
}

// The DFPlayer goes last because its card is not listable over the UART, so playTrack() always
// "succeeds": asked first it would swallow every numeric name and hide /MP3/7.mp3.
PlayResult AudioRouter::playAuto(const std::string& name, DispatchDetail& detail) {
  if (pcm_ && assets_ && assets_->hasMp3(name)) {
    const std::string path = mp3PathFor(name);
    if (!path.empty() && pcm_->playMp3(path)) {
      stopOthersThan(Sink::Pcm);
      return PlayResult::Ok;
    }
  }
  if (tone_ && tone_->playMelodyFile(name)) {
    stopOthersThan(Sink::Tone);
    return PlayResult::Ok;
  }
  int number = 0;
  if (track_ && trackNumber(name, number) && track_->playTrack(number)) {
    stopOthersThan(Sink::Track);
    return PlayResult::Ok;
  }
  detail.message = "nothing called \"" + name + "\"";
  return PlayResult::NotFound;
}

DispatchResult AudioRouter::playStream(const std::string& url, const std::string& label,
                                       DispatchDetail& detail) {
  if (!pcm_) {
    detail.message = "this build has no audio output";
    return DispatchResult::Unavailable;
  }
  return pcm_->playStream(url, label, detail);
}

void AudioRouter::stop(StopScope scope) {
  if (scope != StopScope::Stream) {
    if (tone_) tone_->stop();
    if (track_) track_->stop();
    if (pcm_) pcm_->stopMp3();
  }
  if (scope != StopScope::Sounds && pcm_) pcm_->stopStream();
}

// After the new sound started, so a failed lookup takes no playing one with it. The starting
// sink is spared: on the I2S output a stop/start pair races the audio task.
void AudioRouter::stopOthersThan(Sink keep) {
  if (tone_ && keep != Sink::Tone) tone_->stop();
  if (track_ && keep != Sink::Track) track_->stop();
  if (pcm_ && keep != Sink::Pcm) pcm_->stopMp3();
}

void AudioRouter::setVolumes(uint8_t buzzerPercent, uint8_t trackPercent, uint8_t mp3Percent,
                             uint8_t streamPercent) {
  if (tone_ && buzzerVolume_ != buzzerPercent) {
    buzzerVolume_ = buzzerPercent;
    tone_->setVolume(buzzerPercent);
  }
  if (track_ && trackVolume_ != trackPercent) {
    trackVolume_ = trackPercent;
    track_->setVolume(trackPercent);
  }
  if (pcm_) {
    if (mp3Volume_ != mp3Percent) {
      mp3Volume_ = mp3Percent;
      pcm_->setSoundVolume(mp3Percent);
    }
    if (streamVolume_ != streamPercent) {
      streamVolume_ = streamPercent;
      pcm_->setStreamVolume(streamPercent);
    }
  }
}

void AudioRouter::tick(int64_t nowMs) {
  if (tone_) tone_->tick();
  if (track_) track_->tick();
  if (pcm_) pcm_->tick(nowMs);
}

bool AudioRouter::isPlaying() const {
  if (tone_ && tone_->isPlaying()) return true;
  if (track_ && track_->isPlaying()) return true;
  return pcm_ && pcm_->mp3Playing();
}

Caps AudioRouter::caps() const {
  // One sink answers both today; they stay apart because callers ask them apart.
  const bool pcm = pcm_ != nullptr;
  return {tone_ != nullptr, track_ != nullptr, pcm, pcm};
}

}
}
