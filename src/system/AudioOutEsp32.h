#pragma once

#if defined(AWTRIX_SOC_ESP32S3)

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/audio/AudioStatsRing.h"
#include "core/audio/Mp3Decoder.h"
#include "core/audio/SpectrumAnalyzer.h"
#include "core/sound/RoutedPcmSink.h"
#include "core/sound/VolumeCurve.h"
#include "core/synth/Player.h"
#include "core/synth/SongCache.h"
#include "core/radio/IcyMetadata.h"
#include "core/radio/IcyStream.h"

namespace awtrix {

// Owns the I2S output. One audio task plays internet radio streams, stored
// MP3s and songs; an MP3 or a song interrupts the stream and the stream
// reconnects on its own once it is over.
class AudioOutEsp32 : public sound::RoutedPcmSink {
 public:
  static void* operator new(std::size_t bytes);
  static void operator delete(void* p);

  // pinMclk and pinAmpEnable are -1 on boards that need neither.
  AudioOutEsp32(CoreEngine& engine, int pinBclk, int pinLrclk, int pinDout, int pinMclk,
                int pinAmpEnable);
  ~AudioOutEsp32() override;

  // One gain per group, read once per decoded frame.
  void setVolumes(const sound::Volumes& volumes) override;

  bool playMp3(const std::string& path, sound::Group group) override;
  void stopOneShot() override;
  // A requested file counts as playing before the audio task has taken it.
  bool oneShotPlaying() const override { return mp3Playing_.load() || mp3Pending_.load(); }
  bool synthesizes() const override { return true; }
  bool checkSong(const std::string& text, std::string& error) override;
  bool playSongOnce(const std::string& text, sound::Group group) override;
  void release(const std::string& path) override;

  DispatchResult playStream(const std::string& url, const std::string& label,
                            DispatchDetail& detail) override;
  void stopStream() override;
  void holdStream(bool held) override { streamHeld_.store(held); }

  void tick(int64_t nowMs) override;
  bool analysis(int64_t nowMs, audio::FrameStats& out) override;

  uint32_t underruns() const override { return underruns_.load(); }
  uint32_t decodeUs() const override { return decodeUs_.load(); }
  uint32_t starvedMs() const override { return starvedMs_.load(); }
  uint32_t bufferBytes() const override { return bufferBytes_.load(); }

  static bool usable(int pinBclk, int pinLrclk, int pinDout);

 private:
  static void taskEntry(void* self);
  void run();
  bool ensureTask();
  void playMp3File(const std::string& path, int16_t* pcm);
  void playSongPcm(std::shared_ptr<const synth::Song> song, int16_t* pcm);
  bool writeDecodedFrame(const mp3::DecodeResult& result, int16_t* pcm);
  void closeStream();
  void publishTitle(const std::string& title);
  void publishError(const std::string& message);

  CoreEngine& engine_;
  const int pinBclk_;
  const int pinLrclk_;
  const int pinDout_;
  const int pinMclk_;
  const int pinAmpEnable_;

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;

  std::atomic<bool> playing_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> streamHeld_{false};
  std::atomic<bool> mp3Stop_{false};
  std::atomic<bool> mp3Playing_{false};
  std::atomic<bool> mp3Pending_{false};
  std::atomic<uint8_t> alertVolume_{60};
  std::atomic<uint8_t> appVolume_{60};
  std::atomic<uint8_t> radioVolume_{48};
  std::atomic<uint32_t> handoffSeq_{0};
  std::atomic<uint32_t> urlSeq_{0};
  std::atomic<uint32_t> mp3Seq_{0};
  std::atomic<uint32_t> underruns_{0};
  std::atomic<uint32_t> decodeUs_{0};
  std::atomic<uint32_t> starvedMs_{0};
  std::atomic<uint32_t> bufferBytes_{0};
  uint32_t seenSeq_ = 0;
  uint32_t mp3SeenSeq_ = 0;
  // The audio task's own copy of the group of the file it plays.
  sound::Group mp3Group_ = sound::Group::Alert;
  // The audio task's Q15 scale for the volume it last played at.
  uint8_t scaledVolume_ = 100;
  int32_t scale_ = sound::kVolumeUnity;

  // Shared with the audio task and only valid under lock_; the atomics above signal when there is
  // something new to pick up.
  std::string pendingUrl_;
  std::string pendingLabel_;
  std::string pendingTitle_;
  std::string pendingError_;
  std::string pendingMp3_;
  sound::Group pendingGroup_ = sound::Group::Alert;
  std::shared_ptr<const synth::Song> pendingSong_;
  // The file the audio task has taken on, from taking the request until it has closed the file.
  std::string openMp3_;

  static constexpr std::size_t kSongCacheBytes = 96 * 1024;
  // Asked only from the loop, which parses each text once.
  synth::SongCache songs_{kSongCacheBytes};

  radio::TitleTracker tracker_;
  radio::MetadataSplitter splitter_;
  mp3::Decoder decoder_;
  audio::SpectrumAnalyzer analyzer_;
  audio::StatsRing stats_;
  int sampleRateHz_ = 0;
  int channels_ = 0;
  bool i2sStarted_ = false;
};

}

#endif
