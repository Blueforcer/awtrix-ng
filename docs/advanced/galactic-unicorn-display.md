# Galactic Unicorn display

The Pico W/Pico 2 W board drives the fixed 53x11 panel using the display half
of Pimoroni pico **v1.23.0** (`3440ab232cdc2b019bb8d16f67b0448502efd9dc`, MIT).
Audio and pico_graphics are not included. See `THIRD-PARTY-NOTICES.md`.

## Height and wiring

`panelHeight` defaults to **11**. **8** produces a 53x8 canvas letterboxed on
physical rows **1–8** (zero-based); rows **0, 9, 10** remain dark. Any other
height uses 11 and logs a warning at board startup. Width is always 53, with
one panel and fixed wiring, so the width, chaining and wiring keys have no
effect. `mirror` and `rotate` flip the picture as on any panel, and `rotate`
also swaps A and C.

Clock/data/latch/blank are GPIO 13/14/15/16, row selects GPIO 17–20. The
physical stream reverses both axes, matching Pimoroni's original driver.

## Refresh and color

One dynamically claimed PIO SM and two chained DMA channels continuously
refresh fourteen binary-weighted bit planes without CPU refresh interrupts.
PIO1 is preferred to leave PIO0 instruction memory for CYW43; allocation checks
both free SMs and program space, and can fall back to PIO0. The startup log
prints the actual display PIO/SM and DMA channels. The current bootstrap does
**not initialize Wi-Fi**, so CYW43 has **no PIO assignment yet**. The network
phase must log its actual assignment after initialization; no fixed SM is
reserved or assumed by this driver.

Two aligned 9,240-byte buffers (18,480 bytes total) fit in static RAM. `show()`
packs the inactive buffer, publishes its aligned address, then waits until DMA
has begun reading it before reusing the old buffer. The control channel only
loads a new address at a full-frame boundary. The PIO FIFO preserves ordering
across that boundary. Calls must be serialized on core 0; the board is a single
device-lifetime instance. Each show may wait up to one refresh period; refresh
continues autonomously between calls, including while flash writes run.

Saturation, gamma, brightness, correction and tint follow NG ColorGrade's
ordering. A 14-bit LUT preserves panel precision instead of first quantizing
to an 8-bit intermediate canvas. Gamma is applied **once**, using the NG gamma
setting (default 1.9), replacing Pimoroni's fixed 2.2 curve. Brightness and color
balance scale linear light **after** gamma, unlike the upstream brightness
pre-scale. There is no second fixed gamma curve. Manual and automatic brightness
use the shared NG periphery service (median filtering, light curve and smoothing).

## Inputs

The map and pure debounce/step helpers live in `hal/GalacticUnicornDisplay.h`.
All buttons are active low with internal pull-ups and 35 ms debounce. A held
button acts once, not repeatedly; release and press again for another step.

| Input | GPIO | Function |
|---|---|---|
| A / B / C | 0 / 1 / 3 | Left / select / right |
| D | 6 | Unmapped |
| Sleep | 27 | Display power toggle |
| Volume up / down | 7 / 8 | `buzzerVolume` +/- 5, clamped 0–100 |
| Brightness up / down | 21 / 26 | `brightness` +/- 10, clamped 0–255; disables auto brightness |
| Light sensor | 28 (ADC2) | Automatic brightness |

ADC2 supplies native 12-bit counts (0–4095), not lux. Cover/uncover the sensor
with `autoBrightness` enabled to check the shared light curve and configured
min/max brightness and smoothing. Brightness keys select manual mode through
the same settings dispatcher as an API change. Volume keys use the onboard tone
channel setting (`buzzerVolume`); no audio sink is implemented yet, so there is
no audible output. DFPlayer/MP3/radio volume settings are not changed.

A goes to the previous app, C to the next (subject to NG rotate/swapButtons and
blockNavigation). B dismisses a notification; a double press within 300 ms
toggles power unless navigation is blocked. The shared periphery emits the same
`ButtonsChanged` state and left/middle/right callback names as Ulanzi. D is unmapped.
Extra keys dispatch `SetSettings` or `SetDisplay`, emitting normal settings/power
events; settings persist through the existing delayed LittleFS save. Power is
runtime-only, like the API, and fades out/in with NG's power animator; this is not
deep sleep. Button input continues while the panel is off.

The current Pico bootstrap has no network: MQTT/HA delivery and HTTP button
callbacks await the transport phase. Their source state/events are shared with
ESP32, not a private button protocol. ESP32's HTTP callback adapter is unchanged;
Pico networking must install its own `PeripheryService::setButtonPost` adapter.

ESP32 and Pico register the same `core/BuiltinCatalog.h`: five apps, nineteen
effects and six overlays, including the same palette-enabled subset. Missing
battery/environmental sensors still hide their apps. Boot logs print these
capability lists. With default settings expect Time 00:00/calendar 1, then Date
01.01.24 until the later NTP phase; no separate splash. Saved settings may differ.

Host tests cannot establish physical refresh timing, orientation, or absence of
visible tearing; check those on the panel with the CI-built UF2.
