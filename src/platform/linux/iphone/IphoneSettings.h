#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/api/JsonWriter.h"
#include "platform/linux/ble/BleTypes.h"

namespace awtrix::iphone {

// A rule for one app: only the apps listed here reach the display.
struct AppRule {
  std::string id;
  std::string name;
  std::string icon;
};

// An app that sent a notification, listed or not.
struct Seen {
  std::string id;
  std::string title;
  long long at = 0;
};

struct Settings {
  static constexpr std::size_t kMaxApps = 32;
  static constexpr std::size_t kMaxSeen = 20;
  static constexpr std::size_t kMaxIdChars = 128;
  static constexpr std::size_t kMaxNameChars = 64;
  static constexpr std::size_t kMaxIconBytes = 6000;
  static constexpr std::size_t kMaxSeenTitleChars = 48;
  static constexpr std::size_t kMaxFileBytes = 512 * 1024;

  bool enabled = false;
  bool music = true;
  bool covers = true;
  std::vector<AppRule> apps;
  bool havePhone = false;
  ble::Address phone;
  std::string phoneName;
  std::vector<Seen> seen;

  const AppRule* rule(const std::string& id) const;
  // Newest first, one entry per app.
  void saw(const std::string& id, const std::string& title, long long at);
};

// PUT /api/v1/iphone: {"enabled"?, "music"?, "covers"?, "apps"?}, apps replacing the list. Nothing
// changes unless all of it is valid; message and field say what is not. malformed is set for a
// body that is no JSON object.
bool applyUpdate(const std::string& json, Settings& settings, bool& malformed, std::string& message,
                 std::string& field);

// The parts of the HTTP body the settings own, each as one value.
void writeSettings(api::JsonWriter& w, const Settings& settings);
void writePhone(api::JsonWriter& w, const Settings& settings);
void writeSeen(api::JsonWriter& w, const Settings& settings);

// The file holds all of it; a part that does not read back is left at its default.
std::string toFile(const Settings& settings);
Settings fromFile(const std::string& text);

}
