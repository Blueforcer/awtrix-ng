#pragma once
#include <cstdlib>
#include <ctime>
#include <WiFi.h>
#include <lwip/apps/sntp.h>
#include "system/TimeConfig.h"

namespace awtrix::platform {
class TimeService {
 public:
  void apply(const std::string& tz, const std::string& server, bool force = false) {
    // Stop before changing owned hostname storage retained by lwIP.
    if (!config_.needsUpdate(tz, server, force)) return;
    sntp_stop();
    config_.update(tz, server, force);
    setenv("TZ", config_.timezone().c_str(), 1);
    tzset();
    sntp_setservername(0, config_.server().c_str());
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_init(); // asynchronous DNS, retry and system-clock update supplied by the core

  }
 private:
  TimeConfig config_;

};
}
