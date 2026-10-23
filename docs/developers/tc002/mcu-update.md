---
only: [tc002]
---

# TC002 MCU firmware updates

The TC002's MCU (microcontroller) drives the panel scan, reads the battery and USB power, and
samples the microphone. AWTRIX NG extends the stock MCU firmware `V1.0.17` with its own code for
microphone capture. This repository holds the released extension in
[`tools/tc002/mcu/release`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/mcu/release)
and the installer: the daemon's `McuService` (UART owner and flasher), the
preparation mode `awtrix-linux --prepare-mcu`
([`src/platform/tc002/mcu/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/mcu))
and the pin of the tested artifact in
[`tools/tc002/mcu/release.json`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/mcu/release.json).
The pinned build is 13. The MCU never downloads anything by itself; every TC002 bundle carries
exactly the MCU artifact selected for that Linux release.

## How it works

1. A bundle carries only our extension, not the vendor firmware.
2. When the running MCU needs an update, the daemon starts `awtrix-linux --prepare-mcu`, which
   downloads the vendor original once, checks its SHA-256 and builds the complete image locally.
3. After the runtime has been healthy and USB power is present, `McuService` records the attempt
   and transfers the image over the UART.
4. The MCU reboots the whole clock. The next boot reads the new identity and marks the attempt
   verified.

### What a release carries

The MCU artifact in `tools/tc002/mcu/release` contains exactly `extension.bin`, `manifest.json`
and `LICENSE.txt`. Manifest schema 2
names the fixed `tc002-pcm-v1` recipe, the expected original SHA-256, the extension SHA-256 and
identity, and the exact size and SHA-256 of the locally reconstructed POT image.

Replace the three files and the hashes in
[`tools/tc002/mcu/release.json`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/mcu/release.json)
together, only after testing a new artifact, and never reuse a published MCU version number for
different code. `build_bundle.sh` takes the extension from the built commit;
`--mcu-release DIR` tries another directory first. The importer verifies all three file hashes and refuses extra files, including a leftover complete
firmware image. `--without-mcu` omits the patch; the running MCU firmware keeps working and is
still queried. Browser packages accept only the complete patch file set; older packages without
MCU support remain installable.

The three files are installed in `share/mcu` and covered by the release manifest. No original,
modified full image or binary delta containing vendor instructions is in a release. The extension
is MIT-licensed; the Linux application and the patch helper keep the project licence.

### Local preparation

Only when the version policy calls for an update does the daemon start
`bin/awtrix-linux --prepare-mcu` as a separate child. This mode runs the patcher before any
runtime initialisation and reuses the runtime's TLS libraries, so the release needs no second
static HTTPS binary. The helper has no UART access and cannot flash. Normal UART queries continue
meanwhile.

The helper uses the release's CA bundle, requires certificate and hostname verification, follows
no redirects and accepts at most 35,840 bytes from one fixed download URL of the manufacturer
(`src/platform/tc002/mcu/McuPrepare.cpp`).

The only accepted original SHA-256 is:

```text
ae9737b190501c8ac9500cae31d9fb496f33f55b8e87413dee8f4b90a9c28bbd
```

The verified original is written atomically with mode 0600 to
`/data/awtrix-ng/state/mcu/base-v1.0.17.fot` and kept across Linux and MCU updates. With that file
present no download happens; its hash is checked again on every use. There is no fallback to a
mirror, another vendor version or another hash. A failed download postpones the MCU update and is
retried after 60 s while AWTRIX runs normally; a hash mismatch blocks the update. The first
download needs a network connection and a clock time valid for TLS. Root maintenance may also
place the exact original at the cache path.

The helper builds the image from the local original and our extension and requires the final POT
to have the release's exact SHA-256 before it saves `prepared.pot` in the private cache.
The daemon checks the finished image again and holds those bytes in memory for the transfer. The
recipe's compression parameters and entry offsets must match the MCU builder; any difference fails
closed.

Preparation has a 45 s limit in the child and a 60 s deadline in the daemon. Shutdown ends
preparation and reaps the child. No attempt journal is written until preparation succeeds and USB
power has been queried again. A matching or newer compatible MCU needs neither preparation nor a
network connection.

For a bench preparation without flashing:

```sh
awtrix-linux --prepare-mcu PATCH_DIRECTORY PRIVATE_CACHE CA_BUNDLE
awtrix-linux --prepare-mcu PATCH_DIRECTORY PRIVATE_CACHE CA_BUNDLE --offline
```

### Boot policy

1. `McuService`, the only UART owner, queries the firmware version and the AWTRIX build
   identity (`0x08`). The identity is a 16-byte payload: `AWMC`, ABI u8, features u8, family u16 LE,
   version u32 LE, build tag u32 LE.
2. A matching build is left alone. A newer build with the same ABI and family and the required
   features is kept, also after a Linux rollback. Equal versions with a different tag, an
   incompatible identity or malformed replies block installation.
3. An MCU without `0x08` support is eligible only after bounded probe attempts and an exact
   `V1.0.17` vendor answer. The identity is a compatibility contract, not a hardware attestation.
4. The service waits for a healthy runtime, the confirmation of any pending Linux update, no Linux
   update hand-off or shutdown, and a fresh USB-power report of `1`.
5. After preparation and a fresh power check, before the handshake, the service writes and fsyncs
   `/data/awtrix-ng/state/mcu-update.json` with phase `attempt`, the target identity, SHA-256 and
   the number of attempts in a row for that image (`attempts`). A failed transfer or verification
   adds its reason as `error`. An unwritable or corrupt journal prevents flashing.
6. The non-blocking transfer takes exclusive use of the UART. Battery queries and microphone
   capture pause. A Linux update hand-off is refused while it runs; an orderly shutdown waits for the
   bounded transfer to finish. After a failed transfer the UART stays silent until the next boot.
   The last USB and battery readings then go stale after 30 s, and the runtime is told the USB
   supply is unknown (`usbKnown: false`). It keeps showing the last report.
7. The last ACK confirms the transfer only. The MCU updater normally reboots the whole TC002. On the
   next boot, reading the recorded identity changes the journal to `verified`. If Linux stays up,
   the service does the same check after a short delay.

An attempt without confirmation is repeated on a later boot, at most one attempt per boot and three
in a row for one image. A repeat needs an MCU that answers the boot probe with an eligible build
other than the target, so the earlier attempt left the running firmware as it was. A journal
without an `attempts` field counts as one attempt. After the third attempt the journal blocks
automatic flashing of that image. Another image starts counting at one, so every Linux release
with a different MCU image allows three more attempts. There is no automatic downgrade or MCU
rollback, and a normal boot never clears the journal.

Every change of the update status is logged once as `mcu: update: <status>`, for example
`retry 2 of 3 after an unconfirmed attempt: MCU transfer timeout`,
`installing build 13 (38400 bytes), attempt 2 of 3` or
`MCU update unconfirmed after 3 attempts (last: …); automatic retry blocked`. A failure without a
recorded reason, such as a power loss mid-transfer, shows no reason. `awtrix-tc002d ctl status`
includes `mcu.firmware` with the installed and target version, tag, status, attempts, preparation
activity, bytes acknowledged and whether verification is pending.

## Transfer

`McuUpgrade` sends the image in blocks over the UART (1.5 Mbaud, raw 8N1) and checks an
acknowledgement for each one. A rejected block is sent again at most once; checksum or sequence
errors and timeouts abort without a blind retransmission, and the whole transfer has a 120 s
deadline. Short writes and `EAGAIN` on the UART keep the unsent bytes.

The MCU updater remembers how far an interrupted transfer got. When a later request carries the
same file CRC, its configuration reply offers that block position instead of `0`, and it keeps
offering it for every attempt with that image. `McuUpgrade` continues from the offered block and
logs `transfer continued an interrupted one from byte N`. An offer past the end of the image or off
a block boundary aborts the transfer before any data is sent. The preparation child never
opens the UART, and the daemon never runs a shell flasher or starts a competing serial owner.
Source: `McuFirmware`, `McuUpgrade`, `McuService` in
[`src/platform/tc002/daemon/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/daemon).

## Tests and bench tools

Host tests (`tests/tc002/daemon/test_mcufirmware.cpp` and neighbours,
`tools/tc002/mcu/test_bundle.py`, `test_prepare.py`) cover extension and image integrity, refusal of
full-image manifests, missing or corrupt originals, helper cancellation and download failure, the
boot version policy, the durable attempt and verification, serial fragmentation, `EAGAIN`, packet
and block geometry, retry sequence numbers, a final `ff`, refusal, resume and invalid resume
offers, the attempt count and its limit, errors and timeouts, health and USB gating, and deferred
shutdown.

## Limits

- Flashing is not power-loss safe. The MCU has a single bank; the attempt journal prevents boot
  loops but cannot make the flash write atomic. Independent hardware recovery of the MCU has not
  been qualified.
- The identity check proves compatibility, not authenticity.

## Publication rights

This design avoids redistributing the manufacturer's complete MCU image. It is not a legal
determination that local modification is permitted. The MIT grant covers
only our contributions. Never publish the private cache, generated POT images, stock partition
backups or modified `res` images. The AIC driver licence review and the GPL/LGPL source-delivery
duties of the Linux bundle are separate release items; see the third-party notices.

## Related

- [TC002 developer guide](index.md)
- [Microphone](microphone.md), [Audio gate](audio-gate.md), [Home Assistant Voice](ha-voice.md)
- [Install tools](install-tools.md)
- User docs: [Microphone controller updates](../../guides/updating.md#mcu-updates)
