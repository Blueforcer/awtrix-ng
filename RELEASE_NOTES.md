**Added**
- Berry text takes colours per piece: `text()`, `text_width()`, `text_ink_width()` and both `scroll_text()` forms accept a list of `[text, colour]` pieces wherever they took a string - `text(1, 6, [["CPU ", 0x888888], ["42%", 0x00FF00]])`. The pieces measure, centre and scroll as one line, the same way a pushed app's `text` array does. Leave the colour off a `text()` call and it uses the device's `textColor`.
- ESP32-S3 boards with **quad** PSRAM (`N8R2`, `N16R2`, `N4R2`) get their own image, `firmware-awtrix-ng-s3-quad.bin` - with radio, where these boards previously ran without their PSRAM. The browser flasher detects which PSRAM a board has and picks by itself, and a device refuses an update built for the other PSRAM type instead of installing an image it cannot boot. That refusal arrives *with* this version, so for this one update pick the file by hand: the `-quad-` image on a board that is not quad does not boot and has to be put right over USB.
- The firmware update row under System → Maintenance names the version the device is running, so you can see what you are updating from (#13).

**Changed**
- The USB install images ship as one `usb-awtrix-ng.zip` instead of loose files, so the update images are what you see first on this page. Nothing changed inside: the [browser flasher](https://ang.blueforcer.de/getting-started/flashing/) picks from it by itself, and for esptool you unpack and take yours.
- Both ESP32-S3 files now name their PSRAM type. What was `firmware-awtrix-ng-s3.bin` is `firmware-awtrix-ng-s3-octal.bin`, and the USB image likewise - the quad counterpart makes the old name read as if it were the general case.
- The browser flasher asks in plain words what you want: **Fresh install** wipes the device, **Update AWTRIX NG** keeps your settings. That used to be one button and a tickbox nobody could read the meaning of.

**Fixed**
- The device now tells the router its name, so it shows up in the client list as `awtrixng-…` - or whatever hostname you set - instead of an unnamed device (#12).
- An icon edited in the Icon Editor kept its old picture in the icon list until the page was reloaded. The web UI no longer hands out stale copies of its own files after an update either.
- In `small`, accented letters like `Ä Ö Ü ä č ż` were drawn a row below the bare letter, in the row descenders use. They sit on the same baseline as `A O U a c z` now (#10).
- Uploading a USB install image to the update route left an ESP32-S3 boot-looping until it was flashed over USB again. Both chips now name the file to upload instead.
- The browser flasher gave up on some USB-to-serial bridges with "Unable to verify flash chip connection". It now makes a second attempt at a lower speed (#8).
- The browser flasher warned that the board was left unbootable even when it had never got as far as writing anything.

---

**Which file do I need?**

The easiest install and update is the [browser flasher](https://ang.blueforcer.de/getting-started/flashing/) - it detects your board and picks for you.

| Your board | Update a running AWTRIX NG | First install over USB |
|---|---|---|
| Classic ESP32 - Ulanzi TC001, AWTRIX 2 conversions, most DIY | `firmware-awtrix-ng.bin` | `usb-awtrix-ng-<flash>.bin` |
| ESP32-S3, octal PSRAM (`N8R8`, `N16R8`) or no PSRAM | `firmware-awtrix-ng-s3-octal.bin` | `usb-awtrix-ng-s3-octal-<flash>.bin` |
| ESP32-S3, quad PSRAM (`N8R2`, `N16R2`, `N4R2`) | `firmware-awtrix-ng-s3-quad.bin` | `usb-awtrix-ng-s3-quad-<flash>.bin` |

The `usb-*.bin` images are in `usb-awtrix-ng.zip`, `<flash>` is your board's flash size. Update files are uploaded on the device's update page; USB images are written with [esptool](https://ang.blueforcer.de/getting-started/flashing/#4-flash-it-with-esptool) to offset `0x0`.
