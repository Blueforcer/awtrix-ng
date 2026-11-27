#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>

#include "core/Services.h"
#include "core/render/RenderPipeline.h"
#include "platform/host/ZoneRule.h"

namespace awtrix::remote {

class ZoneClock final : public IPageClock {
 public:
  void setRule(const std::string& rule) {
    os::ZoneRule zone;
    zone.parse(rule);
    std::lock_guard<std::mutex> lock(mutex_);
    zone_ = zone;
  }

  os::LocalTime local(int64_t utcSeconds) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zone_.local(utcSeconds);
  }

  void fill(RenderCtx& ctx, int64_t nowMs) override {
    ctx.nowMs = nowMs;
    const int64_t epochMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    const os::LocalTime t = local(epochMs / 1000);
    ctx.hour = t.hour;
    ctx.minute = t.minute;
    ctx.second = t.second;
    ctx.weekday = t.weekday;
    ctx.mday = t.day;
    ctx.month = t.month;
    ctx.year = t.year;
    ctx.epochMs = ctx.year >= 2020 ? epochMs : -1;
  }

  void fillLog(std::time_t now, std::tm& out) const {
    const os::LocalTime t = local(static_cast<int64_t>(now));
    out = std::tm{};
    out.tm_year = t.year - 1900;
    out.tm_mon = t.month - 1;
    out.tm_mday = t.day;
    out.tm_hour = t.hour;
    out.tm_min = t.minute;
    out.tm_sec = t.second;
    out.tm_wday = t.weekday;
    out.tm_yday = t.yearDay;
    out.tm_isdst = t.dst ? 1 : 0;
  }

 private:
  mutable std::mutex mutex_;
  os::ZoneRule zone_;
};

}
