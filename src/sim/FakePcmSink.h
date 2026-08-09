#pragma once

#include <cstdint>
#include <string>

#include "core/CoreEngine.h"
#include "core/radio/IcyMetadata.h"
#include "core/radio/RadioDisplay.h"
#include "core/sound/AudioSinks.h"
#include "core/sound/SoundMp3.h"

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
  }

 private:
  static constexpr long kTitleIntervalMs = 12000;
  static constexpr long kMp3DurationMs = 1500;

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
};

}
}
