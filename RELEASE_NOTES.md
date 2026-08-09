**Added**
- **MP3s.** Boards with a connected I2S DAC play your own MP3 files: drop them into the new **Audio** tab, then play one by name - `sound:"ding"` in a notification, from a script, or over the API. 
- **ESP32-S3 boards with quad PSRAM** (`N8R2`, `N16R2`, `N4R2`) have their own image. The browser flasher recognises which PSRAM a board has and picks for you; the wrong image is refused instead of installed.
- **Every sound output has its own volume.** Buzzer, DFPlayer, stored MP3s and the radio are four separate sliders now, each 0-100. 
- **A DFPlayer no longer costs you the buzzer.** The two are separate outputs and both stay live, so a board with an added MP3 module keeps its melodies, its melody editor and its `soundRtttl` in notifications.
- New keys on `POST /api/v1/audio/play`: `sound` for "a name, you decide where it comes from" - the same rule a notification's `sound` follows - and `track` for a DFPlayer track number. `mp3`, `melody`, `track` and `rtttl` each name one output and never quietly fall back to another.
- `POST /api/v1/audio/stop` takes an optional `{"scope":"sounds"|"stream"|"all"}`, so a script can silence its own chime without killing the radio someone is listening to.
- Scripts can ask whether the device is still making a sound, with `sound.playing()`, which output it has, with `sound.sinks()`, and name one directly with `sound.mp3()`, `sound.melody()` and `sound.track()`.
- A **converter for AWTRIX 3 flows** on the documentation site turns an old configuration into the AWTRIX NG equivalent.

**Changed**
- **The audio API has changed.** Radio and sounds shared one speaker but had two addresses; everything now lives under `/api/v1/audio`, and MQTT under `cmd/audio/*`. Anything that drives sound from outside - Home Assistant, Node-RED, your own scripts - needs adjusting: [HTTP API](https://blueforcer.github.io/awtrix-ng/reference/http/#audio) · [MQTT](https://blueforcer.github.io/awtrix-ng/reference/mqtt/).
- **Sounds and Radio are one Audio tab**: MP3s, Melodies, Radio.
- **`volume` is gone**, replaced by `buzzerVolume`, `dfplayerVolume`, `mp3Volume` and `radioVolume`, all on a 0-100 scale instead of 0-30. `radioVolume` keeps its name, its scale and your stored value; the old `volume` is dropped, so the buzzer, DFPlayer and MP3 volumes start at their defaults once - which are the old ones expressed in the new scale, so nothing gets quieter. `PATCH {"volume":10}` now answers `422`: anything driving AWTRIX from Node-RED or Home Assistant needs the new key.
- **A backup taken before 1.1.0 loses its settings on restore.** Unknown keys are refused outright, so the whole settings category is skipped with a warning while icons, melodies, MP3s, palettes and scripts restore normally. Check your volumes afterwards.
- **`melody` on a DFPlayer board no longer means a track number.** It means a stored melody, as it does everywhere else, and `{"track":7}` is how you address the SD card. `{"sound":"7"}` still works and now prefers a `/MP3/7.mp3` if you have uploaded one.
- **`soundEnabled` no longer mutes the radio.** It covers one-shot sounds - melodies, MP3s, tracks, a notification's own melody. A stream is something you started deliberately; use `{"scope":"stream"}` on `/audio/stop` to end it. `sound.stop()` in a script follows the same rule.
- **`GET /api/v1/capabilities` reports one `audio` object** - `{"buzzer":…,"track":…,"mp3":…,"radio":…}` - in place of the four loose `radio`/`audio`/`melodies`/`sound` flags, two of which could never disagree.
- A notification whose `soundRtttl` does not parse is now rejected with `422` instead of being accepted and then silently playing nothing.
- A melody or MP3 you ask for by name on a panel that has no such output answers `503` rather than `422`: the request was fine, the hardware is not there.
- The USB install images come as a single `usb-awtrix-ng.zip`, so the update files are what you see first on this page.
- Both ESP32-S3 images now say which PSRAM they are for: `firmware-awtrix-ng-s3-octal.bin` for `N8R8`/`N16R8` boards, `firmware-awtrix-ng-s3-quad.bin` for the quad ones. The plain `-s3` name is gone.

**Fixed**
- The **LookingEyes** effect drew small square eyes instead of the full-size ones AWTRIX 3 has. They are back to size, look around properly and blink again.
- Uploading a script reserved memory for the largest script allowed rather than the one being sent, so a save could be refused on a busy device.
- An MP3 whose file name cannot be played back - spaces, brackets, accents, over 32 characters - is refused at upload instead of sitting there unplayable.

---

**Which file do I need?**

| Your board | Update a running AWTRIX NG | First install over USB |
|---|---|---|
| Classic ESP32 - Ulanzi TC001, AWTRIX 2 conversions, most DIY | `firmware-awtrix-ng.bin` | `usb-awtrix-ng-<flash>.bin` |
| ESP32-S3, octal PSRAM (`N8R8`, `N16R8`) or no PSRAM | `firmware-awtrix-ng-s3-octal.bin` | `usb-awtrix-ng-s3-octal-<flash>.bin` |
| ESP32-S3, quad PSRAM (`N8R2`, `N16R2`, `N4R2`) | `firmware-awtrix-ng-s3-quad.bin` | `usb-awtrix-ng-s3-quad-<flash>.bin` |