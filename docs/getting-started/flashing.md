---
only: [esp32, esp32-s3]
---

# Install AWTRIX NG

<!-- only esp32 -->
This page puts AWTRIX NG on a Ulanzi TC001, an AWTRIX 2 or an ESP32 DIY board over USB.
<!-- /only -->
<!-- only esp32-s3 -->
This page puts AWTRIX NG on an ESP32-S3 board over USB.
<!-- /only -->

## What you need

| | |
|---|---|
<!-- only esp32 -->
| **The board** | A classic ESP32 with 4, 8 or 16 MB of flash. The Ulanzi TC001 and the common 32×8 clocks have 4 MB. |
<!-- /only -->
<!-- only esp32-s3 -->
| **The board** | An ESP32-S3 with 8 or 16 MB of flash, with or without PSRAM. |
<!-- /only -->
| **A USB data cable** | A charge-only cable does not show up as a serial port. |
| **A browser** | Chrome, Edge or Opera on a desktop computer. Other browsers need `esptool` instead (`pip install esptool`). |

## Install from your browser

1. Connect the clock to your computer with the USB cable.
2. Press **Fresh install** for a board that does not run AWTRIX NG yet, or **Update AWTRIX NG** for
   one that does.
3. Choose your board's port when the browser asks.
4. Wait until the flasher says **Done**.

<div id="awtrix-flasher"></div>
<script type="module" src="../../assets/awtrix-flasher.js"></script>

The flasher detects the chip and the flash size, and writes the newest release for your board.
<!-- only esp32-s3 -->
It takes the `-quad-` image only when the chip reports quad PSRAM inside it. Every other board gets
the `-octal-` image. Then check **PSRAM** as described in
[Which of the two S3 images](#which-of-the-two-s3-images).
<!-- /only -->

**Fresh install** is for a board that does not run AWTRIX NG yet. It clears settings, Wi-Fi
credentials, icons, melodies, palettes and scripts. The clock then opens its own setup hotspot:
continue at [Connect to Wi-Fi](first-boot.md).

**Update AWTRIX NG** is for a board that already runs it. Everything on it stays, and it comes
back on your Wi-Fi with the new version. You can also update without a cable
[in the web UI](../guides/updating.md).

!!! tip "Keep a copy of the original firmware"
    Installing overwrites the firmware your clock came with. If you may want it back later, make a
    [backup](#back-up-the-original-firmware) first.

## Set your wiring

<!-- only esp32 -->
A fresh install uses the pin map of the Ulanzi TC001, the default of the common 32×8 clocks. If
you have one of those, there is nothing to do. Continue at [Connect to Wi-Fi](first-boot.md).

An AWTRIX 2 or a DIY board with other wiring needs its pin map set once:
<!-- /only -->
<!-- only esp32-s3 -->
A fresh install uses the [standard pinout](../advanced/diy-build.md#3-the-standard-pinout). If you
wired your board that way, there is nothing to do. Continue at [Connect to Wi-Fi](first-boot.md).

A board with other wiring needs its pin map set once:
<!-- /only -->

1. Open the web UI and go to **System → GPIO**.
2. Enter the **whole** pin map at once. Changing one pin on its own is usually rejected because it
   collides with another pin.
3. Save and restart the clock. The new map takes effect after the restart.

<!-- only esp32 -->
Presets for common boards, the pins each field accepts, and how to undo a bad map are in
[GPIO & boards](../reference/gpio.md).
<!-- /only -->
<!-- only esp32-s3 -->
The pins each field accepts, and how to undo a bad map, are in [GPIO & boards](../reference/gpio.md).
<!-- /only -->

## Install with esptool

Use this method if your browser has no Web Serial support or the browser flasher refuses your board.
`esptool` is a command-line tool: install it with `pip install esptool`.

!!! note "Command names in esptool 5"
    The commands below use `read_flash`, `write_flash`, `erase_flash` and `flash_id`. esptool 5
    also accepts `read-flash`, `write-flash` and so on, and prints a warning for the underscore
    form.

In all commands, `--port` is `COM5` on Windows, `/dev/ttyUSB0` on Linux and
`/dev/cu.usbserial-*` on macOS. Use your own port.

### Back up the original firmware

Make the backup **before** you install AWTRIX NG. This needs esptool, even if you install from the
browser.

<!-- only esp32 -->
```bash
python -m esptool --chip esp32 --port COM5 --baud 921600 read_flash 0x0 0x400000 tc001-stock-4mb.bin
```

* It takes about a minute. If it stops or fails, try again with `--baud 115200`.
* The file must be exactly 4,194,304 bytes. A smaller file is a failed read, not a backup.
* This is for a 4 MB board such as the TC001. For 8 MB read `0x800000` bytes, for 16 MB
  `0x1000000`.

To restore it later, write the same file to offset 0:

```bash
python -m esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 tc001-stock-4mb.bin
```
<!-- /only -->
<!-- only esp32-s3 -->
```bash
python -m esptool --chip esp32s3 --port COM5 --baud 921600 read_flash 0x0 0x1000000 stock-16mb.bin
```

* This is for a 16 MB board. For 8 MB, read `0x800000` bytes instead.
* It takes a few minutes. If it stops or fails, try again with `--baud 115200`.
* The file must be exactly as large as the flash: 16,777,216 bytes for 16 MB, 8,388,608 bytes for
  8 MB. A smaller file is a failed read, not a backup.

To restore it later, write the same file to offset 0:

```bash
python -m esptool --chip esp32s3 --port COM5 --baud 460800 write_flash 0x0 stock-16mb.bin
```
<!-- /only -->

This restores the original firmware **and** everything stored on it, Wi-Fi credentials included.

### Pick your image

Download `usb-awtrix-ng.zip` from the
[releases page](https://github.com/Blueforcer/awtrix-ng/releases) and unpack it. It holds one
image per board and flash size:

| File | For |
|---|---|
<!-- only esp32 -->
| `usb-awtrix-ng-4mb.bin` | 4 MB ESP32 boards, including the Ulanzi TC001 |
| `usb-awtrix-ng-8mb.bin`, `usb-awtrix-ng-16mb.bin` | ESP32 boards with more flash |
<!-- /only -->
<!-- only esp32-s3 -->
| `usb-awtrix-ng-s3-octal-8mb.bin`, `usb-awtrix-ng-s3-octal-16mb.bin` | ESP32-S3 boards. Start with this one |
| `usb-awtrix-ng-s3-quad-8mb.bin`, `usb-awtrix-ng-s3-quad-16mb.bin` | ESP32-S3 boards where the octal image finds no PSRAM |
<!-- /only -->

Take the image that matches your board's flash size. If you do not know the flash size, ask the
chip:

```bash
python -m esptool --port COM5 flash_id
```

The `firmware-awtrix-ng*.bin` files on the same page are **not** for a USB install. They are for
[updating a clock](../guides/updating.md) that already runs AWTRIX NG.

<!-- only esp32-s3 -->
### Which of the two S3 images

PSRAM is extra memory on many ESP32-S3 boards. It is connected in one of two ways, quad or octal,
and the image must match. The markings on the board (`R8`, `R2`) do not tell you reliably which
one your board uses.

So do not guess:

1. Write the `-octal-` image first. It starts on every S3, with or without PSRAM.
2. Open the clock's web UI and look at **PSRAM**.
3. Compare with this table:

| The clock shows | What to do |
|---|---|
| A size, usually 8 MB | Nothing. You have the right image. |
| `none`, and your board has no PSRAM | Nothing. You have the right image. Radio needs PSRAM, so this board has no radio. |
| `none`, but your board is sold with PSRAM | Your board is wired quad. Write the `-quad-` image. |

!!! warning "Always try octal first"
    The `-quad-` image does not start at all on a board that is not quad. The display stays dark
    until you write the other image over USB. The `-octal-` image always starts.

<!-- /only -->
### Write the image

<!-- only esp32 -->
```bash
python -m esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 usb-awtrix-ng-4mb.bin
```
<!-- /only -->
<!-- only esp32-s3 -->
```bash
python -m esptool --chip esp32s3 --port COM5 --baud 460800 write_flash 0x0 usb-awtrix-ng-s3-octal-16mb.bin
```
<!-- /only -->

When the write is done, esptool prints `Hash of data verified.` If it stops earlier, see
[When it goes wrong](#when-it-goes-wrong).

<!-- only esp32 -->
!!! warning "Do not use 921600 baud for writing"
    On a TC001 a write at 921600 baud stops partway. The chip is then half-written and does not
    start. Repeat the write with `--baud 460800`.

<!-- /only -->
What happens to your data:

* **Settings and Wi-Fi credentials are erased.** The clock opens its setup hotspot. Continue at
  [Connect to Wi-Fi](first-boot.md).
* **Your files may or may not survive.** Icons, melodies, palettes and scripts are stored in a
  separate area that the image does not overwrite. A different firmware may not find them there.
  Download anything you want to keep first in the web UI, or list the files with
  [`GET /api/v1/files`](../reference/http.md#get-apiv1files) and download each one from its
  `/ICONS/`, `/MELODIES/` or `/PALETTES/` path.

To start from a completely empty chip, erase it before you write:

<!-- only esp32 -->
```bash
python -m esptool --chip esp32 --port COM5 erase_flash
```
<!-- /only -->
<!-- only esp32-s3 -->
```bash
python -m esptool --chip esp32s3 --port COM5 erase_flash
```
<!-- /only -->

### Watch it start

Open any serial monitor at **115200 baud**. After a successful install you see a line like:

<!-- only esp32 -->
```text
boot: AWTRIX NG 1.0.12 on ESP32
```
<!-- /only -->
<!-- only esp32-s3 -->
```text
boot: AWTRIX NG 1.0.12 on ESP32-S3
```
<!-- /only -->

A freshly installed clock has no Wi-Fi credentials and opens its setup hotspot. See
[Connect to Wi-Fi](first-boot.md). Once it is on your network, it shows its IP address on the
display every time it starts.

## When it goes wrong

| Symptom | What to check |
|---|---|
| **No serial port found** | The USB cable may be charge-only, or the driver for your board's USB-to-serial chip is missing. |
| **The write stops partway** | Lower the baud rate: `--baud 115200` works where 460800 does not. The clock does not start until a write succeeds, so just repeat it. |
<!-- only esp32 -->
| **`A fatal error occurred: Failed to connect`** | Hold the boot button while esptool connects. A TC001 does not need this. DIY boards often do. |
<!-- /only -->
<!-- only esp32-s3 -->
| **`A fatal error occurred: Failed to connect`** | Hold the boot button while esptool connects. Many boards need this. |
<!-- /only -->
| **`Unable to verify flash chip connection`**, with a different reason each time | Add `--no-stub` to the command. It is slower but works with more USB-to-serial chips. The browser flasher retries this way by itself. |
| **It starts, but the display stays dark** | The **LED data** pin does not match your hardware, or the brightness is 0. See [Set your wiring](#set-your-wiring). |
| **It starts, but the hardware behaves strangely** | Watch the serial log. If the saved pin map cannot be used on this chip, the log says so and the clock uses the default pins for its chip. |

## Related

* [Connect to Wi-Fi](first-boot.md) - the setup hotspot, and joining your network
* [Find your clock](discovery.md) - get its address
* [Updating firmware](../guides/updating.md) - update a clock that already runs AWTRIX NG
* [GPIO & boards](../reference/gpio.md) - all pin settings
* [Building from source](../developers/building.md) - for developers who build the firmware themselves
