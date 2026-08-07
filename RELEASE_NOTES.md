**Added**
- **MP3 sounds.** Boards with a speaker play short MP3 clips: drop them into the web UI's **Audio** tab like icons, then play one by name - `sound:"ding"` in a notification, from a script, or over the API. Clips share the radio's volume, and a clip playing over a running stream lets the stream come back by itself. Needs an ESP32-S3 with an I2S DAC, the same one [internet radio](https://ang.blueforcer.de/guides/radio/) uses.
- Scripts can ask whether the device is still making a sound, with `sound.playing()` - useful to wait for one clip to end before starting the next.
- **ESP32-S3 boards with quad PSRAM** (`N8R2`, `N16R2`, `N4R2`) have their own firmware image and can play the radio, which they could not before. The browser flasher recognises which PSRAM a board has and writes the right one; uploading the wrong one to a running device is refused instead of installed.
- A **converter for AWTRIX 3 flows** on the documentation site turns an old configuration into the AWTRIX NG equivalent, and the guides now show what each example looks like on the panel.

**Changed**
- **Sounds and Radio are one Audio tab.** Clips, melodies and stations live on the same page. Old `#/sounds` and `#/radio` bookmarks land there.
- The USB install images come as a single `usb-awtrix-ng.zip` instead of seven separate downloads, so the update files are what you see first on the release page.
- Both ESP32-S3 images now say which PSRAM they are for: `firmware-awtrix-ng-s3-octal.bin` for `N8R8`/`N16R8` boards and boards without PSRAM, `firmware-awtrix-ng-s3-quad.bin` for the quad ones. The plain `-s3` name is gone.

**Fixed**
- The **LookingEyes** effect drew small square eyes instead of the full-size ones AWTRIX 3 has. They are back to size, look around properly and blink again.
- Uploading a script reserved as much memory as the largest script allowed, not as much as the script being uploaded, so a save could be refused on a busy device.
