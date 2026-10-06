#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/CoreEngine.h"
#include "core/audio/AudioStatsRing.h"
#include "core/audio/SpectrumAnalyzer.h"
#include "core/sound/AudioSinks.h"
#include "core/sound/PcmRouting.h"
#include "core/sound/Rtttl.h"
#include "core/synth/SongCache.h"
#include "platform/tc002/audio/PlaybackPitch.h"
#include "platform/tc002/audio/Tc002AudioLink.h"
#include "platform/tc002/audio/Tc002AudioMixer.h"
#include "platform/tc002/audio/Tc002AudioPlayer.h"
#include "platform/tc002/audio/UrlSounds.h"
#include "platform/tc002/speech/SpeechVoice.h"

namespace awtrix {
namespace tc002 {

class StreamSource;

// The TC002 speaker behind both sink interfaces: the render thread records requests;
// an audio thread owns the helper socket, decoders, station and mixer.
class Tc002AudioSink final : public sound::IToneSink, public sound::IPcmSink {
 public:
  // helperFd is this process's end of the awtrix-tc002-audio-pcm socket pair; the sink owns it.
  // Without a voice the sink does not speak.
  Tc002AudioSink(CoreEngine& engine, int helperFd, std::shared_ptr<speech::SpeechVoice> voice = {});
  ~Tc002AudioSink() override;
  Tc002AudioSink(const Tc002AudioSink&) = delete;
  Tc002AudioSink& operator=(const Tc002AudioSink&) = delete;

  enum class SystemStart : uint8_t { None, Pending, Audible, Failed };

  // False once the helper failed or exited; nothing plays after that.
  bool available() const { return available_.load(); }

  // A file outside the data directory (the boot sound, the voice assistant's answer), played once
  // as an alert one-shot. The first skipFrames decoded frames are dropped. Any other one-shot
  // replaces it, stopOneShot() stops it.
  bool playSystemSound(const std::string& path, uint32_t skipFrames);
  void stopSystemSound();
  // Pending until its first frame is queued; then Audible with the monotonic time that frame
  // reaches the speaker, or Failed once it can no longer start.
  SystemStart systemStart(int64_t& audibleAtMs) const;
  bool systemFinished() const { return systemFinished_.load(); }
  // Voice owns the speaker from capture start through audible TTS completion. A station and the
  // loop wait meanwhile and play again once the voice lets go, the loop from its start.
  void setVoiceOwned(bool owned);
  bool voiceQuiet() const { return voiceQuiet_.load(); }

  void begin() override {}
  void setVolumes(const sound::Volumes& volumes) override;
  bool playRtttl(const std::string& rtttl, sound::Group group) override;
  bool playMelodyFile(const std::string& name, sound::Group group) override;
  void stop() override;
  void tick() override {}
  bool isPlaying() const override;
  bool sharesPcmOutput() const override { return true; }

  sound::PcmPlay playSpec(const sound::PcmRequest& request, DispatchDetail& detail) override;
  sound::Caps caps() const override { return sound::PcmRouting::caps(*this); }
  bool checkSpec(const sound::Spec& spec, DispatchDetail& detail) override;
  void stopLayers(sound::Stop what, const std::string& owner) override;
  sound::PcmState state() override;
  bool takeError(sound::PcmError& error) override;

  bool playMp3(const std::string& path, sound::Group group);
  void stopOneShot() override;
  bool oneShotPlaying() const override;
  bool oneShotGroup(sound::Group& group) const;
  bool mixes() const { return available(); }
  bool playEffect(const std::string& path);
  bool playLoop(const std::string& path);
  void stopEffects();
  void stopLoop();
  bool effectsPlaying() const;
  bool loopPlaying() const;
  bool synthesizes() const { return available(); }
  bool checkSong(const std::string& text, std::string& error);
  bool playSong(const std::string& text, bool nextBar);
  bool playFx(const std::string& text);
  bool playSongOnce(const std::string& text, sound::Group group);
  bool speaks() const { return available() && voice_ != nullptr; }
  bool checkSpeech(const std::string& text, DispatchDetail& detail);
  DispatchResult playSpeech(const std::string& text, sound::Group group, DispatchDetail& detail);
  bool songBeat(int64_t nowMs, double& beat) const;
  bool clips() const { return available(); }
  bool fetches() const { return available(); }
  bool playUrl(const std::string& url, sound::Group group, bool music);
  bool takeUrlError(std::string& error, bool& music);
  DispatchResult playClip(std::string&& bytes, std::string& error);
  void release(const std::string& path) override;
  DispatchResult playStream(const std::string& url, const std::string& label,
                            DispatchDetail& detail) override;
  void stopStream() override;
  void holdStream(bool held) override;
  void tick(int64_t nowMs) override;
  bool analysis(int64_t nowMs, audio::FrameStats& out) override;
  // music.pitch() from what the speaker plays; pitch() is for the render thread.
  PlaybackPitch& playbackPitch() { return pitch_; }
  uint32_t underruns() const override { return underruns_.load(); }
  uint32_t decodeUs() const override { return decodeUs_.load(); }
  uint32_t starvedMs() const override { return starvedMs_.load(); }
  uint32_t bufferBytes() const override { return bufferBytes_.load(); }

 private:
  // What the one-shot layer plays.
  enum class Kind : uint8_t { None, Tone, Mp3, System, Clip, Speech, Song };
  enum class Playing : uint8_t { None, Mix, Stream };
  static constexpr std::size_t kMaxQueuedEffects = 8;
  // A song position older than this has no mixer behind it any more.
  static constexpr int64_t kSongMarkStaleMs = 100;

  // What the loop's layer plays: a looping MP3 or a song, never both.
  struct Background {
    std::string mp3;
    std::shared_ptr<const synth::Song> song;
    bool nextBar = false;
    bool empty() const { return mp3.empty() && !song; }
    void clear() {
      mp3.clear();
      song.reset();
      nextBar = false;
    }
  };

  // One effect: a stored MP3 or a short song.
  struct Effect {
    std::string mp3;
    std::shared_ptr<const synth::Song> song;
  };

  // The song's position in the block that becomes audible at audibleAtMs.
  struct SongMark {
    bool playing = false;
    MixSource::SongPosition position;
  };

  struct Request {
    uint32_t oneShotSeq = 0;
    Kind oneShot = Kind::None;
    sound::Group oneShotGroup = sound::Group::Alert;
    std::vector<rtttl::Note> notes;
    uint16_t timeUnit = 0;
    // The device path of a stored MP3, or the URL of a fetched one, whose file is mp3File.
    std::string mp3Path;
    std::string mp3File;
    std::string systemPath;
    uint32_t systemSkipFrames = 0;
    // Handed on to the one-shot's source, so a clip lives in one copy.
    std::shared_ptr<const std::string> clip;
    std::shared_ptr<const speech::Plan> speech;
    std::shared_ptr<const synth::Song> oneShotSong;
    std::vector<Effect> effects;
    // Host folders and files whose effects stop, in the order release() was asked.
    std::vector<std::string> released;
    uint32_t effectsStopSeq = 0;
    uint32_t loopSeq = 0;
    Background loop;
    uint32_t streamSeq = 0;
    bool streamWanted = false;
    bool streamHeld = false;
    std::shared_ptr<const std::string> url;
    sound::Volumes volumes;
  };

  struct Report {
    uint32_t seq = 0;
    uint32_t streamSeq = 0;
    std::string title;
    std::string error;
    bool titleNew = false;
    bool errorNew = false;
    bool streaming = false;
  };

  void run();
  void wake();
  UrlSounds::Speaker urlSpeaker();
  bool playOneShotMp3(const std::string& file, const std::string& path, sound::Group group);
  bool loopFile(const std::string& file);
  void endOneShotRequest();
  void endLoopRequest();
  void reconcile(int64_t nowMs);
  std::unique_ptr<PcmSource> openOneShot(const Request& request);
  void startMixer(uint8_t volume, int64_t nowMs);
  void startStream(const Request& request, int64_t nowMs);
  void applyVolumes(const Request& request);
  void afterPump();
  void oneShotEnded();
  void finishOneShot(uint32_t seq);
  void publishError(const std::string& message);
  void publishTitle(const std::string& title);
  void markSong(int64_t audibleAtMs);
  void systemGone(uint32_t seq);
  Tc002AudioPlayer::Tap analysisTap();

  const std::shared_ptr<speech::SpeechVoice> voice_;
  CoreEngine& engine_;
  Tc002AudioLink link_;
  Tc002AudioPlayer player_;
  mutable std::mutex mutex_;
  Request request_;
  // Why the last sound from an address did not play, per layer (one-shot, loop), until
  // takeUrlError() hands it over.
  std::string urlError_[2];
  bool urlFailed_[2] = {false, false};
  // The group of the sound from an address that is being fetched.
  sound::Group urlGroup_ = sound::Group::Alert;
  // Effects asked for that the mixer has not counted yet, so effectsPlaying() never blinks off
  // between a request and its voice.
  uint32_t effectsPending_ = 0;
  Report report_;
  uint32_t seenReport_ = 0;
  std::atomic<bool> available_{true};
  std::atomic<bool> quit_{false};
  std::atomic<uint32_t> underruns_{0};
  std::atomic<uint32_t> decodeUs_{0};
  std::atomic<uint32_t> starvedMs_{0};
  std::atomic<uint32_t> bufferBytes_{0};
  std::atomic<uint32_t> effectVoices_{0};
  std::atomic<uint32_t> systemSeq_{0};
  std::atomic<SystemStart> systemStart_{SystemStart::None};
  std::atomic<bool> systemFinished_{false};
  std::atomic<bool> voiceOwned_{false};
  std::atomic<bool> voiceQuiet_{false};
  std::atomic<int64_t> systemAudibleAtMs_{-1};
  int wake_[2] = {-1, -1};
  std::thread thread_;

  // Audio thread only.
  Playing playing_ = Playing::None;
  Kind oneShot_ = Kind::None;
  sound::Group oneShotGroup_ = sound::Group::Alert;
  // The mixer sent the one-shot's last sample; it ends once that is heard.
  bool oneShotEnding_ = false;
  int64_t oneShotEndsAtMs_ = 0;
  uint32_t playingOneShotSeq_ = 0;
  // The request the one-shot in the mixer came from; a newer one may already be reconciled.
  uint32_t mixedOneShotSeq_ = 0;
  uint32_t playingEffectsStopSeq_ = 0;
  uint32_t playingLoopSeq_ = 0;
  // The voice held the loop back at the last reconcile, so no mixer carries it.
  bool loopHeld_ = false;
  uint32_t playingStreamSeq_ = 0;
  StreamSource* stream_ = nullptr;
  MixSource* mixer_ = nullptr;
  EffectCache effectCache_;
  bool again_ = false;
  audio::SpectrumAnalyzer analyzer_;
  audio::StatsRing stats_;
  PlaybackPitch pitch_;

  sound::PcmRouting routing_;

  // Render thread only: song text parsed once, so the same text is always the same Song.
  synth::SongCache songs_;
  static constexpr uint32_t kSongMarks = 32;
  audio::StampedRing<SongMark, kSongMarks> songMarks_;

  // Render thread only. Last, so it goes before anything its speaker calls into.
  UrlSounds urls_;
};

}
}
