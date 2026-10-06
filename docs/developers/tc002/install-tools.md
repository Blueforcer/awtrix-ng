---
only: [tc002]
---

# TC002 install tools

`tc002_install.py` puts AWTRIX NG on a Ulanzi TC002 and keeps it there, over USB ADB (Android
Debug Bridge). It backs up the flash, takes the vendor app offline, writes the `res` partition
with the loader and `loop.ko`, writes releases into the release slot behind the `res` squashfs,
and builds web update packages. `build_bundle.sh` and `bundle.py` build the release bundle it
installs. Everything lives in
[`tools/tc002/install/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002/install).
How the clock boots, what the loader decides, what the daemon runs and how a web update works is
in the [TC002 overview](index.md).

End users do not need this page: they install with the [desktop installer](desktop-installer.md)
or the browser installer, as the [TC002 guide](../../getting-started/tc002.md) describes.

## Prerequisites

- **A host with Python 3, Node.js 22+ and `adb`.** `tc002_install.py` runs on Windows, Linux and macOS. It
  takes adb from `--adb`, `$ADB` or `PATH`.
  Image construction, vendor cleanup, verified `res` writes and Wi-Fi setup use the same
  JavaScript engine as the browser and desktop installers. Python handles packaging, release
  deployment, private backups and ADB transport. File contents and credentials travel over
  private process pipes; they are never passed as Node command-line arguments.
- **Node.js and the prepared image tools for `build-res`.** It uses the same JavaScript/WASM
  engine as the browser and desktop installers, directly on Windows, Linux and macOS. Prepare
  the pinned assets as described in [Browser image tools](browser-image.md#rebuilding).
- **WSL or Linux for release builds.** `build_bundle.sh` and `package` run on Linux. On
  Windows, `package` runs itself in WSL (distro `Ubuntu`, or the one named in
  `AWTRIX_WSL_DISTRO`).
- **Two cross toolchains**, set in the environment: `TC002_MUSL_PREFIX` (static musl, for the
  programs) and `TC002_GLIBC_PREFIX` (ARM GNU 9.2, for the loader and kernel modules).
  [`tools/tc002/toolchain/env.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/toolchain/env.sh)
  says where each comes from.
- **The prepared kernel tree** (`fetch-source.sh` and `prepare-tree.sh`, see
  [Kernel modules](kernel.md)).
- **squashfs-tools 4.6 or newer** in WSL, for the release image.
- **`qemu-arm`** (optional), to run the ARM contracts with `ctest --preset tc002`.
- **A private flash backup** of the clock (see [Back up the flash](#back-up-the-flash)). The
  first installation and `restore-stock` need it.

Every device command needs `--serial`. All TC002 units report the same USB serial
`0123456789ABCDEF`, so attach only one clock at a time; when adb lists the serial twice, the tool
stops. A network serial (`IP:PORT`) is left to adb.

## Commands

### Build the bundle

In WSL, with both toolchains set and the kernel tree prepared once:

```sh
export TC002_MUSL_PREFIX=BUILDROOT_WORK/output/host/bin/arm-linux-
export TC002_GLIBC_PREFIX=DIR/gcc-arm-9.2-2019.12-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
tools/tc002/kernel/fetch-source.sh && tools/tc002/kernel/prepare-tree.sh
bash tools/tc002/install/build_bundle.sh /mnt/c/PATH/TO/OUT
```

| Option | Effect |
|---|---|
| `--commit REV` | Build this commit (default `HEAD`), exported with `git archive`. Nothing outside the commit enters the release. |
| `--working-tree` | Build the checkout as it is, to try a change on a clock. The version ends in `-dirty` when anything differs from `HEAD`. |
| `--work DIR` | Work directory (default `~/.cache/awtrix-tc002/bundle`). |
| `--jobs N` | Parallel build jobs (default: all cores). |
| `--mcu-release DIR`, `--without-mcu` | Take the MCU extension from DIR instead of `tools/tc002/mcu/release`, or build without it. See [MCU update](mcu-update.md). |

`OUT/bundle` then holds the release. What is in it is described under
[Bundle contents](#bundle-contents).

### Connect

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL connect
```

A TC002 that never ran AWTRIX offers USB ADB only from about 2 s to 4.5 s after power-on. `connect`
catches that window and keeps ADB up (see [USB keeper](#usb-keeper)): switch the clock off and on
with its USB cable in the PC when the tool asks. Every other device command does the same first
when adb does not list the serial.

### Status

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL status [--bundle OUT/bundle]
```

`status` shows the mounted release and what runs, the update state, the start counter, the loader
markers and logs. With `--bundle` it also shows free space, the release slot and the `res`
hashes.

### Back up the flash

`backup` only reads; it never writes to the clock. The output directory must be new, private and
outside the checkout:

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL backup --output D:/PrivateTC002/backup-001
python tools/tc002/install/tc002_install.py verify-backup D:/PrivateTC002/backup-001
```

`backup` copies every partition listed in `/proc/mtd` through its MTD device with `adb pull`,
which the stock `adbd` serves byte for byte. It checks size and SHA-256 of each copy and writes
`profile.json`, `manifest.json` and `mtdN.bin`. `--timeout SECONDS` (1-3600, default 300) limits
each read. `verify-backup` checks a backup offline. The clock keeps running during a backup, so a
matching checksum does not prove a consistent filesystem snapshot.

!!! warning
    A raw flash backup contains passwords, keys and identifiers. Keep it private and encrypted;
    never commit or share it.

### First installation

```sh
python tools/tc002/install/tc002_install.py build-res --backup PRIVATE_BACKUP_DIR --bundle OUT/bundle --output PRIVATE_RES_DIR
python tools/tc002/install/tc002_install.py --serial SERIAL flash-loader --bundle OUT/bundle --res-dir PRIVATE_RES_DIR --yes
```

`build-res` builds the loader image of `res` from the backup and the bundle (see
[The res image](#the-res-image)); `--replace` overwrites an existing output, `--loader FILE`
uses another `libawtrix-loader.so` than the bundle's. The images contain the manufacturer's app
and stay outside the repository.

`flash-loader` first runs `secure-vendor` (see [Vendor app offline](#vendor-app-offline)), then
writes the loader image into `res` and the bundle's release into the release slot, and reboots
(unless `--no-reboot`). Without `--yes` it stops after the preflight. `--backup-root DIR` sets
where the vendor secrets backup goes. Later releases come with `deploy` or a web update.

### Deploy a release

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL deploy --bundle OUT/bundle
```

`deploy` writes the bundle's release image into the release slot of a clock whose `res` holds the
loader and `loop.ko`; without them it stops and asks for `flash-loader` first. A slot that already
holds this release is left alone. After the write, `deploy` reads every file of the release
through the mount and compares it with the bundle. How it works is under
[How deploy writes the slot](#how-deploy-writes-the-slot).

### Build a web update package

```sh
python tools/tc002/install/tc002_install.py package --bundle OUT/bundle --out OUT/awtrix-ng-tc002.awup
```

`package` wraps the bundle's `release.img` with
[`tools/update/package.py`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/update/package.py):
an `AWUPD003` container for the target `awtrix-ng:tc002` with the bundle's release name and
counter and the release image as payload. It runs on Linux, and through WSL on Windows. Python 3
is enough; no signing key or OpenSSL command is needed. It refuses a bundle without a counter or
release image, a dirty bundle (unless `--allow-dirty`), an existing output file and a package
above 8 MiB, and prints the package facts as JSON. SHA-256 checks the manifest and payload for
corruption; it does not prove who created a package.

The web UI of a TC002 takes the `.awup` file (at most 8 MiB). The clock accepts it when its format
and SHA-256 checks pass, the target is `awtrix-ng:tc002`, the payload is a release image that fits
the slot and the counter is higher than the accepted one; otherwise it answers with the reason and
changes nothing. The daemon then stops everything and hands the package to the running release's
flash helper, which writes the slot as `deploy` does and restarts the clock. The new release
confirms itself once its runtime has been healthy for 60 s; one that fails three starts leaves the
clock in rescue with USB RECOVERY. Until the confirmation, the clock refuses further uploads.

Clocks with an `AWUPD001` or `AWUPD002` updater need a one-time USB installation (`build-res`
and `flash-loader` with a current bundle). Current firmware accepts only `AWUPD003`.

### Recover a clock in USB RECOVERY

The loader shows **USB RECOVERY** when the release slot holds no complete release (a slot write
was cut short) or a release failed three starts. In that state the clock keeps USB ADB up, so:

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL status
python tools/tc002/install/tc002_install.py --serial SERIAL deploy --bundle OUT/bundle
```

`deploy` writes a complete release and clears the start counter and the rescue marker. Settings
and files in `/data/awtrix-ng` stay.

### Install an older version

Every deployed bundle is kept in `~/TC002-releases/<release>` (`AWTRIX_TC002_RELEASES` names
another directory). The five deployed last are kept, each with a `<release>.order` file; other
directories there stay untouched. A rollback is a deploy of one of them:

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL deploy --bundle ~/TC002-releases/RELEASE
```

A deploy records the bundle's counter as the accepted one, so a deploy of an older bundle lowers
it again. The web UI only installs packages with a higher counter than the accepted one, so an
older version always goes over USB.

### Restore the stock firmware

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL restore-stock --bundle OUT/bundle --res-dir PRIVATE_RES_DIR --yes [--remove-data]
```

`restore-stock` writes the backed-up `res` partition back byte for byte, which also erases the
release slot, so the vendor app starts at every boot. `--remove-data` also deletes
`/data/awtrix-ng`. `--accept-unknown-res` restores even when `res` holds content the tool does not
know. The changes of `secure-vendor` stay, so the vendor app stays offline until it is set up
again.

An interrupted flash leaves `flash-journal-<serial>.json` in the `res` directory; repeat the
command (or `restore-stock`) before rebooting.

### When USB ADB stops answering

When an adb step fails, the error names that step first; cleanups that failed after it follow,
indented. When adb reports the device offline or not found, or does not answer in time, the tool
adds what to do. The clock itself keeps running:

- **After `deploy`:** unplug and replug the USB cable, or restart the clock from the web UI
  (**System → Maintenance → Reboot**) or with `curl -X POST http://DEVICE_IP/api/v1/device/reboot`,
  then run `deploy` again. Meanwhile the clock helps itself: 90 s after the lost command's last
  token write, the web update lock is free and the loader starts the release in the slot, or
  shows USB RECOVERY when the slot write was cut short.
- **After `flash-loader` or `restore-stock`:** do not reboot, since `res` may be partly written.
  Replug the cable and repeat the command (or `restore-stock`).
- **After any other command:** replug the cable and run it again.

## How it works

### Bundle contents

`OUT/bundle` holds the release (the full file list is in the header of
[`build_bundle.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/install/build_bundle.sh)):

| Path | Content |
|---|---|
| `bin/` | The static musl programs: `awtrix-linux`, `awtrix-tc002d`, `awtrix-tc002-audio-pcm`, `awtrix-tc002-flash`, `udhcpc` (BusyBox), `dhcp-callback`, `wpa_supplicant` |
| `share/` | The web UI (`index.html.gz`), the CA bundle, the boot sound, the MCU extension and `licenses.txt.gz` |
| `lib/modules/` | `aic8800_bsp.ko`, `aic8800_fdrv.ko` ([Wi-Fi driver](wifi-driver.md)), `awtrix_pcm.ko` ([speaker driver](speaker-driver.md)) |
| `lib/libawtrix-loader.so`, `lib/modules/loop.ko` | Host-only: they go into `res`, not into the release |
| `manifest.json` | Size, SHA-256 and mode of every installed file; the release name (version plus a digest of the installed files); `counter` (the committer time of the release commit); `compilers`; `image` (size and SHA-256 of `release.img`); `hostOnly` |
| `release.img` | The release image: the squashfs of the installed release |

The AWTRIX NG files come from the CMake presets `tc002` and `tc002-loader`
([`CMakePresets.json`](https://github.com/Blueforcer/awtrix-ng/blob/main/CMakePresets.json)):
`cmake --install BUILD --component tc002` puts the programs, the loader, the web UI and the boot
sound into the bundle, and the script adds the rest. The script builds every component and stops
when one fails, and `bundle.py` refuses a bundle that lacks an installed one.

`release.img` is what `deploy` writes into the release slot and what update packages carry.
`bundle.py image` makes it with mksquashfs 4.6 or later and fixed options, so equal releases give
equal images with the same mksquashfs. `python3 tools/tc002/install/bundle.py release OUT/bundle
TREE` writes the installed release as a directory.

Build details:

- `awtrix-linux` links the static, trimmed [OpenSSL](openssl.md); the build fails if it links any
  other `libcrypto.a`. The programs are built with LTO, `-fno-pie`, function sections and a
  terminate handler that aborts.
- `bin/wpa_supplicant` has no debug output; `WPA_STDOUT_DEBUG=1` builds the `-d` variant for
  repair over USB ADB (see [wpa_supplicant](wpa-supplicant.md)).
- Source tarballs, the base64 header and the musl licence text come through `TC002_DL_DIR`
  (default `~/.cache/awtrix-tc002/downloads`) and must match their pinned SHA-256. A tarball
  placed there by hand is used when it matches.
- Outside a git checkout, set `SOURCE_DATE_EPOCH` to the release commit time.
- The MCU preparation runs through `bin/awtrix-linux --prepare-mcu`. The bundle ships only the
  AWTRIX extension, its manifest and licence in `share/mcu`, never a complete MCU image. See
  [MCU update](mcu-update.md).

### The res image

`res` is the 8 MiB flash partition that holds the vendor app. `build-res` builds the loader
image from the backed-up `res`: the stock content with three differences:

- the loader `lib/libawtrix-loader.so` and the `etc/EasyUI.cfg` entry that makes the stock GUI
  host start it;
- `awtrix-ng/loop.ko` (root-owned), through which the loader mounts the release;
- no vendor Bluetooth setup programs (`bin/hciattach`, `hciconfig`, `hcitool`, `gattserverbin`),
  so the vendor Bluetooth setup cannot run.

The image keeps the stock squashfs settings (xz, 128 KiB blocks, no BCJ filter) and the stock
mkfs time for every added entry, so the same inputs give the same image. `res-images.json`
records the release slot (header and image offsets, capacity) and `loop.ko`.
The shared engine verifies every entry after rebuilding the image, including data, permissions,
owners, timestamps, symbolic links and regular-file hard links. Changed configuration and removed
Bluetooth files must have no hard-link aliases. The host verifies output hashes and writes private
images only after the complete build and release-slot capacity checks succeed.

### The release slot

The release slot is the rest of `res` behind the squashfs. It starts at the first 64 KiB erase
block behind the squashfs: that block holds the slot header, and the release image starts in the
next one. With the stock vendor content the squashfs takes 2.57 MB and the slot 5.70 MB.
`build-res` stops when the slot cannot hold the bundle's release image and names both sizes. The
loader mounts the image read-only through `loop.ko` at `/tmp/awtrix-release`. Persistent state
stays in `/data/awtrix-ng`.

### Flash method

The flash helper `awtrix-tc002-flash` does every write, driven from the host. For `flash-loader`
and `restore-stock` it runs from `/tmp/awtrix-install`:

- the live partition must hash to the backup, or to an image built here;
- unchanged erase blocks are skipped, and every written block is read back;
- the whole range is verified before the reboot;
- the result is a JSON line the host checks.

The host transfers at most 1 MiB at a time and checks each transfer before the helper writes it.
The restore image is the backed-up partition byte for byte, written the same way. `flash-loader`
writes the bundle's release into the slot the way `deploy` does, then records the verified `res`
image in `/data/awtrix-ng/state/res-image.json`. A private local journal remains after an
interrupted `res` write so the command can resume. A failed installation never reboots the clock.

adb writes only to `/tmp` (tmpfs), never to `/data`. `secure-vendor`, `flash-loader` and
`restore-stock` pass their files through the private directory `/tmp/awtrix-stage`, which is
removed however the command ends.

### How deploy writes the slot

1. It reads the slot (`slot-info --verify`) and leaves a slot that already holds this release
   alone.
2. It stops the daemon through its control socket, and the stock GUI service only if that does
   not end it.
3. It copies the image to `/tmp/awtrix-install/release.img` and checks its SHA-256 there. It stops
   before the copy when `/tmp` has less free space than the image plus 2 MiB.
4. The helper's `write-slot` unmounts the release, checks the image once more, erases the slot's
   header block, writes and verifies the image (skipping blocks that already hold the right
   bytes) and writes the header last.
5. `deploy` clears the start counter, `/tmp/awtrix-loader.vendor` and `/tmp/awtrix-loader.rescue`,
   removes `/data/awtrix-ng/releases` and `/data/awtrix-ng/current` when present, and writes
   `state/update-state.json` in the format of the web update's state: the release
   `confirmed`, its counter accepted, the image's SHA-256 as payload digest.
6. It restarts the GUI service, so the loader mounts the new release, and compares every file with the
   bundle.

A bundle without a counter or release image is refused, and so is an image larger than the slot,
before anything changes. A failure before the helper erased anything leaves the release that was
in the slot, and `deploy` starts it again. After the erase, only a complete write leaves a
release: the clock shows USB RECOVERY and keeps USB ADB until `deploy` runs again. The daemon's
first start in a boot switches ADB over TCP off, and init restarts adbd for it; `deploy` waits out
that second without USB ADB.

**Lock.** While it works, `deploy` holds the lock of the web update (`/tmp/awtrix-update/lock`).
The helper keeps it while the token `/tmp/awtrix-install/lock.token` exists and was written within
the last 90 s, for at most 30 min. `deploy` rewrites the token before an adb command once 10 s
passed since the last write, and removes it before its final restart of the GUI service. A deploy that loses
USB therefore frees the lock within 90 s, and web uploads work again without a reboot. The token
holds `deploy` and the device's uptime at the write. The helper and the loader judge a token they
meet for the first time by that uptime and time every later rewrite on the monotonic clock, so a
wall clock that is still at 1970 and jumps when SNTP sets it neither ends the lock early nor
stretches it. A token without an uptime is judged by its modification time. When an upload or
install holds the lock, `deploy` stops before changing anything. The same token holds the loader
back (the deploy hold, see the [TC002 overview](index.md)).

### USB keeper

On a TC002 that never ran AWTRIX, the kernel's USB scan switches the port to host mode about
4.5 s after power-on, and ADB is gone until the next power-on. `connect`:

1. asks to switch the clock off and on again, with its USB cable in the PC;
2. checks `adb devices` every 150 ms, for up to 2 min, looking only at the serial given with
   `--serial`;
3. the moment adb lists that serial as `device`, starts a USB keeper on the clock with one
   `adb shell` command;
4. waits until adb lists the clock for 4 s in a row after the USB scan (10 s of uptime), then
   reports that it stays.

The keeper lives in RAM only and is gone after the next reboot. It survives the end of the adb
session and looks once per second of uptime: it reads
`/sys/bus/platform/devices/soc:usbotg/otg_role` (never the `usb_device`, `usb_host` or `usb_null`
nodes next to it, which act on read) and writes `usb_device` only when it reads `usb_host` or
`usb_null`, then not again for 10 s. It ends after 20 min. All three times are measured on
`/proc/uptime`: the stock BusyBox has no `sleep`, so the keeper spins on `/proc/uptime` between
its looks, but reads `otg_role` no more often and never runs longer. Once the AWTRIX loader runs
in this boot without the vendor app, the loader and the daemon keep the port themselves: the
keeper stops writing at once and ends when that has held for 2 s. A second `connect` reuses a
keeper that still runs.

### Vendor app offline

```sh
python tools/tc002/install/tc002_install.py --serial SERIAL secure-vendor [--backup-root DIR] [--dry-run]
```

The vendor app keeps Ulanzi cloud tokens and Wi-Fi credentials in `/data`. `secure-vendor` works
over USB only and:

1. copies the vendor files it changes into a new `BACKUP_ROOT/<date>-vendor-secrets-backup`
   directory (default `~/TC002-private`, refused inside the repository; a second backup on the
   same day gets `-2`), with a `manifest.json` of size, SHA-256, mode and owner per file and the
   previous property values;
2. removes the stored cloud tokens and Wi-Fi credentials; every other setting, the mode and the
   owner stay;
3. switches the vendor app's Wi-Fi and access point off.

Each file is replaced atomically: a hidden copy next to it is read back and compared, gets owner
and mode, then `sync` and `mv` over the original. Afterwards the files and properties are read
again. The output names keys, never values. A device already in this state is left alone and gets
no new backup. `--dry-run` only reports what would change. To undo it, push the backed-up files to
the paths in the backup's manifest, give them the recorded mode and owner, and set the properties
to the recorded values.

`flash-loader --yes` first stops the vendor app, its Bluetooth setup and the stock
`wpa_supplicant`, which could write credentials back from memory, then runs `secure-vendor`, stops
if it fails, and checks the result again after writing `res`. When `res` already holds the loader,
it stops only the stock `wpa_supplicant`, and the vendor app while the knob started it.

### Reset button

Holding the reset button for 5 s restores the Ulanzi firmware and wipes `/data`, AWTRIX NG and
everything in `/data` included. The restored vendor app can be older than the one the clock had.
The loader yields to a pending vendor update, so this works with the loader installed.

## Browser installation package

The browser installer lives in
[`docs/assets/tc002/`](https://github.com/Blueforcer/awtrix-ng/tree/main/docs/assets/tc002). It
uses the same flash helper, loader, `loop.ko` and release image as the command-line installer, and
its RAM-only USB keeper catches the stock boot window. The `res` image is built in the browser by
the [browser image tools](browser-image.md). Browser tests use synthetic images and fake USB
endpoints and never contact a clock.

After building a clean release bundle, create its corresponding-source archive with
`source_package.py` (see its
[README](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/install/source_package_README.md)),
then build the browser package:

```sh
python3 tools/tc002/install/browser_package.py OUT/bundle OUT/usb-awtrix-ng-tc002.zip \
  --license LICENSE.md --notices THIRD-PARTY-NOTICES.md \
  --source-url https://github.com/Blueforcer/awtrix-ng/releases/download/TAG/tc002-sources.tar.gz \
  --source-sha256 SHA256_OF_THAT_ARCHIVE
```

`usb-awtrix-ng-tc002.zip` and that exact source archive belong on the same release, with the
actual release tag and archive digest. `tc002-update.yml` builds both on every `v*` tag and the
release job attaches them. The ZIP holds no manufacturer partition or device data:
the bundle's manifest, the release image, the flash helper, the loader and `loop.ko`, notices and
integrity metadata. `--allow-dirty` is only for local tests; the browser installer rejects dirty
packages.

The Docs workflow mirrors the published ZIP into `docs/firmware/tc002/` and writes its size and
SHA-256 to `index.json`. A release without that ZIP leaves the installer without an automatic
download. The first installation writes the release into the slot; later releases come as
`.awup` updates. Wi-Fi setup uses the TC002 access point at `192.168.4.1`; USB Wi-Fi setup is
optional.

## Tests

The CMake build runs these tools' tests as `tc002-install-tools`, with a host build of
`awtrix-tc002-flash` for the lock tests and `awtrix-update-verify` for the package tests. The USB
keeper tests run the keeper in `sh` against a stand-in `otg_role`.

```sh
ctest --test-dir BUILD_DIR -R 'tc002-(install-tools|loader|flasher)' --output-on-failure
```

The loader host tests use a 3 s staleness window for the deploy hold. Set
`AWTRIX_TC002_RES_BACKUP` to a private backup directory to include the real `res` dump, and
`AWTRIX_TC002_BUNDLE` to a bundle as well, to check that the bundle's release image fits the slot
behind the vendor content. The loader and flasher tests live in
[`tests/tc002/loader`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/tc002/loader) and
[`tests/tc002/flasher`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/tc002/flasher).

## Related

- [TC002 overview](index.md)
- [TC002 tools](tools.md)
- [Desktop installer](desktop-installer.md)
- [Browser image tools](browser-image.md)
- [MCU update](mcu-update.md)
- [Source package README](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/install/source_package_README.md)
