**Which file do I download?**
- Updating a device that already runs AWTRIX NG: `firmware-awtrix-ng.bin` for a Ulanzi TC001 or any other classic ESP32, `firmware-awtrix-ng-s3.bin` for an ESP32-S3 board. Upload it under System → Maintenance.
- First install over USB: the `usb-*.bin` matching your board's chip and flash size - a TC001 takes `usb-awtrix-ng-4mb.bin`. These are **not** for the update route.

**Added**
- Scripts can draw a line in several colors. `text()`, `scroll_text()` and the two width functions take a list of `[text, color]` pieces wherever they took plain text. A piece without a color follows the call, and `text()` may leave its color out to use the device text color.

**Changed**
- The USB install images are called `usb-*.bin` now, not `factory-*.bin`. Too many people took `factory-awtrix-ng-4mb.bin` for the update file of a 4 MB board - it was the only asset naming a flash size. The update images keep their names.
- The browser flasher asks in plain words what you want: **Fresh install** wipes the device, **Update AWTRIX NG** keeps your settings. That used to be one button and a tickbox nobody could read the meaning of.

**Fixed**
- The device now tells the router its name, so it shows up in the client list as `awtrixng-…` - or whatever hostname you set - instead of an unnamed device (#12).
- An icon edited in the Icon Editor kept its old picture in the icon list until the page was reloaded. The web UI no longer hands out stale copies of its own files after an update either.
- In `small`, accented letters like `Ä Ö Ü ä č ż` were drawn a row below the bare letter, in the row descenders use. They sit on the same baseline as `A O U a c z` now (#10).
- Uploading a USB install image to the update route left an ESP32-S3 boot-looping until it was flashed over USB again. Both chips now name the file to upload instead.
- The browser flasher gave up on some USB-to-serial bridges with "Unable to verify flash chip connection". It now makes a second attempt at a lower speed (#8).
- The browser flasher warned that the board was left unbootable even when it had never got as far as writing anything.
