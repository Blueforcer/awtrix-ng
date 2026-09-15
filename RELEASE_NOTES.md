**Added**

- **🎉 The AWTRIX Hub is here!** Discover community-made flows and icons, create your own in the Pixel Studio, and send your favourites straight to your display at [awtrix.de](https://awtrix.de). The Web UI automatically checks Hub-installed scripts for updates and applies them with one click, preserving their settings and protecting local changes.
- **The live display on a page of its own**: `http://<awtrix-ip>/fullscreen`, made for an iframe on a Home Assistant dashboard (#29).
- **Scripts can declare their Hub icons** with `# @icons ...`. The editor and Apps tab show missing icons and install them in one action.
- The browser tab carries the hostname, so several AWTRIX open at once are told apart (#18).
- Scripts can swallow a button press: return `true` from `on_button()`.
- Scripts can switch the matrix with `display.power()` and read its state with `display.is_on()` (#56).
- **The web UI checks for updates.** The System page compares the running version with the latest GitHub release and offers the download for exactly your board; the dashboard shows *update available*. The check runs in the browser, the clock never talks to GitHub. `GET /api/v1/device` now names the file it updates from as `updateImage`.
- **Scripts can react to the music.** On an ESP32-S3 with a speaker, `music.bands()`, `music.level()` and `music.beat()` describe what the radio or a stored MP3 is playing, timed to the speaker. A spectrum display is one line: `bar_chart(music.bands(16, 8), "Rainbow", false)`. Other boards answer zeros, so the same script runs everywhere.
- **More DIY audio hardware is supported.** `pinI2sMclk` supplies DACs requiring a master clock, while `pinAmpEnable` controls amplifiers with an enable input.
- DIY panels can select their physical LED colour order in the Panel settings (#54).
- Directional app transitions can run in their normal or reversed direction (#49).
- Auto brightness can be switched directly from the dashboard; manual brightness stays disabled while it is active (#38).
- Backup creation has an **All** switch that selects every available category at once (#43).
- `progress()`, `bar_chart()` and `line_chart()` take an optional x offset.
- Script HTTP requests accept `cap` to choose how much of a response may be retained, bounded by available memory.
- Scripting tutorials on the documentation site.

**Changed**

- **The button webhook sends JSON.** `buttonCallback` now posts `{"button":"left","state":true,"uid":"…"}` with `Content-Type: application/json` instead of a form-encoded body. A listener that reads `button=…&state=1` needs adjusting.
- **Berry scripts now use available memory instead of most fixed caps.** Besides removing `scriptLimit` and `scriptMaxBytes`, fixed limits on configuration fields, select options, imports, shared values, stores and HTTP request data were replaced with available-memory checks. Sending the two removed keys is ignored rather than refused, but a backup or an automation that still writes them needs looking at.
- Berry VMs, regular expressions and GIF decoding retain less temporary memory, improving reliability when several scripts or animations run together.

**Fixed**

- An I2S amplifier crackled and hissed from power-on until the first sound played: the I2S lines floated until then. They are now held low from boot.
- A notification with an empty `soundRtttl` was refused outright, so clients that send their whole schema - Home Assistant among them - got nothing at all (#27).
- An app pushed under a built-in name like `Temperature` was stored and listed, but the panel kept showing the built-in. The pushed app takes the name over now (#37).
- An icon that is a PNG under a `.jpg` name counted as drawn, leaving a black gap where the picture should be. The column goes back to the text and the log names the file (#23).
- One time zone the browser does not know ended the System page halfway, with no maintenance and no backup below MQTT (#25).
- Files in downloaded backup ZIPs now carry the backup creation time instead of invalid 1979/1601 timestamps (#44).
- The Icons tab could show an empty Hub area, and the framed icon editor was not told which Hub to publish to.
- Delete and duplicate in the icon editor were blank grey chips. Ships with the Hub, not with the firmware, so it is already fixed (#30).

---

**Which file do I need?**

| Your board | Update a running AWTRIX NG | First install over USB |
|---|---|---|
| Classic ESP32 - Ulanzi TC001, AWTRIX 2 conversions, most DIY | `firmware-awtrix-ng.bin` | `usb-awtrix-ng-<flash>.bin` |
| ESP32-S3, octal PSRAM (`N8R8`, `N16R8`) or no PSRAM | `firmware-awtrix-ng-s3-octal.bin` | `usb-awtrix-ng-s3-octal-<flash>.bin` |
| ESP32-S3, quad PSRAM (`N8R2`, `N16R2`, `N4R2`) | `firmware-awtrix-ng-s3-quad.bin` | `usb-awtrix-ng-s3-quad-<flash>.bin` |
