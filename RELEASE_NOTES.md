**Added**

- **The live display on a page of its own**: `http://<awtrix-ip>/fullscreen`, made for an iframe on a Home Assistant dashboard (#29).
- **Icons from the AWTRIX Hub in the Icons tab.** Search the shared catalogue, install one with a click, and send your own the other way. Your browser does the fetching, the clock never reaches the internet.
- The browser tab carries the hostname, so several AWTRIX open at once are told apart (#18).
- Scripts can swallow a button press: return `true` from `on_button()`.
- `progress()` takes an x offset.
- Scripting tutorials on the documentation site.

**Changed**

- **`scriptLimit` and `scriptMaxBytes` are gone.** The free memory on the device decides how large a script may be and how many run alongside each other. Sending the two keys is ignored rather than refused, but a backup or an automation that still writes them needs looking at.

**Fixed**

- A notification with an empty `soundRtttl` was refused outright, so clients that send their whole schema - Home Assistant among them - got nothing at all (#27).
- An app pushed under a built-in name like `Temperature` was stored and listed, but the panel kept showing the built-in. The pushed app takes the name over now (#37).
- An icon that is a PNG under a `.jpg` name counted as drawn, leaving a black gap where the picture should be. The column goes back to the text and the log names the file (#23).
- One time zone the browser does not know ended the System page halfway, with no maintenance and no backup below MQTT (#25).
- The Icons tab showed a blank pane when the Hub catalogue is empty, and the framed icon editor was not told which Hub to publish to.
- Delete and duplicate in the icon editor were blank grey chips. Ships with the Hub, not with the firmware, so it is already fixed (#30).

---

**Which file do I need?**

| Your board | Update a running AWTRIX NG | First install over USB |
|---|---|---|
| Classic ESP32 - Ulanzi TC001, AWTRIX 2 conversions, most DIY | `firmware-awtrix-ng.bin` | `usb-awtrix-ng-<flash>.bin` |
| ESP32-S3, octal PSRAM (`N8R8`, `N16R8`) or no PSRAM | `firmware-awtrix-ng-s3-octal.bin` | `usb-awtrix-ng-s3-octal-<flash>.bin` |
| ESP32-S3, quad PSRAM (`N8R2`, `N16R2`, `N4R2`) | `firmware-awtrix-ng-s3-quad.bin` | `usb-awtrix-ng-s3-quad-<flash>.bin` |