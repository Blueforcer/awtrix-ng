#include "platform/tc002/audio/Tc002AudioMixer.h"

#include <sys/stat.h>

#include <algorithm>
#include <cmath>

#include "core/audio/GainRamp.h"
#include "core/sound/SoundMp3.h"

namespace awtrix {
namespace tc002 {
namespace {

uint32_t stepFor(uint32_t rate) {
  return static_cast<uint32_t>((static_cast<uint64_t>(rate) << 16) / MixSource::kRate);
}

int32_t lerp(int32_t a, int32_t b, uint32_t fraction) {
  return a + static_cast<int32_t>((static_cast<int64_t>(b - a) * fraction) >> 16);
}

using audio::GainRamp;
using audio::scaled;

// Only samples whose interpolation neighbour is already buffered belong to the
// fast block. Its last sample still uses the source's normal refill/Wait path.
std::size_t bufferedFrames(uint64_t position, uint32_t step, uint64_t end,
                           std::size_t wanted) {
  if (end < 2 || (position >> 16) >= end - 1) return 0;
  const uint64_t distance = ((end - 1) << 16) - position;
  if (!step || distance > static_cast<uint64_t>(wanted - 1) * step) return wanted;
  return static_cast<std::size_t>(1 + (distance - 1) / step);
}

void mixBuffered(int32_t* sum, const int16_t* samples, uint64_t base, uint64_t& position,
                 uint32_t step, std::size_t frames, int32_t from, int32_t to, GainRamp& gain) {
  uint64_t cursor = position - (base << 16);
  if (from == to) {
    for (std::size_t i = 0; i < frames; ++i, cursor += step) {
      const std::size_t index = static_cast<std::size_t>(cursor >> 16);
      sum[i] += scaled(lerp(samples[index], samples[index + 1], cursor & 0xffffu), from);
    }
  } else {
    for (std::size_t i = 0; i < frames; ++i, cursor += step) {
      const std::size_t index = static_cast<std::size_t>(cursor >> 16);
      sum[i] += scaled(lerp(samples[index], samples[index + 1], cursor & 0xffffu), gain.next());
    }
  }
  position = cursor + (base << 16);
}

// Adds a block of synthesizer output with its gain ramping from `from` to `to`.
void mixSynth(int32_t* sum, const float* in, std::size_t frames, int32_t from, int32_t to) {
  if (from == to) {
    for (std::size_t i = 0; i < frames; ++i)
      sum[i] += scaled(static_cast<int32_t>(in[i] * 32767.0f), from);
    return;
  }
  GainRamp gain(from, to, frames);
  for (std::size_t i = 0; i < frames; ++i)
    sum[i] += scaled(static_cast<int32_t>(in[i] * 32767.0f), gain.next());
}

void appendMono(std::vector<int16_t>& to, const int16_t* samples, std::size_t frames,
                uint8_t channels) {
  if (channels == 1) {
    to.insert(to.end(), samples, samples + frames);
    return;
  }
  for (std::size_t i = 0; i < frames; ++i)
    to.push_back(static_cast<int16_t>((samples[2 * i] + samples[2 * i + 1]) / 2));
}

}

bool EffectCache::stamp(const std::string& path, int64_t& size, int64_t& mtime) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) return false;
  size = static_cast<int64_t>(st.st_size);
  mtime = static_cast<int64_t>(st.st_mtime);
  return true;
}

std::shared_ptr<const Clip> EffectCache::find(const std::string& path) {
  auto it = entries_.find(path);
  if (it == entries_.end()) return nullptr;
  int64_t size = 0, mtime = 0;
  if (!stamp(path, size, mtime) || size != it->second.size || mtime != it->second.mtime) {
    bytes_ -= it->second.clip->samples.capacity() * sizeof(int16_t);
    entries_.erase(it);
    return nullptr;
  }
  it->second.used = ++clock_;
  return it->second.clip;
}

void EffectCache::put(const std::string& path, std::shared_ptr<const Clip> clip) {
  const std::size_t cost = clip->samples.capacity() * sizeof(int16_t);
  Entry entry;
  if (cost > kBudgetBytes || !stamp(path, entry.size, entry.mtime)) return;
  auto it = entries_.find(path);
  if (it != entries_.end()) {
    bytes_ -= it->second.clip->samples.capacity() * sizeof(int16_t);
    entries_.erase(it);
  }
  entry.clip = std::move(clip);
  entry.used = ++clock_;
  entries_[path] = std::move(entry);
  bytes_ += cost;
  trim();
}

void EffectCache::trim() {
  while (bytes_ > kBudgetBytes && !entries_.empty()) {
    auto oldest = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
      if (it->second.used < oldest->second.used) oldest = it;
    bytes_ -= oldest->second.clip->samples.capacity() * sizeof(int16_t);
    entries_.erase(oldest);
  }
}

// One effect: a cached clip, or a file decoding while it plays whose recording joins the cache
// once it decoded to the end.
class MixSource::Voice {
 public:
  Voice(std::string path, std::shared_ptr<const Clip> clip)
      : clip_(std::move(clip)), path_(std::move(path)), step_(stepFor(clip_->rate)) {}
  Voice(std::string path, std::unique_ptr<Mp3FileSource> live)
      : path_(std::move(path)), live_(std::move(live)), recording_(new Clip()) {}

  const std::string& path() const { return path_; }

  // Adds this block with its gain ramping from `from` to `to`; returns how many frames it sounded.
  std::size_t mix(int32_t* sum, std::size_t frames, int32_t from, int32_t to, EffectCache& cache) {
    GainRamp gain(from, to, frames);
    std::size_t i = 0;
    while (i < frames) {
      const std::size_t index = static_cast<std::size_t>(position_ >> 16);
      if (!have(index, cache)) break;
      const std::vector<int16_t>& s = samples();
      const std::size_t buffered = bufferedFrames(position_, step_, s.size(), frames - i);
      if (buffered) {
        mixBuffered(sum + i, s.data(), 0, position_, step_, buffered, from, to, gain);
        i += buffered;
        continue;
      }
      const int32_t a = s[index];
      const int32_t b = have(index + 1, cache) ? s[index + 1] : a;
      sum[i] += scaled(lerp(a, b, static_cast<uint32_t>(position_ & 0xffffu)), gain.next());
      position_ += step_;
      ++i;
    }
    return i;
  }

  bool done(EffectCache& cache) { return !have(static_cast<std::size_t>(position_ >> 16), cache); }

 private:
  const std::vector<int16_t>& samples() const {
    return clip_ ? clip_->samples : recording_->samples;
  }

  bool have(std::size_t index, EffectCache& cache) {
    if (index < samples().size()) return true;
    while (live_ && recording_->samples.size() <= index) decode(cache);
    return index < samples().size();
  }

  void decode(EffectCache& cache) {
    const int16_t* samples = nullptr;
    std::size_t frames = 0;
    const PcmSource::Read read = live_->next(samples, frames);
    if (read == PcmSource::Read::Data) {
      const uint32_t rate = live_->rate();
      if (!recording_->rate) {
        recording_->rate = rate;
        step_ = stepFor(rate);
      }
      const uint64_t limit = static_cast<uint64_t>(recording_->rate) * kMaxEffectMs / 1000;
      if (rate == recording_->rate && recording_->samples.size() < limit) {
        appendMono(recording_->samples, samples, frames, live_->channels());
        return;
      }
    } else if (read == PcmSource::Read::End && !recording_->samples.empty()) {
      cache.put(path_, recording_);
    }
    live_.reset();
  }

  std::shared_ptr<const Clip> clip_;
  std::string path_;
  std::unique_ptr<Mp3FileSource> live_;
  std::shared_ptr<Clip> recording_;
  uint64_t position_ = 0;
  uint32_t step_ = 0;
};

// A one-shot plays its source once. A loop decodes its file pass after pass into one endless
// stream, each pass without its leading codec silence, so the loop point has no gap.
class MixSource::Track {
 public:
  explicit Track(std::unique_ptr<PcmSource> once) : source_(std::move(once)) {}
  explicit Track(std::string loopPath) : path_(std::move(loopPath)) {}

  const std::string& path() const { return path_; }
  bool open() { return path_.empty() ? source_ != nullptr : restart(); }
  bool failed() const { return failed_; }
  bool done() const { return ended_ && (position_ >> 16) >= base_ + buffer_.size(); }

  // Adds this block with its gain ramping from `from` to `to`; returns how many frames it sounded.
  std::size_t mix(int32_t* sum, std::size_t frames, int32_t from, int32_t to) {
    GainRamp gain(from, to, frames);
    std::size_t i = 0;
    while (i < frames) {
      const uint64_t index = position_ >> 16;
      if (!have(index)) break;
      const std::size_t buffered = bufferedFrames(position_, step_, base_ + buffer_.size(), frames - i);
      if (buffered) {
        mixBuffered(sum + i, buffer_.data(), base_, position_, step_, buffered, from, to, gain);
        i += buffered;
        continue;
      }
      const int32_t a = buffer_[index - base_];
      const int32_t b = have(index + 1) ? buffer_[index + 1 - base_] : a;
      sum[i] += scaled(lerp(a, b, static_cast<uint32_t>(position_ & 0xffffu)),
                       gain.next());
      position_ += step_;
      ++i;
    }
    const uint64_t behind = (position_ >> 16) - base_;
    if (behind > 16384) {
      buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(behind - 1));
      base_ += behind - 1;
    }
    return i;
  }

 private:
  bool restart() {
    std::unique_ptr<Mp3FileSource> source(new Mp3FileSource(path_));
    if (!source->opened()) return false;
    source_ = std::move(source);
    skip_ = kLoopSkipFrames;
    passFrames_ = 0;
    return true;
  }

  bool have(uint64_t index) {
    while (index >= base_ + buffer_.size()) {
      if (ended_) return false;
      const int16_t* samples = nullptr;
      std::size_t frames = 0;
      const PcmSource::Read read = source_->next(samples, frames);
      if (read == PcmSource::Read::Wait) return false;
      if (read == PcmSource::Read::End && !path_.empty() && passFrames_ && restart()) continue;
      if (read != PcmSource::Read::Data) {
        ended_ = true;
        failed_ = read == PcmSource::Read::Error || !path_.empty();
        source_.reset();
        return false;
      }
      const uint32_t rate = source_->rate();
      if (rate_ && rate != rate_) {
        ended_ = failed_ = true;
        source_.reset();
        return false;
      }
      rate_ = rate;
      step_ = stepFor(rate);
      passFrames_ += frames;
      const std::size_t dropped = path_.empty() ? 0 : std::min<std::size_t>(skip_, frames);
      skip_ -= static_cast<uint32_t>(dropped);
      appendMono(buffer_, samples + dropped * source_->channels(), frames - dropped,
                 source_->channels());
    }
    return true;
  }

  std::string path_;
  std::unique_ptr<PcmSource> source_;
  std::vector<int16_t> buffer_;
  uint64_t base_ = 0;
  uint64_t position_ = 0;
  uint32_t rate_ = 0;
  uint32_t step_ = 0;
  uint32_t skip_ = 0;
  uint64_t passFrames_ = 0;
  bool ended_ = false;
  bool failed_ = false;
};

// A song played once, at the mixer's rate, by the player the synth effects use.
class SongOnce final : public PcmSource {
 public:
  explicit SongOnce(std::shared_ptr<const synth::Song> song)
      : player_(MixSource::kRate, MixSource::kFxVoices, true), synth_(MixSource::kBlockFrames),
        out_(MixSource::kBlockFrames) {
    player_.play(std::move(song));
  }
  Read next(const int16_t*& samples, std::size_t& frames) override {
    if (player_.idle()) return Read::End;
    player_.render(synth_.data(), synth_.size());
    for (std::size_t i = 0; i < out_.size(); ++i)
      out_[i] = static_cast<int16_t>(std::max(-32768.0f, std::min(32767.0f, synth_[i] * 32767.0f)));
    samples = out_.data();
    frames = out_.size();
    return Read::Data;
  }
  uint32_t rate() const override { return MixSource::kRate; }
  uint8_t channels() const override { return 1; }

 private:
  synth::Player player_;
  std::vector<float> synth_;
  std::vector<int16_t> out_;
};

std::unique_ptr<PcmSource> MixSource::songOnce(std::shared_ptr<const synth::Song> song) {
  return std::unique_ptr<PcmSource>(new SongOnce(std::move(song)));
}

MixSource::MixSource(EffectCache& cache)
    : cache_(cache), limiter_(kRate, kBlockFrames), sum_(kBlockFrames), out_(kBlockFrames), synth_(kBlockFrames) {}

MixSource::~MixSource() = default;

void MixSource::setOneShot(std::unique_ptr<PcmSource> source) {
  oneShot_.reset(source ? new Track(std::move(source)) : nullptr);
  oneShotEnd_ = End::None;
  oneShotSounded_ = false;
  oneShotStarted_ = false;
}

MixSource::End MixSource::takeOneShotEnd() {
  const End end = oneShotEnd_;
  oneShotEnd_ = End::None;
  return end;
}

bool MixSource::takeOneShotStarted() {
  const bool started = oneShotStarted_;
  oneShotStarted_ = false;
  return started;
}

bool MixSource::addEffect(const std::string& hostPath) {
  std::unique_ptr<Voice> voice;
  if (std::shared_ptr<const Clip> clip = cache_.find(hostPath)) {
    voice.reset(new Voice(hostPath, std::move(clip)));
  } else {
    std::unique_ptr<Mp3FileSource> live(new Mp3FileSource(hostPath));
    if (!live->opened()) return false;
    voice.reset(new Voice(hostPath, std::move(live)));
  }
  if (voices_.size() >= kVoices) voices_.erase(voices_.begin());
  voices_.push_back(std::move(voice));
  return true;
}

void MixSource::stopEffects() {
  voices_.clear();
  fx_.clear();
}

bool MixSource::stopEffectsFrom(const std::string& hostPath) {
  const std::size_t before = voices_.size();
  voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                               [&](const std::unique_ptr<Voice>& voice) {
                                 return sound::within(voice->path(), hostPath);
                               }),
                voices_.end());
  return voices_.size() != before;
}

bool MixSource::setLoop(const std::string& hostPath) {
  if (song_) song_->stop();
  if (hostPath.empty()) {
    loop_.reset();
    // With nothing else sounding the song goes at once, as a looping MP3 does; under other layers
    // it fades, so they play on without a click.
    if (!oneShot_ && voices_.empty() && fx_.empty()) song_.reset();
    return true;
  }
  if (loop_ && loop_->path() == hostPath) return true;
  std::unique_ptr<Track> loop(new Track(hostPath));
  if (!loop->open()) return false;
  loop_ = std::move(loop);
  return true;
}

void MixSource::setSong(std::shared_ptr<const synth::Song> song, bool nextBar) {
  loop_.reset();
  if (!song_) song_.reset(new synth::Player(kRate, kSongVoices));
  song_->play(std::move(song), nextBar ? synth::Player::Start::NextBar : synth::Player::Start::Now);
}

void MixSource::addFx(std::shared_ptr<const synth::Song> song) {
  std::unique_ptr<synth::Player> fx(new synth::Player(kRate, kFxVoices, true));
  fx->play(std::move(song));
  if (fx_.size() >= kVoices) fx_.erase(fx_.begin());
  fx_.push_back(std::move(fx));
}

bool MixSource::songPosition(SongPosition& out) const {
  if (!positioned_) return false;
  out = position_;
  return true;
}

void MixSource::setGains(int32_t oneShot, int32_t effects, int32_t loop) {
  oneShotGain_ = oneShot;
  effectGain_ = effects;
  loopGain_ = loop;
}

bool MixSource::idle() const {
  return !oneShot_ && voices_.empty() && !loop_ && fx_.empty() && (!song_ || song_->idle());
}

PcmSource::Read MixSource::next(const int16_t*& samples, std::size_t& frames) {
  positioned_ = false;
  if (idle()) {
    limiter_.reset();
    return Read::End;
  }
  std::fill(sum_.begin(), sum_.end(), 0);
  std::size_t sounded = 0;
  const bool ducking = oneShot_ != nullptr;
  if (oneShot_) {
    const std::size_t n = oneShot_->mix(sum_.data(), kBlockFrames, oneShotGain_, oneShotGain_);
    if (n && !oneShotSounded_) oneShotSounded_ = oneShotStarted_ = true;
    sounded = std::max(sounded, n);
    if (oneShot_->done()) {
      oneShotEnd_ = oneShot_->failed() ? End::Failed : End::Finished;
      oneShot_.reset();
    }
  }
  const int32_t effectDuck = ducking && duckEffects_ ? kDuckGain : kUnity;
  const int32_t effectFrom = scaled(effectGain_, effectDuck_);
  const int32_t effectTo = scaled(effectGain_, effectDuck);
  for (std::size_t i = 0; i < voices_.size();) {
    sounded = std::max(sounded, voices_[i]->mix(sum_.data(), kBlockFrames, effectFrom, effectTo, cache_));
    if (voices_[i]->done(cache_)) voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i));
    else ++i;
  }
  effectDuck_ = effectDuck;
  for (std::size_t i = 0; i < fx_.size();) {
    fx_[i]->render(synth_.data(), kBlockFrames);
    mixSynth(sum_.data(), synth_.data(), kBlockFrames, effectFrom, effectTo);
    sounded = kBlockFrames;
    if (fx_[i]->idle()) fx_.erase(fx_.begin() + static_cast<std::ptrdiff_t>(i));
    else ++i;
  }
  const int32_t level = scaled(loopGain_, kLoopGain);
  const int32_t duck = ducking ? kDuckGain : kUnity;
  if (loop_) {
    sounded = std::max(sounded, loop_->mix(sum_.data(), kBlockFrames, scaled(level, duck_),
                                           scaled(level, duck)));
    if (loop_->done()) loop_.reset();
  }
  if (song_ && !song_->idle()) {
    double beat = 0.0;
    if (song_->beat(beat)) {
      const synth::Song& song = *song_->song();
      positioned_ = true;
      position_.beat = beat;
      position_.beatsPerMs = static_cast<double>(song.bpm) / 60000.0;
      position_.loopBeat = static_cast<double>(song.loopTick) / synth::kTicksPerBeat;
      position_.endBeat = static_cast<double>(song.endOf(false)) / synth::kTicksPerBeat;
      position_.loops = song.loops;
    }
    song_->render(synth_.data(), kBlockFrames);
    mixSynth(sum_.data(), synth_.data(), kBlockFrames, scaled(level, duck_), scaled(level, duck));
    sounded = kBlockFrames;
  }
  duck_ = duck;
  if (!sounded && idle()) {
    limiter_.reset();
    return Read::End;
  }
  frames = idle() ? sounded : kBlockFrames;
  limiter_.apply(sum_.data(), out_.data(), frames);
  samples = out_.data();
  return Read::Data;
}

}
}
