**Added**
- **Clips.** Boards with a speaker play your own MP3 files: drop them into the new **Audio** tab, then play one by name - `sound:"ding"` in a notification, from a script, or over the API. A clip interrupts a running stream, which comes back by itself afterwards.
- **ESP32-S3 boards with quad PSRAM** (`N8R2`, `N16R2`, `N4R2`) have their own image and can play the radio, which they could not before. The browser flasher recognises which PSRAM a board has and picks for you; the wrong image is refused instead of installed.
- Scripts can ask whether the device is still making a sound, with `sound.playing()`.
- A **converter for AWTRIX 3 flows** on the documentation site turns an old configuration into the AWTRIX NG equivalent.

**Changed**
- **The audio API has changed.** Radio and sounds shared one speaker but had two addresses; everything now lives under `/api/v1/audio`, and MQTT under `cmd/audio/*`. Anything that drives sound from outside - Home Assistant, Node-RED, your own scripts - needs adjusting: [HTTP API](https://ang.blueforcer.de/reference/http/#audio) · [MQTT](https://ang.blueforcer.de/reference/mqtt/).
- **Sounds and Radio are one Audio tab**: Clips, Melodies, Radio, and one stop button for all of it. Old `#/sounds` and `#/radio` bookmarks land there.
- The USB install images come as a single `usb-awtrix-ng.zip`, so the update files are what you see first on this page.
- Both ESP32-S3 images now say which PSRAM they are for: `firmware-awtrix-ng-s3-octal.bin` for `N8R8`/`N16R8` boards and boards without PSRAM, `firmware-awtrix-ng-s3-quad.bin` for the quad ones. The plain `-s3` name is gone.

**Fixed**
- The **LookingEyes** effect drew small square eyes instead of the full-size ones AWTRIX 3 has. They are back to size, look around properly and blink again.
- Uploading a script reserved memory for the largest script allowed rather than the one being sent, so a save could be refused on a busy device.
- A clip whose file name cannot be played back - spaces, brackets, accents, over 32 characters - is refused at upload instead of sitting there unplayable.

---

**Which file do I need?**

Easiest is the [browser flasher](https://ang.blueforcer.de/getting-started/flashing/) - it detects your board and picks for you.

| Your board | Update a running AWTRIX NG | First install over USB |
|---|---|---|
| Classic ESP32 - Ulanzi TC001, AWTRIX 2 conversions, most DIY | `firmware-awtrix-ng.bin` | `usb-awtrix-ng-<flash>.bin` |
| ESP32-S3, octal PSRAM (`N8R8`, `N16R8`) or no PSRAM | `firmware-awtrix-ng-s3-octal.bin` | `usb-awtrix-ng-s3-octal-<flash>.bin` |
| ESP32-S3, quad PSRAM (`N8R2`, `N16R2`, `N4R2`) | `firmware-awtrix-ng-s3-quad.bin` | `usb-awtrix-ng-s3-quad-<flash>.bin` |

The `usb-*.bin` images are in `usb-awtrix-ng.zip`, `<flash>` is your board's flash size. Update files go on the device's update page; USB images are written with [esptool](https://ang.blueforcer.de/getting-started/flashing/#4-flash-it-with-esptool) to offset `0x0`.
