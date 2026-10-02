#pragma once
// Pimoroni pico v1.23.0 galactic_unicorn.pio, expressed with SDK encoders to
// avoid an external pioasm build dependency. Copyright (c) 2021 Pimoroni Ltd.
// MIT; see LICENSES/MIT-Pimoroni.txt. Jump operands are relocated by pio_add_program.
#include <hardware/pio.h>
#include <hardware/pio_instructions.h>
namespace awtrix::galactic {
inline const uint16_t instructions[] = {
  uint16_t(pio_encode_out(pio_y, 8)),                             // 0: pixel count - 1
  uint16_t(pio_encode_out(pio_pins, 8)),                          // 1: row select
  uint16_t(pio_encode_out(pio_x, 1) | pio_encode_sideset_opt(1, 0) | pio_encode_delay(1)),
  uint16_t(pio_encode_set(pio_pins, 4)),
  uint16_t(pio_encode_jmp_not_x(6)),
  uint16_t(pio_encode_set(pio_pins, 5)),
  uint16_t(pio_encode_nop() | pio_encode_sideset_opt(1, 1) | pio_encode_delay(2)),
  uint16_t(pio_encode_out(pio_x, 1) | pio_encode_sideset_opt(1, 0) | pio_encode_delay(1)),
  uint16_t(pio_encode_set(pio_pins, 4)),
  uint16_t(pio_encode_jmp_not_x(11)),
  uint16_t(pio_encode_set(pio_pins, 5)),
  uint16_t(pio_encode_nop() | pio_encode_sideset_opt(1, 1) | pio_encode_delay(2)),
  uint16_t(pio_encode_out(pio_x, 1) | pio_encode_sideset_opt(1, 0) | pio_encode_delay(1)),
  uint16_t(pio_encode_set(pio_pins, 4)),
  uint16_t(pio_encode_jmp_not_x(16)),
  uint16_t(pio_encode_set(pio_pins, 5)),
  uint16_t(pio_encode_out(pio_null, 5) | pio_encode_sideset_opt(1, 1) | pio_encode_delay(2)),
  uint16_t(pio_encode_jmp_y_dec(2)),
  uint16_t(pio_encode_out(pio_null, 8)),                          // padding byte
  uint16_t(pio_encode_set(pio_pins, 6) | pio_encode_delay(5)),     // latch, blank
  uint16_t(pio_encode_set(pio_pins, 0)),                          // enable output
  uint16_t(pio_encode_out(pio_y, 32)),                           // binary-weighted dwell
  uint16_t(pio_encode_jmp_y_dec(22)),
  uint16_t(pio_encode_set(pio_pins, 4)),                          // blank before next row
};
inline const pio_program program = {instructions, 24, -1};
}
