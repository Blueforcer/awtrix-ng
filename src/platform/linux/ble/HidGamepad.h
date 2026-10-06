#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/linux/ble/BleTypes.h"

namespace awtrix::ble {

// One control inside an input report: where its bits start, how many there are, and its logical
// minimum - negative for a value that is signed on the wire, 1 for a hat that counts from 1.
struct HidField {
  int bit = -1;
  int bits = 0;
  int32_t min = 0;
  bool valid() const { return bit >= 0 && bits > 0; }
};

// Where a gamepad's input report puts its controls. Only a report inside a Gamepad or Joystick
// application collection that carries buttons counts; a stick missing from it stays invalid and
// reads as centred. reportId is 0 for a device that numbers no reports.
struct HidLayout {
  std::size_t length = 0;
  int reportId = 0;
  HidField hat, lx, ly, rx, ry, lt, rt, buttons;
};

// Walks a HID report map (the Report Map characteristic, 0x2A4B). False when it describes no
// gamepad: a keyboard, a mouse or a remote.
bool parseHidReportMap(const Bytes& map, HidLayout& out);

// A report as the controls read: sticks and triggers scaled to one byte (128 = centre for sticks),
// the hat 0-7 clockwise from up or -1 when released, button n+1 in bit n.
struct HidControls {
  uint32_t buttons = 0;
  int hat = -1;
  uint8_t axes[4] = {128, 128, 128, 128};
  uint8_t triggers[2] = {0, 0};
};

// False when the report is not the layout's length.
bool readHidReport(const Bytes& report, const HidLayout& layout, HidControls& out);

}
