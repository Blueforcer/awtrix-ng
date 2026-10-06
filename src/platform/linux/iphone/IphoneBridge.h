#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/Services.h"
#include "platform/linux/ble/IphoneLink.h"
#include "platform/linux/iphone/IphoneCovers.h"
#include "platform/linux/iphone/IphonePayload.h"
#include "platform/linux/iphone/IphoneSettings.h"

namespace awtrix::iphone {

// The main loop's half of the iPhone link: its settings and their file, the HTTP routes
// GET/PUT /api/v1/iphone and DELETE /api/v1/iphone/phone, and what the phone's events become on
// the display: notifications of the listed apps, and the pushed app kMusicApp while the phone
// plays. Main loop only, as the engine is.
class IphoneBridge {
 public:
  static constexpr int64_t kRepeatMs = 2000;
  static constexpr int64_t kSettleMs = 400;
  static constexpr int64_t kCoverWaitMs = 2000;
  static constexpr int64_t kRefreshMs = 20000;
  static constexpr int64_t kGraceMs = 6000;
  static constexpr int64_t kSeenSaveMs = 10000;

  struct Options {
    Panel panel;
    std::string path;
    std::function<long long()> unixTime;
    std::function<void(const std::string&)> log;
  };

  // Reads the file and hands the link what it says. covers may be null.
  IphoneBridge(ble::IphoneControl& link, INotifyService& notify, IAppService& apps, CoverSource* covers,
               Options options);

  // The HTTP status and its JSON body, or 0 for a path that is not the iPhone's.
  int handle(const std::string& method, const std::string& path, const std::string& request, std::string& body);
  void tick(int64_t nowMs);
  // Writes what is still unsaved.
  void flush();
  const Settings& settings() const { return settings_; }

  // The built-in icon of the now-playing app without a cover, size 8 or 16.
  static const char* noteIcon(int size);

 private:
  void onPhone(const ble::IphoneEvent& e);
  void onNotification(const ble::ancs::Attributes& a, int64_t nowMs);
  void music(int64_t nowMs);
  std::string cover(int64_t nowMs, bool& waiting);
  void removeMusic();
  bool save(const Settings& settings);
  std::string body() const;

  ble::IphoneControl& link_;
  INotifyService& notify_;
  IAppService& apps_;
  CoverSource* covers_;
  Options options_;
  Settings settings_;
  bool seenDirty_ = false;
  int64_t seenSavedAt_ = 0;

  std::string lastKey_;
  int64_t lastAt_ = -1;

  ble::ams::Playback track_;
  int64_t settleAt_ = -1;
  int64_t stopAt_ = -1;
  bool pushed_ = false;
  int64_t pushedAt_ = 0;
  std::string shownKey_, shownCover_;
  int shownLeds_ = -1;

  CoverCache cache_;
  std::string lookupKey_;
  int64_t lookupSince_ = 0;
};

}
