# Flashing

---

## What you need

| | |
|---|---|
| **The board** | A classic ESP32 with 4 MB of flash - what the common 32×8 clocks use - or an ESP32-S3 board. |
| **A USB data cable** | Charge-only cables never show up as a serial port. |
| **A browser, or `esptool`** | Chrome, Edge or Opera on a desktop can do the whole flash themselves. Everything else needs `pip install esptool`. |

Building the firmware yourself instead needs PlatformIO, Python and Node.js - that path is
[Building from source](../advanced/building.md).

The commands below spell the esptool subcommands `read_flash`, `write_flash`, `erase_flash` and
`flash_id`. esptool 5 spells them with hyphens - `read-flash` and so on - and prints a warning for
the underscore form.

---

## 1. Flash it from your browser

Chrome, Edge or Opera on a desktop. Nothing to install:

<div id="awtrix-flasher"></div>
<script type="module" src="../../assets/awtrix-flasher.js"></script>

Pick your board's port when the browser asks. It detects the chip and the flash size, then writes
the newest release for it.

**Erase the whole flash first**: tick it the first time you put AWTRIX NG on a board. It clears
settings, Wi-Fi credentials, icons, melodies, palettes and scripts, and the device comes up as its
own access point - that is [First boot](first-boot.md).

Leave it off to update a board that already runs AWTRIX NG. Everything on it stays, and it comes
back on your Wi-Fi on the new version. Without a cable, the same update runs
[over the web UI](../guides/updating.md).

---

## 2. Back up what is on the chip

Flashing overwrites everything, including the firmware the device came with. Take the copy before
you flash, not after - this needs esptool even if the flash itself did not.

```bash
python -m esptool --chip esp32 --port COM5 --baud 921600 read_flash 0x0 0x400000 tc001-stock-4mb.bin
```

* `--port` is `COM5` on Windows, `/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-*` on macOS.
* It takes about a minute. If it stalls or errors, retry with `--baud 115200`.
* The file must be exactly 4,194,304 bytes. Anything shorter is a failed read, not a backup.

To go back to it later, write the same file to offset 0:

```bash
python -m esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 tc001-stock-4mb.bin
```

That restores the original firmware **and** everything that was stored on it, Wi-Fi credentials
included.

---

## 3. Pick your image

The manual route, for a browser without Web Serial or a board the flasher refuses.

Download from the [releases page](https://github.com/Blueforcer/awtrix-ng/releases):

| File | For |
|---|---|
| `usb-awtrix-ng-4mb.bin` | 4 MB ESP32 boards, the Ulanzi TC001 among them |
| `usb-awtrix-ng-8mb.bin`, `usb-awtrix-ng-16mb.bin` | ESP32 boards with more flash |
| `usb-awtrix-ng-s3-8mb.bin`, `usb-awtrix-ng-s3-16mb.bin` | ESP32-S3 boards |

Take the one matching your board's flash size. If you are unsure how much it has, ask the chip:

```bash
python -m esptool --port COM5 flash_id
```

The `firmware-awtrix-ng.bin` and `firmware-awtrix-ng-s3.bin` assets on the same page are **not** for
this - they are for [updating a device](../guides/updating.md) that already runs AWTRIX NG.

Releases up to v1.0.14 named these images `factory-awtrix-ng-4mb.bin` and so on. Same file, older
name.

---

## 4. Flash it with esptool

```bash
python -m esptool --chip esp32 --port COM5 --baud 460800 write_flash 0x0 usb-awtrix-ng-4mb.bin
```

Use `--chip esp32s3` for an S3 board. When it finishes it prints `Hash of data verified.`

!!! warning "Do not raise the baud rate to 921600"
    A write at 921600 aborts partway on the USB-serial bridge a TC001 uses. The chip has been
    erased by then, so it is left half-written and will not boot. Repeat the write at
    `--baud 460800`.

What this does to AWTRIX:

* **Settings and Wi-Fi credentials are erased.** It comes up as its own access point - continue at
  [First boot](first-boot.md).
* **Files you uploaded may or may not survive.** Icons, melodies, palettes and scripts live in a
  separate area that the image does not overwrite, but a firmware whose storage area sits
  elsewhere will not find them. Download anything you care about first, through the web UI, or
  list what's on the device with [`GET /api/v1/files`](../reference/http.md#get-apiv1files) and
  fetch each file from its `/ICONS/`, `/MELODIES/` or `/PALETTES/` path.

For a genuinely blank chip, erase before you write:

```bash
python -m esptool --chip esp32 --port COM5 erase_flash
```

---

## 5. Watch it boot

```bash
pio device monitor
```

Any serial monitor at **115200 baud** does. The line you are looking for is:

```text
boot: AWTRIX NG 1.0.12 on ESP32
```

Freshly flashed, AWTRIX has no Wi-Fi credentials, so after about 15 seconds it opens an access
point - that is [First boot](first-boot.md). On later boots, once it joins your network, it
scrolls `AWTRIX   <ip>` across the matrix in rainbow colours, so you can read the web UI's
address off the panel without a cable.

---

## 6. Set your wiring

A fresh flash always comes up on the ESP32 default pin map, the wiring of the common 32×8 clocks.
If that is your board, you are done - continue to [First boot](first-boot.md).

Anything else needs its GPIO map set once, in the web UI under **System → GPIO**. The map is
stored on AWTRIX and takes effect after a reboot. Send the **whole** map at once: changing one
pin on its own is usually rejected for colliding with another.

Presets for both boards, the pins each field accepts, and how to recover from a map you regret are
in [GPIO & boards](../reference/gpio.md).

---

## When it goes wrong

| Symptom | What to check |
|---|---|
| **No serial port found** | A charge-only USB cable, or a missing driver for your board's USB-to-serial bridge. The port has to exist before anything can talk to it. |
| **The write aborts partway** | Lower the baud rate - `--baud 115200` completes where 460800 does not. The chip is left unbootable until a write succeeds, so just repeat it. |
| **`A fatal error occurred: Failed to connect`** | Some boards need the boot button held while the tool connects. On a TC001 that is not required; on a DIY board it often is. |
| **`Unable to verify flash chip connection`**, and the reason differs every attempt | esptool speeds the port up partway through connecting, and not every USB-to-serial bridge survives that. Add `--no-stub` to the command: it holds one speed the whole way, which is slower but gets through. The browser flasher retries that way on its own. |
| **Boots, but the panel stays dark** | The wrong matrix pin for your hardware, or brightness at 0. |
| **Boots, but the hardware misbehaves** | Watch the serial log. A stored pin map the chip cannot use is announced there, and AWTRIX falls back to the defaults for its chip. |

---

## Related

* [First boot](first-boot.md) - the access point, and joining your Wi-Fi
* [Finding AWTRIX](discovery.md) - mDNS and UDP discovery
* [Updating firmware](../guides/updating.md) - reflashing a device that is already running
* [GPIO & boards](../reference/gpio.md) - the complete pin map reference
* [Building from source](../advanced/building.md) - build environments, partition tables, CI
