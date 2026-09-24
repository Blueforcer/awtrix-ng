#pragma once
#include <cstdint>
namespace awtrix::platform {
// Must run after the display claims PIO/DMA and before any WiFi call.
void beginRadio();

// Turns off the CYW43 firmware's own roaming (the "roam_off" variable) and reads it back; returns
// the value the firmware now reports, or -1 if it could not be read. ESP32 never roams on its own;
// on an eero mesh the firmware roamed back and forth between two nodes within half a minute and a
// roam's key exchange timed out, dropping the link. Roaming stays with NetworkService::roamIfWeak.
int disableFirmwareRoaming();
// The firmware's current "roam_off" value, or -1 if it could not be read.
int firmwareRoamOff();

// One CYW43 driver event (join, auth, deauth, link, key exchange), recorded as the driver
// processes it. Scan results are not recorded.
struct RadioEvent {
  uint32_t ms;
  uint32_t type;
  uint32_t status;
  uint32_t reason;
  uint16_t flags;
  uint8_t itf;
  uint8_t addr[6];
  uint32_t joinState;  // the driver's join state after the event
};
// Oldest recorded event not yet taken; false when none is pending. Events that arrive while the
// buffer is full are counted in `dropped`.
bool takeRadioEvent(RadioEvent& out, uint32_t& dropped);
const char* radioEventName(uint32_t type);
}
