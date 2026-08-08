#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include <functional>

#include "core/PinRules.h"
#include "core/Services.h"
#include "core/render/Canvas.h"
#include "system/ClipPlayback.h"
#include "hal/IBoard.h"
#include "transport/DeviceStateJson.h"

namespace awtrix {

using Publisher = std::function<void(const std::string& suffix, const std::string& payload)>;


class DeviceSound : public ISoundService {
 public:
  explicit DeviceSound(IBoard& board) : board_(board) {}
  bool playSound(const std::string& name, SoundKind kind) override {
    if (kind != SoundKind::Melody && playStoredClip(clips_, name)) return true;
    if (kind == SoundKind::Clip) return false;
    return board_.sound().playFile(name);
  }
  void playRtttl(const std::string& rtttl) override { board_.sound().playRtttl(rtttl); }
  void r2d2(const std::string& ) override {
    board_.sound().playRtttl("r2d2:d=4,o=5,b=240:16c6,16g6,16e6,16a6,16g6,16e7");
  }
  void stop() override {
    if (clips_) clips_->stopClip();
    board_.sound().stop();
  }
  bool supportsRtttl() const override { return board_.sound().supportsRtttl(); }

  void setClips(IClipService* clips) { clips_ = clips; }

 private:
  IBoard& board_;
  IClipService* clips_ = nullptr;
};

class DeviceDisplay : public IDisplayService {
 public:
  void setPublisher(Publisher pub) { pub_ = std::move(pub); }
  void setScreen(Canvas* screen) { screen_ = screen; }
  void sendScreen() override {
    if (pub_ && screen_) pub_("state/screen", buildScreenJson(*screen_));
  }

 private:
  Publisher pub_;
  Canvas* screen_ = nullptr;
};

class DeviceSystem : public ISystemService {
 public:
  enum class Pending { None, Reboot, Sleep, FactoryReset, ResetSettings };

  // These only record the intent. Rebooting or wiping flash from inside a request would kill the
  // connection before the response goes out, so runPending() does the work later in the loop.
  void reboot() override { pending_ = Pending::Reboot; }
  void sleep(uint64_t durationMs) override {
    if (durationMs == 0) return;
    sleepMs_ = durationMs;
    pending_ = Pending::Sleep;
  }
  void factoryReset() override { pending_ = Pending::FactoryReset; }
  void resetSettings() override { pending_ = Pending::ResetSettings; }

  // Only an RTC-capable GPIO survives deep sleep; anything else is stored as -1 so the device wakes
  // on the timer alone rather than never.
  void setWakeButtonPin(int pin) { wakePin_ = pins::isRtcWakePin(pin) ? pin : -1; }
  void setDisplayOff(std::function<void()> fn) { displayOff_ = std::move(fn); }

  bool hasPending() const { return pending_ != Pending::None; }
  void runPending() {
    const Pending p = pending_;
    pending_ = Pending::None;
    switch (p) {
      case Pending::Reboot:
        // The delays give the pending HTTP response and MQTT publish time onto the wire.
        delay(200);
        ESP.restart();
        break;
      case Pending::Sleep:
        if (displayOff_) displayOff_();
        esp_sleep_enable_timer_wakeup(sleepMs_ * 1000ULL);
        if (wakePin_ >= 0) {
          const gpio_num_t g = static_cast<gpio_num_t>(wakePin_);
          rtc_gpio_pullup_en(g);
          rtc_gpio_pulldown_dis(g);
          esp_sleep_enable_ext0_wakeup(g, 0);
        }
        delay(200);
        esp_deep_sleep_start();
        break;
      case Pending::FactoryReset:
        // Three namespaces because older firmware wrote under different names; leaving one behind
        // means settings reappear after the reset.
        clearNvs("awtrix-ng");
        clearNvs("awtrix-cfg");
        clearNvs("awtrix");
        LittleFS.format();
        WiFi.disconnect(true, true);
        delay(300);
        ESP.restart();
        break;
      case Pending::ResetSettings:
        clearNvs("awtrix-ng");
        clearNvs("awtrix");
        delay(200);
        ESP.restart();
        break;
      case Pending::None:
        break;
    }
  }


 private:
  static void clearNvs(const char* ns) {
    Preferences p;
    p.begin(ns, false);
    p.clear();
    p.end();
  }
  Pending pending_ = Pending::None;
  uint64_t sleepMs_ = 0;
  int wakePin_ = -1;
  std::function<void()> displayOff_;
};

}
