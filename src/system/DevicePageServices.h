#pragma once

#include <chrono>
#include <cstdint>

#include <ctime>

#include "core/Services.h"
#include "core/render/RenderPipeline.h"
#include "core/sound/SoundClips.h"
#include "hal/IBoard.h"

#if defined(AWTRIX_SOC_ESP32S3)
#include <LittleFS.h>
#endif

namespace awtrix {

class DevicePageSound : public IPageSound {
 public:
  explicit DevicePageSound(IBoard& board) : board_(board) {}
  // Explicit RTTTL in the notification is a deliberate choice and wins; after that a stored MP3
  // clip beats a melody of the same name on builds with an I2S output.
  void play(const AppSpec& spec) override {
    if (!spec.extras().rtttl.empty()) {
      board_.sound().playRtttl(spec.extras().rtttl);
      return;
    }
    if (spec.sound.empty()) return;
#if defined(AWTRIX_SOC_ESP32S3)
    if (clips_) {
      const std::string path = sound::clipPathFor(spec.sound);
      if (!path.empty() && LittleFS.exists(path.c_str())) {
        clips_->playClip(path);
        return;
      }
    }
#endif
    board_.sound().playFile(spec.sound);
  }
  bool isPlaying() const override {
#if defined(AWTRIX_SOC_ESP32S3)
    if (clips_ && clips_->clipPlaying()) return true;
#endif
    return board_.sound().isPlaying();
  }

#if defined(AWTRIX_SOC_ESP32S3)
  void setClips(IClipService* clips) { clips_ = clips; }
#endif

 private:
  IBoard& board_;
#if defined(AWTRIX_SOC_ESP32S3)
  IClipService* clips_ = nullptr;
#endif
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
