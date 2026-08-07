**Added**
- Berry text takes colours per piece: `text()`, `text_width()`, `text_ink_width()` and both `scroll_text()` forms accept a list of `[text, colour]` pieces wherever they took a string - `text(1, 6, [["CPU ", 0x888888], ["42%", 0x00FF00]])`. The pieces measure, centre and scroll as one line, the same way a pushed app's `text` array does. Leave the colour off a `text()` call and it uses the device's `textColor`.
- The firmware update row under System → Maintenance names the version the device is running, so you can see what you are updating from (#13).

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
