#include "platform/tc002/audio/Tc002AudioSink.h"

#include <algorithm>

#include "platform/linux/host/HostStore.h"
#include "platform/tc002/audio/Tc002AudioStream.h"
#include "platform/tc002/speech/SpeechSource.h"
#include "system/Log.h"
#include "system/MonotonicClock.h"

namespace awtrix {
namespace tc002 {

namespace {
class SkippingSource final : public PcmSource {
 public:
  SkippingSource(std::unique_ptr<PcmSource> inner, uint32_t frames)
      : inner_(std::move(inner)), skip_(frames) {}
  Read next(const int16_t*& samples, std::size_t& frames) override {
    for (;;) {
      const Read read = inner_->next(samples, frames);
      if (read != Read::Data || skip_ == 0) return read;
      const std::size_t dropped = std::min<std::size_t>(skip_, frames);
      skip_ -= static_cast<uint32_t>(dropped);
      frames -= dropped;
      samples += dropped * inner_->channels();
      if (frames) return Read::Data;
    }
  }
  uint32_t rate() const override { return inner_->rate(); }
  uint8_t channels() const override { return inner_->channels(); }

 private:
  std::unique_ptr<PcmSource> inner_;
  uint32_t skip_;
};

}

// Called with the sequence number of a one-shot that ended, failed or took over; a system sound
// requested before it and not yet audible will never start.
void Tc002AudioSink::systemGone(uint32_t seq) {
  if (static_cast<int32_t>(seq - systemSeq_.load()) < 0) return;
  SystemStart pending = SystemStart::Pending;
  systemStart_.compare_exchange_strong(pending, SystemStart::Failed);
  systemFinished_.store(true);
}

void Tc002AudioSink::finishOneShot(uint32_t seq) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (request_.oneShotSeq != seq) return;
  request_.oneShot = Kind::None;
  request_.speech.reset();
}

// The one-shot in the mixer was heard to its end, was replaced, or went with the mixer.
void Tc002AudioSink::oneShotEnded() {
  if (oneShot_ == Kind::System) systemGone(mixedOneShotSeq_);
  if (oneShot_ != Kind::None) finishOneShot(mixedOneShotSeq_);
  oneShot_ = Kind::None;
  oneShotEnding_ = false;
}

std::unique_ptr<PcmSource> Tc002AudioSink::openOneShot(const Request& request) {
  if (request.oneShot == Kind::Speech) {
    std::unique_ptr<PcmSource> source = speech::SpeechSource::open(voice_, request.speech);
    if (!source) logf("speech: no thread for the voice");
    return source;
  }
  if (request.oneShot == Kind::Tone)
    return std::unique_ptr<PcmSource>(new ToneSource(request.notes, request.timeUnit));
  if (request.oneShot == Kind::Clip) return openClip(request.clip);
  if (request.oneShot == Kind::Song) return MixSource::songOnce(request.oneShotSong);
  const bool system = request.oneShot == Kind::System;
  const std::string path = system                   ? request.systemPath
                           : !request.mp3File.empty() ? request.mp3File
                                                      : host::hostPath(request.mp3Path);
  if (system) {
    auto wav = std::make_unique<WavFileSource>(path);
    if (wav->opened()) return wav;
  }
  std::unique_ptr<Mp3FileSource> mp3(new Mp3FileSource(path));
  if (!mp3->opened()) {
    logf("MP3 %s disappeared before playback", system ? path.c_str() : request.mp3Path.c_str());
    return nullptr;
  }
  if (system)
    return std::unique_ptr<PcmSource>(new SkippingSource(std::move(mp3), request.systemSkipFrames));
  return std::unique_ptr<PcmSource>(std::move(mp3));
}

// A one-shot that ended in a mixer with nothing else to play ends with the mixer's play-out;
// under other layers it ends once what was queued ahead of it has been heard.
void Tc002AudioSink::afterPump() {
  if (mixer_) {
    const int64_t now = monotonicMs();
    const auto end = mixer_->takeOneShotEnd();
    if (end == MixSource::End::Failed && oneShot_ == Kind::System) systemStart_.store(SystemStart::Failed);
    if (end == MixSource::End::Failed && oneShot_ == Kind::Speech) logf("speech: the voice failed");
    if (end != MixSource::End::None) {
      oneShotEnding_ = true;
      oneShotEndsAtMs_ = now + link_.queuedMs();
    }
    if (oneShotEnding_ && !mixer_->idle() && now >= oneShotEndsAtMs_) oneShotEnded();
    effectVoices_.store(static_cast<uint32_t>(mixer_->voices()));
  }
  std::unique_ptr<PcmSource> finished;
  const Tc002AudioPlayer::Outcome outcome = player_.takeOutcome(&finished);
  if (outcome != Tc002AudioPlayer::Outcome::None) {
    again_ = true;
    const Playing was = playing_;
    playing_ = Playing::None;
    if (was == Playing::Mix) {
      if (outcome == Tc002AudioPlayer::Outcome::Failed && oneShot_ == Kind::System) systemStart_.store(SystemStart::Failed);
      oneShotEnded();
      mixer_ = nullptr;
      effectVoices_.store(0);
      // The loop went with the mixer, played to its end or failed; a newer one still plays, and
      // one the voice holds back was never in it.
      std::lock_guard<std::mutex> lock(mutex_);
      if (request_.loopSeq == playingLoopSeq_ && !loopHeld_) request_.loop.clear();
    } else if (was == Playing::Stream) {
      std::string text;
      const bool explained = stream_ && stream_->takeError(text);
      stream_ = nullptr;
      bool wanted;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted = request_.streamSeq == playingStreamSeq_ && request_.streamWanted;
        if (wanted) {
          request_.streamWanted = false;
          request_.url.reset();
        }
      }
      if (explained) {
        publishError(text);
      } else if (wanted && outcome == Tc002AudioPlayer::Outcome::Failed && !link_.ready()) {
        publishError("speaker unavailable");
      }
    }
  }
  finished.reset();
  if (link_.failed() && available_.load()) {
    available_.store(false);
    systemGone(systemSeq_.load());
    logf("TC002 speaker unavailable: %s", link_.failure().c_str());
    bool streamWanted;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      streamWanted = request_.streamWanted;
      request_.streamWanted = false;
      request_.url.reset();
      request_.oneShot = Kind::None;
      request_.clip.reset();
      request_.speech.reset();
      request_.effects.clear();
      request_.loop.clear();
      effectsPending_ = 0;
    }
    if (streamWanted) publishError("speaker unavailable");
  }
  if (stream_) {
    std::string text;
    if (stream_->takeError(text)) publishError(text);
    if (stream_->takeTitle(text)) publishTitle(text);
    bufferBytes_.store(stream_->bufferBytes());
    starvedMs_.store(stream_->starvedMs());
    decodeUs_.store(stream_->decodeUs());
  }
  underruns_.store(link_.status().underruns);
}

}
}
