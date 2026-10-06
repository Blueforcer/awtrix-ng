#include "platform/tc002/audio/Tc002AudioPlayer.h"

#include <algorithm>

namespace awtrix {
namespace tc002 {

void Tc002AudioPlayer::play(std::unique_ptr<PcmSource> source, uint8_t volume, int64_t nowMs,
                            Tap tap) {
  stop();
  outcome_ = Outcome::None;
  finished_.reset();
  source_ = std::move(source);
  tap_ = std::move(tap);
  volume_ = volume;
  link_.setVolume(volume);
  readyDeadlineMs_ = nowMs + kReadyTimeoutMs;
}

void Tc002AudioPlayer::stop() {
  if (!source_) return;
  if (generation_) link_.stop();
  source_.reset();
  tap_ = nullptr;
  generation_ = 0;
  pending_.clear();
  offset_ = 0;
  draining_ = false;
  waiting_ = false;
}

void Tc002AudioPlayer::setVolume(uint8_t percent) {
  if (percent == volume_) return;
  volume_ = percent;
  if (source_) link_.setVolume(percent);
}

void Tc002AudioPlayer::finish(Outcome outcome) {
  outcome_ = outcome;
  finished_ = std::move(source_);
  tap_ = nullptr;
  generation_ = 0;
  pending_.clear();
  offset_ = 0;
  draining_ = false;
  waiting_ = false;
}

Tc002AudioPlayer::Outcome Tc002AudioPlayer::takeOutcome(std::unique_ptr<PcmSource>* finished) {
  const Outcome outcome = outcome_;
  outcome_ = Outcome::None;
  if (finished) *finished = std::move(finished_);
  finished_.reset();
  return outcome;
}

int Tc002AudioPlayer::waitMs() const {
  if (!source_) return -1;
  return waiting_ ? 10 : 50;
}

void Tc002AudioPlayer::pump(int64_t nowMs) {
  link_.poll();
  if (!source_) return;
  if (link_.failed()) {
    finish(Outcome::Failed);
    return;
  }
  if (!link_.ready()) {
    if (nowMs >= readyDeadlineMs_) finish(Outcome::Failed);
    return;
  }
  if (draining_) {
    if (link_.finished(generation_)) {
      finish(ending_);
    } else if (nowMs >= drainDeadlineMs_) {
      link_.stop();
      finish(Outcome::Failed);
    }
    return;
  }
  if (generation_ && link_.finished(generation_)) {
    generation_ = source_->endless() ? link_.open(rate_, channels_) : 0;
    if (!generation_) {
      finish(Outcome::Failed);
      return;
    }
  }
  fill(nowMs);
}

bool Tc002AudioPlayer::fill(int64_t nowMs) {
  waiting_ = false;
  for (int rounds = 0; rounds < 64; ++rounds) {
    if (offset_ >= pending_.size()) {
      const uint32_t lead = source_->leadMs();
      if (lead && generation_ && link_.queuedMs() >= lead) {
        waiting_ = true;
        return true;
      }
      const int16_t* samples = nullptr;
      std::size_t frames = 0;
      const PcmSource::Read read = source_->next(samples, frames);
      if (read == PcmSource::Read::Wait) {
        waiting_ = true;
        return true;
      }
      const uint32_t rate = source_->rate();
      const uint8_t channels = source_->channels();
      const bool usable = read == PcmSource::Read::Data && frames && rate >= TC002_AUDIO_MIN_RATE &&
                          rate <= TC002_AUDIO_MAX_RATE && channels >= 1 && channels <= 2;
      if (!usable) {
        ending_ = read == PcmSource::Read::End ? Outcome::Finished : Outcome::Failed;
        if (!generation_) {
          finish(ending_);
          return false;
        }
        link_.drain();
        draining_ = true;
        drainDeadlineMs_ = nowMs + link_.queuedMs() + kDrainSlackMs;
        return false;
      }
      if (!generation_ || rate != rate_ || channels != channels_) {
        if (generation_) link_.stop();
        generation_ = link_.open(rate, channels);
        rate_ = rate;
        channels_ = channels;
        if (!generation_) {
          finish(Outcome::Failed);
          return false;
        }
      }
      if (tap_) tap_(samples, frames, channels, rate, link_.queuedMs());
      pending_.assign(samples, samples + frames * channels);
      offset_ = 0;
    }
    std::size_t count = std::min(pending_.size() - offset_, link_.credit() / 2);
    count -= count % channels_;
    if (!count || !link_.sendPcm(pending_.data() + offset_, count)) return true;
    offset_ += count;
  }
  return true;
}

}
}
