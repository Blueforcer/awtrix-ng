#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/CoreEngine.h"
#include "core/audio/AudioStatsRing.h"
#include "core/audio/SpectrumAnalyzer.h"
#include "core/radio/IcyMetadata.h"
#include "core/radio/RadioDisplay.h"
#include "core/sound/AudioSinks.h"
#include "core/sound/SoundMp3.h"
#include "sim/SimSong.h"

namespace awtrix {
namespace sim {

// No decoder and no DAC on the host: a station feeds canned ICY metadata through the real
// TitleTracker, an MP3 flips the runtime state for a fixed duration.
class FakePcmSink : public sound::IPcmSink {
 public:
  explicit FakePcmSink(CoreEngine& engine) : engine_(engine) {}

  void setSoundVolume(uint8_t) override {}
  void setStreamVolume(uint8_t) override {}

  bool playMp3(const std::string& path) override {
    engine_.state().runtime().mp3Playing = true;
    engine_.state().runtime().mp3Name = sound::mp3NameFor(path);
    engine_.state().emit(StateEvent::RadioChanged);
    mp3EndsAtMs_ = 0;
    mp3Running_ = true;
    return true;
  }
  void stopMp3() override { finishMp3(); }
  bool mp3Playing() const override { return mp3Running_; }

  DispatchResult playStream(const std::string&, const std::string& label,
                            DispatchDetail&) override {
    label_ = label;
    streaming_ = true;
    titleIndex_ = 0;
    nextChangeMs_ = 0;
    announce(label_, radio::Announcement::Station);
    return DispatchResult::Ok;
  }
  void stopStream() override { streaming_ = false; }

  void tick(int64_t nowMs) override {
    nowMs_ = nowMs;
    tickMp3(nowMs);
    tickStream(nowMs);
    tickSong(nowMs);
  }

  bool analysis(int64_t nowMs, audio::FrameStats& out) override {
    stats_.markInterest(nowMs);
    return stats_.latestAudibleAt(nowMs, out);
  }

 private:
  static constexpr long kTitleIntervalMs = 12000;
  static constexpr long kMp3DurationMs = 1500;
  static constexpr int kSongFrames = 1152;
  static constexpr int kSongLeadMs = 80;

  // The generated song runs up to kSongLeadMs ahead of the clock, the way the device's DMA queue
  // does, so the "which frame is audible now" logic is exercised on the host as well.
  void tickSong(int64_t nowMs) {
    if (!(streaming_ || mp3Running_) || !stats_.wanted(nowMs)) {
      songStartMs_ = 0;
      return;
    }
    if (songStartMs_ == 0) {
      songStartMs_ = nowMs;
      songSamples_ = 0;
      song_.reset();
      analyzer_.reset();
    }
    for (int n = 0; n < 8; ++n) {
      const int64_t at = songStartMs_ + songSamples_ * 1000 / SimSong::kRateHz;
      if (at >= nowMs + kSongLeadMs) return;
      song_.fill(pcm_.data(), kSongFrames);
      audio::FrameStats st;
      if (analyzer_.analyze(pcm_.data(), kSongFrames, 2, SimSong::kRateHz, st))
        stats_.publish(st, at);
      songSamples_ += kSongFrames;
    }
    // Too far behind to catch up (a paused debugger): start over from now.
    if (songStartMs_ + songSamples_ * 1000 / SimSong::kRateHz < nowMs) songStartMs_ = 0;
  }

  void tickMp3(int64_t nowMs) {
    if (!mp3Running_) return;
    if (mp3EndsAtMs_ == 0) {
      mp3EndsAtMs_ = nowMs + kMp3DurationMs;
      return;
    }
    if (nowMs >= mp3EndsAtMs_) finishMp3();
  }

  void finishMp3() {
    if (!mp3Running_) return;
    mp3Running_ = false;
    mp3EndsAtMs_ = 0;
    engine_.state().runtime().mp3Playing = false;
    engine_.state().runtime().mp3Name.clear();
    engine_.state().emit(StateEvent::RadioChanged);
  }

  void tickStream(int64_t nowMs) {
    if (!streaming_) return;
    if (nextChangeMs_ == 0) {
      nextChangeMs_ = nowMs + kTitleIntervalMs;
      return;
    }
    if (nowMs < nextChangeMs_) return;
    nextChangeMs_ = nowMs + kTitleIntervalMs;

    // Deliberately awkward: an apostrophe inside the title, Latin-1 bytes, and an empty title, so
    // the parser and the scroller get exercised rather than a happy path.
    static const char* const kBlocks[] = {
        "StreamTitle='Kraftwerk - Das Model';StreamUrl='';",
        "StreamTitle='Rock'n'Roll Hits';StreamUrl='';",
        "StreamTitle='Bj\xF6rk - J\xF3ga';StreamUrl='';",
        "StreamTitle='';StreamUrl='';",
    };
    constexpr int kCount = sizeof(kBlocks) / sizeof(kBlocks[0]);
    const std::string block = kBlocks[titleIndex_ % kCount];
    ++titleIndex_;
    if (!tracker_.update(block)) return;
    engine_.state().runtime().radioTitle = tracker_.title();
    engine_.state().emit(StateEvent::RadioChanged);
    announce(tracker_.title(), radio::Announcement::Title);
  }

  void announce(const std::string& text, radio::Announcement kind) {
    if (!engine_.state().settings().radioMeta) return;
    AppSpec spec;
    if (!radio::buildAnnouncement(text, kind, spec)) return;
    engine_.notifications().push(spec, nowMs_);
  }

  CoreEngine& engine_;
  radio::TitleTracker tracker_;
  std::string label_;
  bool streaming_ = false;
  bool mp3Running_ = false;
  int titleIndex_ = 0;
  int64_t nextChangeMs_ = 0;
  int64_t mp3EndsAtMs_ = 0;
  int64_t nowMs_ = 0;
  audio::SpectrumAnalyzer analyzer_;
  audio::StatsRing stats_;
  SimSong song_;
  std::vector<int16_t> pcm_ = std::vector<int16_t>(kSongFrames * 2);
  int64_t songStartMs_ = 0;
  int64_t songSamples_ = 0;
};

}
}
