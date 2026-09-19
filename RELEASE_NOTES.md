**Added**

- **Install firmware updates from the web UI.** Click **Check for updates**, then **Download & install** and confirm. Updates are checked only when you ask. Your browser downloads and verifies the correct image, then uploads it to AWTRIX with progress shown throughout. Settings and files are kept.
- **Modbus TCP for Berry scripts.** Read holding registers, input registers, coils and discrete inputs from local devices without blocking the display. Helpers decode signed integers and 32-bit floating-point values, with configurable host, port and unit ID.
- **Timers and delayed actions.** `timer.after()` runs a callback once; `timer.every()` repeats it; `timer.cancel()` cancels it. Timers work in the background, including in headless scripts, and are cleaned up when a script is replaced, removed or fails.
- **More button events for interactive apps.** `on_button_event()` supports press, long press, repeat and release. An app can capture a press and handle the whole gesture. Existing `on_button()` scripts and normal device navigation keep working when the new handler does not consume the press.
- **Multiple icons in pushed apps and notifications.** The new `icons` array adds up to four independently animated icons at chosen positions, alongside the existing `icon` field.
- **Adjustable spacing beside icons.** `iconGap` sets the gap between the main icon and text, from 0 to 128 pixels. The default remains one pixel.

**Changed**

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
