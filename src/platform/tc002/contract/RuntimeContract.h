#pragma once

#include <string>

// How awtrix-tc002d starts awtrix-linux: the descriptors the runtime inherits and the flags that
// hand them over. The runtime accepts exactly these numbers.
namespace awtrix {
namespace tc002 {

// The evdev devices of the three top buttons and of the knob, opened by the hardware lease.
constexpr int kKeysFd = 100;
constexpr int kKnobFd = 101;
// The SOCK_SEQPACKET channel of SupervisorProtocol.h.
constexpr int kSupervisorFd = 103;
// The socket to the awtrix-tc002-audio-pcm speaker helper; the helper gets its end on the same number.
constexpr int kAudioFd = 104;

constexpr const char* kInputFdsFlag = "--tc002-input-fds";
constexpr const char* kSupervisorFdFlag = "--supervisor-fd";
constexpr const char* kAudioFdFlag = "--tc002-audio-fd";
// The supervisor can attach the Bluetooth controller when the runtime asks (the bluetooth message).
constexpr const char* kBluetoothFlag = "--tc002-ble";
constexpr const char* kUpdateDirFlag = "--update-dir";
constexpr const char* kStartReasonFlag = "--start-reason";
constexpr const char* kUidFlag = "--uid";

// Why the runtime starts, named like the ESP32's reset reasons (resetReason in /api/v1/device): the
// first start after the clock powered on; after awtrix-tc002d rebooted the clock or restarted
// the runtime, on request or after an update; after the runtime crashed; after the daemon ended a
// runtime that did not report readiness.
constexpr const char* kStartPowerOn = "poweron";
constexpr const char* kStartSoftware = "software";
constexpr const char* kStartPanic = "panic";
constexpr const char* kStartWatchdog = "watchdog";

inline bool validStartReason(const std::string& reason) {
  return reason == kStartPowerOn || reason == kStartSoftware || reason == kStartPanic || reason == kStartWatchdog;
}

// The clock's id (uid in /api/v1/device, the default MQTT prefix and client id): the Wi-Fi MAC as
// twelve lowercase hex digits, as on the ESP32.
inline bool validUid(const std::string& uid) {
  if (uid.size() != 12) return false;
  for (const char c : uid)
    if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) return false;
  return true;
}

inline std::string inputFdsValue() { return std::to_string(kKeysFd) + "," + std::to_string(kKnobFd); }

}
}
