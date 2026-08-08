#pragma once

#include <chrono>
#include <cstdint>

#include <ctime>

#include "core/Services.h"
#include "core/render/RenderPipeline.h"
#include "hal/IBoard.h"
#include "system/ClipPlayback.h"

namespace awtrix {

class DevicePageSound : public IPageSound {
 public:
  explicit DevicePageSound(IBoard& board) : board_(board) {}
  // RTTTL spelled out in the notification is a deliberate choice and wins over everything stored.
  void play(const AppSpec& spec) override {
    if (!spec.extras().rtttl.empty()) {
      board_.sound().playRtttl(spec.extras().rtttl);
      return;
    }
    if (spec.sound.empty()) return;
    if (playStoredClip(clips_, spec.sound)) return;
    board_.sound().playFile(spec.sound);
  }
  bool isPlaying() const override {
    return (clips_ && clips_->clipPlaying()) || board_.sound().isPlaying();
  }

  void setClips(IClipService* clips) { clips_ = clips; }

 private:
  IBoard& board_;
  IClipService* clips_ = nullptr;
};

class DevicePageClock : public IPageClock {
 public:
  void fill(RenderCtx& ctx, int64_t nowMs) override {
    ctx.nowMs = nowMs;
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int64_t epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch())
                                .count();
    std::tm tmv{};
    localtime_r(&t, &tmv);
    ctx.hour = tmv.tm_hour;
    ctx.minute = tmv.tm_min;
    ctx.second = tmv.tm_sec;
    ctx.weekday = tmv.tm_wday;
    ctx.mday = tmv.tm_mday;
    ctx.month = tmv.tm_mon + 1;
    ctx.year = tmv.tm_year + 1900;
    // -1 marks "clock not set yet", so apps can tell an unsynced device from 1970 and skip drawing
    // a time that would only be wrong.
    ctx.epochMs = ctx.year >= 2020 ? epochMs : -1;
  }
};

}
