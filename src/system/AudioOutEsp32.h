#pragma once

#if defined(AWTRIX_SOC_ESP32S3)

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/audio/Mp3Decoder.h"
#include "core/sound/AudioSinks.h"
#include "core/radio/IcyMetadata.h"
#include "core/radio/IcyStream.h"

namespace awtrix {

// Owns the I2S output. One audio task plays both internet radio streams and
// stored MP3s; an MP3 interrupts the stream and the stream reconnects on
// its own once it is over.
class AudioOutEsp32 : public sound::IPcmSink {
 public:
  static void* operator new(std::size_t bytes);
  static void operator delete(void* p);

  AudioOutEsp32(CoreEngine& engine, int pinBclk, int pinLrclk, int pinDout);
  ~AudioOutEsp32() override;

  // Two gains, one DAC. A stream turned down for the background must not take the doorbell
  // with it, and both are read once per decoded frame rather than per sample.
  void setSoundVolume(uint8_t percent) override;
  void setStreamVolume(uint8_t percent) override;

  bool playMp3(const std::string& path) override;
  void stopMp3() override { mp3Stop_.store(true); }
  bool mp3Playing() const override { return mp3Playing_.load(); }

  DispatchResult playStream(const std::string& url, const std::string& label,
                            DispatchDetail& detail) override;
  void stopStream() override;

  void tick(int64_t nowMs) override;

  uint32_t underruns() const override { return underruns_.load(); }
  uint32_t decodeUs() const override { return decodeUs_.load(); }
  uint32_t starvedMs() const override { return starvedMs_.load(); }
  uint32_t bufferBytes() const override { return bufferBytes_.load(); }

  static bool usable(int pinBclk, int pinLrclk, int pinDout);

 private:
  static void taskEntry(void* self);
  void run();
  bool ensureTask();
  void playMp3File(const std::string& path, const std::string& name, int16_t* pcm);
  bool writeDecodedFrame(const mp3::DecodeResult& result, int16_t* pcm);
  void closeStream();
  void publishTitle(const std::string& title);
  void publishError(const std::string& message);
  void publishMp3Started(const std::string& name);
  void publishMp3Ended();

  CoreEngine& engine_;
  const int pinBclk_;
  const int pinLrclk_;
  const int pinDout_;

  TaskHandle_t task_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;

  std::atomic<bool> playing_{false};
  std::atomic<bool> stopRequested_{false};
  std::atomic<bool> mp3Stop_{false};
  std::atomic<bool> mp3Playing_{false};
  std::atomic<int> soundVolume_{70};
  std::atomic<int> streamVolume_{60};
  std::atomic<uint32_t> handoffSeq_{0};
  std::atomic<uint32_t> urlSeq_{0};
  std::atomic<uint32_t> mp3Seq_{0};
  std::atomic<uint32_t> underruns_{0};
  std::atomic<uint32_t> decodeUs_{0};
  std::atomic<uint32_t> starvedMs_{0};
  std::atomic<uint32_t> bufferBytes_{0};
  uint32_t seenSeq_ = 0;
  uint32_t mp3SeenSeq_ = 0;

  // Shared with the audio task and only valid under lock_; the atomics above signal when there is
  // something new to pick up.
  std::string pendingUrl_;
  std::string pendingLabel_;
  std::string pendingTitle_;
  std::string pendingError_;
  std::string pendingMp3_;
  std::string pendingMp3Name_;
  std::string pendingMp3Started_;
  bool pendingMp3Ended_ = false;

  radio::TitleTracker tracker_;
  radio::MetadataSplitter splitter_;
  mp3::Decoder decoder_;
  int sampleRateHz_ = 0;
  int channels_ = 0;
  bool i2sStarted_ = false;
};

}

#endif
