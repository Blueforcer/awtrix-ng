#pragma once

#include <cstdint>

#include <string>

#include "core/CoreEngine.h"
#include "core/Services.h"
#include "core/radio/IcyMetadata.h"
#include "core/radio/RadioDisplay.h"

namespace awtrix {
namespace sim {

// There is no audio decoder on the host, so this only pretends to stream: it accepts a station and
// feeds canned ICY metadata through the real TitleTracker every 12 seconds.
class FakeRadioService : public IRadioService {
 public:
  explicit FakeRadioService(CoreEngine& engine) : engine_(engine) {}

  DispatchResult play(const std::string& url, const std::string& label,
                      DispatchDetail&) override {
    url_ = url;
    label_ = label;
    playing_ = true;
    titleIndex_ = 0;
    nextChangeMs_ = 0;
    announce(label_, radio::Announcement::Station);
    return DispatchResult::Ok;
  }

  void stop() override {
    playing_ = false;
    url_.clear();
  }

  void setVolume(int percent) override { volume_ = percent; }
  bool isPlaying() const override { return playing_; }

  void tick(int64_t nowMs) {
    nowMs_ = nowMs;
    if (!playing_) return;
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

  const std::string& url() const { return url_; }
  int volume() const { return volume_; }
  const std::string& label() const { return label_; }

 private:
  static constexpr long kTitleIntervalMs = 12000;

  void announce(const std::string& text, radio::Announcement kind) {
    if (!engine_.state().settings().radioMeta) return;
    AppSpec spec;
    if (!radio::buildAnnouncement(text, kind, spec)) return;
    engine_.notifications().push(spec, nowMs_);
  }

 private:
  CoreEngine& engine_;
  radio::TitleTracker tracker_;
  std::string url_;
  std::string label_;
  bool playing_ = false;
  int volume_ = 60;
  int titleIndex_ = 0;
  int64_t nextChangeMs_ = 0;
  int64_t nowMs_ = 0;
};

}
}
