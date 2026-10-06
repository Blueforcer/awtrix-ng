#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/linux/ble/BleTypes.h"

// The Apple Notification Center Service an iPhone offers a bonded accessory: a Notification
// Source that announces each notification, a Control Point to ask for its attributes and a Data
// Source that answers, one notification at a time.
namespace awtrix::ble::ancs {

constexpr const char* kService = "7905f431-b5ce-4e99-a40f-4b1e122d00d0";
constexpr const char* kNotificationSource = "9fbf120d-6301-42d9-8c58-25e699a21dbd";
constexpr const char* kControlPoint = "69d1d8f3-45e1-49a8-9821-9bbdfdaad9d9";
constexpr const char* kDataSource = "22eac6e9-24d6-4bb5-be44-b36ace7c7bfb";

enum Event : uint8_t { kAdded = 0, kModified = 1, kRemoved = 2 };
enum Flag : uint8_t { kSilent = 1, kImportant = 2, kPreExisting = 4, kPositiveAction = 8, kNegativeAction = 16 };

// The longest title and message asked for, in bytes; the phone cuts longer ones.
constexpr uint16_t kTitleBytes = 48;
constexpr uint16_t kMessageBytes = 120;

struct Notice {
  uint8_t event = 0;
  uint8_t flags = 0;
  uint8_t category = 0;
  uint8_t count = 0;
  uint32_t uid = 0;
  // A new notification worth showing: not one the phone already had when the link came up, and
  // not one it delivers without a sound.
  bool fresh() const { return event == kAdded && !(flags & (kPreExisting | kSilent)); }
};

// One Notification Source value; false when it is not 8 bytes.
bool parseNotice(const Bytes& value, Notice& out);

// GetNotificationAttributes for the app identifier, the title and the message.
Bytes attributesRequest(uint32_t uid);

struct Attributes {
  uint32_t uid = 0;
  std::string app, title, message;
};

// Puts one answer on the Data Source back together: the phone splits it over as many
// notifications as the link needs. Bytes that do not start an answer for the awaited uid, such as
// the tail of an answer that came too late, are dropped until one does.
class Assembler {
 public:
  enum class Result { Waiting, Done, Failed };
  static constexpr std::size_t kMaxBytes = 1024;

  void begin(uint32_t uid);
  void reset() { active_ = false; buffer_.clear(); }
  bool active() const { return active_; }
  Result feed(const Bytes& value);
  const Attributes& result() const { return result_; }

 private:
  bool active_ = false;
  Bytes buffer_;
  Attributes result_;
};

// Cuts a trailing UTF-8 sequence the phone's byte limit split in half.
void trimUtf8(std::string& text);

}
