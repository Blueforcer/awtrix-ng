#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/audio/Limiter.h"
#include "core/synth/Player.h"
#include "platform/tc002/audio/Tc002AudioSources.h"

namespace awtrix {
namespace tc002 {

// A decoded effect, mono at the rate it was stored in.
struct Clip {
  std::vector<int16_t> samples;
  uint32_t rate = 0;
};

// Decoded effects by host path. A file replaced on disk decodes again; past the budget the
// effect used longest ago goes first. Audio thread only.
class EffectCache {
 public:
  static constexpr std::size_t kBudgetBytes = 2u * 1024u * 1024u;

  std::shared_ptr<const Clip> find(const std::string& path);
  void put(const std::string& path, std::shared_ptr<const Clip> clip);
  std::size_t bytes() const { return bytes_; }

 private:
  struct Entry {
    std::shared_ptr<const Clip> clip;
    int64_t size = 0;
    int64_t mtime = 0;
    uint64_t used = 0;
  };
  static bool stamp(const std::string& path, int64_t& size, int64_t& mtime);
  void trim();

  std::map<std::string, Entry> entries_;
  std::size_t bytes_ = 0;
  uint64_t clock_ = 0;
};

// Sums one-shot, effects and loop to 44.1 kHz mono in 10 ms blocks through the Limiter,
// with at most kLeadMs queued. Layers resample from their own rates; one-shots duck
// the loop and, when setDuckEffects is on, the effects. The last block ends with the sound.
class MixSource final : public PcmSource {
 public:
  static constexpr uint32_t kRate = 44100;
  static constexpr std::size_t kBlockFrames = 441;
  static constexpr std::size_t kVoices = 4;
  static constexpr uint32_t kLeadMs = 130;
  static constexpr uint32_t kMaxEffectMs = 10000;
  static constexpr int32_t kUnity = 32768;
  static constexpr int32_t kLoopGain = 23170;
  static constexpr int32_t kDuckGain = 8192;
  // LAME's encoder delay plus the decoder's: silence at the start of every loop pass.
  static constexpr uint32_t kLoopSkipFrames = 1105;
  static constexpr std::size_t kSongVoices = 24;
  static constexpr std::size_t kFxVoices = 8;

  // Where the song is at the start of the block next() handed out last, and how it moves on.
  struct SongPosition {
    double beat = 0.0;
    double beatsPerMs = 0.0;
    double loopBeat = 0.0;
    double endBeat = 0.0;
    bool loops = false;
  };

  enum class End : uint8_t { None, Finished, Failed };

  explicit MixSource(EffectCache& cache);
  ~MixSource() override;

  // A song as a one-shot: it plays once, rendered like a synth effect.
  static std::unique_ptr<PcmSource> songOnce(std::shared_ptr<const synth::Song> song);

  // Replaces the one-shot without reporting the old one; nullptr stops it. The new one's end is
  // reported once by takeOneShotEnd(), and takeOneShotStarted() is true once after the block in
  // which it first sounded.
  void setOneShot(std::unique_ptr<PcmSource> source);
  bool oneShotActive() const { return oneShot_ != nullptr; }
  End takeOneShotEnd();
  bool takeOneShotStarted();

  // False when the file cannot be opened. A fifth effect cuts off the oldest.
  bool addEffect(const std::string& hostPath);
  void stopEffects();
  // Stops the effects that play from that file, or from any file inside that folder; true when
  // there was one.
  bool stopEffectsFrom(const std::string& hostPath);
  std::size_t voices() const { return voices_.size() + fx_.size(); }

  // An empty path stops the loop, song included; the path already looping keeps playing. A new
  // MP3 loop fades a song out. False when the file cannot be opened.
  bool setLoop(const std::string& hostPath);
  // A song in the loop's place; an MP3 loop stops, the song playing now asked again keeps
  // playing, and nextBar lets it reach its next bar line before the new one takes over.
  void setSong(std::shared_ptr<const synth::Song> song, bool nextBar);
  // A short song next to the effects; a fifth cuts off the oldest.
  void addFx(std::shared_ptr<const synth::Song> song);
  bool looping() const { return loop_ != nullptr || (song_ && !song_->idle()); }
  bool songPosition(SongPosition& out) const;

  // Per-layer scale in Q15 (kUnity is full), for a layer set quieter than the speaker's volume.
  void setGains(int32_t oneShot, int32_t effects, int32_t loop);
  // Whether the effects duck under the one-shot as the loop does.
  void setDuckEffects(bool on) { duckEffects_ = on; }

  bool idle() const;

  Read next(const int16_t*& samples, std::size_t& frames) override;
  uint32_t rate() const override { return kRate; }
  uint8_t channels() const override { return 1; }
  uint32_t leadMs() const override { return kLeadMs; }

 private:
  class Voice;
  class Track;

  EffectCache& cache_;
  std::unique_ptr<Track> oneShot_;
  End oneShotEnd_ = End::None;
  bool oneShotSounded_ = false;
  bool oneShotStarted_ = false;
  std::vector<std::unique_ptr<Voice>> voices_;
  std::unique_ptr<Track> loop_;
  std::unique_ptr<synth::Player> song_;
  std::vector<std::unique_ptr<synth::Player>> fx_;
  SongPosition position_;
  bool positioned_ = false;
  int32_t oneShotGain_ = kUnity;
  int32_t effectGain_ = kUnity;
  int32_t loopGain_ = kUnity;
  int32_t duck_ = kUnity;
  bool duckEffects_ = false;
  int32_t effectDuck_ = kUnity;
  audio::Limiter limiter_;
  std::vector<int32_t> sum_;
  std::vector<int16_t> out_;
  std::vector<float> synth_;
};

}
}
