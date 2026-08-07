#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

#include <ctime>

#include "core/Services.h"
#include "core/render/RenderPipeline.h"
#include "core/sound/SoundClips.h"
#include "hal/IBoard.h"
#include "sim/SimStore.h"

namespace awtrix {

class SimPageSound : public IPageSound {
 public:
  explicit SimPageSound(IBoard& board) : board_(board) {}
  // Mirrors DevicePageSound: explicit RTTTL first, then a stored MP3 clip, then the melody file.
  void play(const AppSpec& spec) override {
    if (!spec.extras().rtttl.empty()) {
      board_.sound().playRtttl(spec.extras().rtttl);
      return;
    }
    if (spec.sound.empty()) return;
    if (clips_) {
      const std::string path = sound::clipPathFor(spec.sound);
      if (!path.empty() && std::filesystem::exists(std::filesystem::u8path(sim::hostPath(path)))) {
        clips_->playClip(path);
        return;
      }
    }
    board_.sound().playFile(spec.sound);
  }
  bool isPlaying() const override {
    if (clips_ && clips_->clipPlaying()) return true;
    return board_.sound().isPlaying();
  }

  void setClips(IClipService* clips) { clips_ = clips; }

 private:
  IBoard& board_;
  IClipService* clips_ = nullptr;
};

// Same as DevicePageClock except that it reads the host's local time: there is no NTP sync and the
// configured timezone is ignored, so the simulator always shows whatever the machine thinks it is.
class SimPageClock : public IPageClock {
 public:
  void fill(RenderCtx& ctx, int64_t nowMs) override {
    ctx.nowMs = nowMs;
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int64_t epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch())
                                .count();
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    ctx.hour = tmv.tm_hour;
    ctx.minute = tmv.tm_min;
    ctx.second = tmv.tm_sec;
    ctx.weekday = tmv.tm_wday;
    ctx.mday = tmv.tm_mday;
    ctx.month = tmv.tm_mon + 1;
    ctx.year = tmv.tm_year + 1900;
    // -1 is the "clock not set yet" sentinel apps check for. On the device that is a real state
    // before NTP lands; here it only trips if the host clock is badly wrong.
    ctx.epochMs = ctx.year >= 2020 ? epochMs : -1;
  }
};

}
