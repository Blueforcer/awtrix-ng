**Added**

- **Pimoroni Galactic Unicorn support.** Dedicated Pico W and Pico 2 W UF2 builds, fixed 53×11 PIO/DMA display, light sensor, buttons, I²S RTTTL melodies, LittleFS persistence, Wi-Fi provisioning, HTTP/MQTT API, Home Assistant, NTP, discovery, Art-Net and watchdog recovery. Pico W is hardware-verified; Pico 2 W is compile-only.
- **Tall panels.** Configurable height 8–16 for generic panels, with eight-row content centred above full-canvas effects, backgrounds, charts and transitions.
- **Capability-driven controls.** Builds without scripting hide its toggle and editor; unsupported MP3/radio controls remain hidden with an availability note. The initial Pico builds omit scripting, MP3/radio, outbound TLS and browser OTA, so the web UI offers no browser install there; use USB BOOTSEL updates and note that timed sleep is emulated, not deep sleep.
- **Install firmware updates from the web UI.** Click **Check for updates**, then **Download & install** and confirm. Updates are checked only when you ask. Your browser downloads and verifies the correct image, then uploads it to AWTRIX with progress shown throughout. Settings and files are kept.
- **Modbus TCP for Berry scripts.** Read holding registers, input registers, coils and discrete inputs from local devices without blocking the display. Helpers decode signed integers and 32-bit floating-point values, with configurable host, port and unit ID.
- **Timers and delayed actions.** `timer.after()` runs a callback once; `timer.every()` repeats it; `timer.cancel()` cancels it. Timers work in the background, including in headless scripts, and are cleaned up when a script is replaced, removed or fails.
- **More button events for interactive apps.** `on_button_event()` supports press, long press, repeat and release. An app can capture a press and handle the whole gesture. Existing `on_button()` scripts and normal device navigation keep working when the new handler does not consume the press.
- **Multiple icons in pushed apps and notifications.** The new `icons` array adds up to four independently animated icons at chosen positions, alongside the existing `icon` field.
- **Adjustable spacing beside icons.** `iconGap` sets the gap between the main icon and text, from 0 to 128 pixels. The default remains one pixel.

**Changed**

- **BrickBreaker, Snake and PingPong are real mini-games on every panel size.** BrickBreaker shows separate 3-pixel bricks (a third row from 11 rows up) knocked out one at a time, with two bounce angles. Snake chases red food and grows. PingPong has a paddle on each side. BrickBreaker and PingPong keep fixed colours; Snake still takes the palette for its body.
- **GIFs keep their own size, up to the panel dimensions.** Scripts, pushed apps and notifications support GIFs sized for custom panels. Text layout follows the icon's actual width; a panel-wide GIF becomes a background.
- Each script app manages its own icons and releases them when hidden. File access and streamed log and Wi-Fi scan responses use less temporary memory.
- The scripting documentation, AI prompt and downloadable agent skill cover Modbus, timers and extended button events, with corrected settings instructions and guidance for different panel sizes and PSRAM budgets.

**Fixed**

- Scrolling text no longer enters the gap beside an icon.
- The countdown tutorial correctly parses months and days with leading zeros, such as `09`, instead of falling back to the default date.

---

**Which file do I need?**

| Your board | Update a running AWTRIX NG | First install over USB |
|---|---|---|
| Classic ESP32 - Ulanzi TC001, AWTRIX 2 conversions, most DIY | `firmware-awtrix-ng.bin` | `usb-awtrix-ng-<flash>.bin` |
| ESP32-S3, octal PSRAM (`N8R8`, `N16R8`) or no PSRAM | `firmware-awtrix-ng-s3-octal.bin` | `usb-awtrix-ng-s3-octal-<flash>.bin` |
| ESP32-S3, quad PSRAM (`N8R2`, `N16R2`, `N4R2`) | `firmware-awtrix-ng-s3-quad.bin` | `usb-awtrix-ng-s3-quad-<flash>.bin` |
| Galactic Unicorn, Pico W | USB BOOTSEL only | `firmware-galactic-unicorn.uf2` |
| Galactic Unicorn, Pico 2 W (compile-only) | USB BOOTSEL only | `firmware-galactic-unicorn-2w.uf2` |
