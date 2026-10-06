---
only: [tc002]
---

# TC002 developer guide

AWTRIX NG runs on the Ulanzi TC002 as a Linux program on the manufacturer's kernel. This page is
the entry point for work on that port: how the clock boots into AWTRIX NG, how the release is
stored and mounted, what the supervisor daemon does, how a release is built, installed, updated
from the web UI and recovered, and how all of it coexists with the Ulanzi firmware. The TC002 code
lives in [`src/platform/tc002/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002),
its tests in [`tests/tc002/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/tc002) and
its host tooling in [`tools/tc002/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tools/tc002)
([tools overview](tools.md)). End users read [Ulanzi TC002](../../getting-started/tc002.md).

Private material never enters the repository: flash backups of a clock, the stock `res` image and
anything built from it, captures and measurement evidence. Keep it outside the checkout; the tools
take its location as an argument.

## At a glance

- **One partition changes.** AWTRIX NG keeps the stock boot loader, kernel, root filesystem and
  SoC modules. It rewrites only the `res` partition: its squashfs gets a loader library and
  `loop.ko`, and a *release slot* behind that squashfs holds the AWTRIX NG release.
- **Loader.** The stock GUI host loads `libawtrix-loader.so` at every start. The loader
  checks the release slot, mounts the release read-only at `/tmp/awtrix-release` and execs the
  supervisor daemon.
- **Daemon.** `awtrix-tc002d` owns the hardware, Wi-Fi, IP, the MCU (microcontroller) link,
  Bluetooth and updates, and starts the runtime `awtrix-linux --board tc002`, the same engine as the
  ESP32 firmware.
- **Data.** `/data/awtrix-ng` holds settings and user data only. A release never lives there.
- **Updates.** A web update uploads an `.awup` package; the daemon hands it to the flash helper,
  which rewrites the release slot and reboots. The new release is confirmed after 60 s of health.
- **Recovery.** There is one release. When it is missing, damaged or fails three starts in a row,
  the loader shows `USB RECOVERY` on the panel and keeps USB ADB (Android Debug Bridge) open, so a
  USB `deploy` can write a working release.
- **Ulanzi firmware.** The vendor app stays in `res` and starts only when the knob is held at
  power-on or a vendor update is pending. `restore-stock` puts the original `res` back.

## Architecture

### The pieces

| Piece | On the clock | Source | Role |
|---|---|---|---|
| `libawtrix-loader.so` | `/res/lib` | `src/platform/tc002/loader/` (glibc) | The plugin the stock GUI host loads at every start; decides what starts and mounts the release ([Loader decisions](#loader-decisions)). |
| `loop.ko` | `/res/awtrix-ng` | `tools/tc002/kernel/build-loop.sh` | The kernel's loop block driver, which the stock kernel lacks; the loader mounts the release through it ([Kernel modules](kernel.md)). |
| release | the release slot in `res`, mounted read-only at `/tmp/awtrix-release` | a bundle's `release.img` | The one release: a squashfs of its programs, modules, web UI and `manifest.json`. |
| `awtrix-tc002d` | release `bin/` | `src/platform/tc002/daemon/` | The supervisor: hardware, network, the runtime and the update hand-off ([The supervisor](#the-supervisor-awtrix-tc002d)). |
| `awtrix-linux` | release `bin/` | `src/platform/linux/`, `src/platform/tc002/runtime/` and the shared engine | The AWTRIX NG runtime with `--board tc002` ([The runtime](#the-runtime-on-the-tc002)). |
| `awtrix-tc002-audio-pcm` | release `bin/` | `src/platform/tc002/audio/pcm/`, `audio/helper/` | The speaker helper above the `awtrix_pcm` driver ([Speaker driver](speaker-driver.md)). |
| `udhcpc`, `dhcp-callback`, `wpa_supplicant` | release `bin/` | `tools/tc002/busybox/`, `src/platform/tc002/daemon/ip/DhcpCallbackMain.cpp`, `tools/tc002/wpa_supplicant/` | The DHCP client and, for the setup access point, the DHCP server (a BusyBox with only those two applets), its callback into the daemon and the Wi-Fi supplicant ([wpa_supplicant](wpa-supplicant.md)). |
| `aic8800_bsp.ko`, `aic8800_fdrv.ko`, `awtrix_pcm.ko` | release `lib/modules/` | `tools/tc002/kernel/`, `src/platform/tc002/kmod/` | The Wi-Fi driver rebuilt from the AICSemi GPL source with our patches ([Wi-Fi driver](wifi-driver.md)), and the speaker driver (GPL) on the audio exports of the stock `mhal.ko`. |
| `awtrix-tc002-flash` | release `bin/`; the installer pushes a copy to `/tmp` | `src/platform/tc002/flasher/` | Writes and verifies the `res` partition and the release slot for `flash-loader`, `deploy`, `restore-stock` and the web update. |

Every program of a release is a static musl binary for armv7 hard-float; only the loader links the
stock glibc, because it runs inside the stock GUI host. The daemon runs only the release's own
programs and modules and never falls back to stock ones: a release that lacks one logs
`broken release, not starting` and exits, and the loader's start counter takes it from there.

### Boot chain

```
power-on -> stock boot loader, kernel and init                                       [stock]
  stock GUI service  loads the library named in /res/etc/EasyUI.cfg                     [stock]
    /res/lib/libawtrix-loader.so                                                        [ours]
      -> checks the release slot, loads /res/awtrix-ng/loop.ko, mounts the release
         read-only at /tmp/awtrix-release
      -> exec /tmp/awtrix-release/bin/awtrix-tc002d --root /tmp/awtrix-release
                   --data /data/awtrix-ng --boot
      -> or rescue: USB RECOVERY on the panel, the GUI host becomes an idle shell,
         no Wi-Fi, USB ADB kept
      -> or the untouched vendor app (knob held, pending vendor update)
  awtrix-tc002d      -> awtrix-linux --board tc002 ..., speaker helper, wpa_supplicant, udhcpc
```

### Loader decisions

At every start the loader reads `/sys/bus/platform/devices/soc:usbotg/otg_role` and
logs it. Only when it reads `usb_host` or `usb_null` does it write `usb_device`, once, so USB ADB
works whatever follows. Until the kernel's USB scan ends (about 8.5 s after boot) the role reads
`unkown` even when the port already works, and every write costs a USB disconnect; the loader then
leaves the role to the daemon (`HardwareLease::checkUsbRole`). The daemon reads it every 500 ms
while it reads neither role during its first 20 s, then every 5 s for two minutes and every 30 s
after that, and writes `usb_device` only over a definite `usb_host` or `usb_null`, at most every
10 s. Then the loader checks, in this order:

| Condition | Result |
|---|---|
| a vendor update is pending | vendor app, which installs the update |
| knob pressed at start | vendor app for this boot, the start counter cleared |
| `/tmp/awtrix-loader.vendor` exists | vendor app (the knob chose it earlier in this boot) |
| `/tmp/awtrix-install/lock.token` written within the last 90 s (a USB `deploy` runs) | deploy hold: the app is reported running, no release starts; then the rows below |
| no release in the slot, or its image differs from the slot's header | rescue |
| boot counter at 3 or unreadable | rescue, and the counter is cleared, so the next power-on tries the release again |
| otherwise | the app is reported running, boot counter + 1 (synced), mount the release, exec its daemon |

**Mounting the release slot.** The loader finds `res` in `/proc/mtd`, reads the squashfs
superblock and the slot's header through `/dev/mtd/mtd<N>ro` and hashes the image against the
header's SHA-256 (`release_slot.h`, `slot_read` in `flasher/slot_io.c`, which the flash helper
shares); a 5 MB image takes a fraction of a second. To start the release it loads
`/res/awtrix-ng/loop.ko` (`finit_module`; an already loaded module is fine), attaches a loop device
over `/dev/block/mtdblock<N>` at the image's offset and length, read-only, cleared by the kernel
once the mount goes and with direct I/O (`LOOP_SET_DIRECT_IO`), detaches a release mounted earlier
in this boot and mounts the squashfs `ro,nodev,nosuid` at `/tmp/awtrix-release`.

Direct I/O matters: `res` stays mounted from that block device, so its page cache outlives a slot
write, which goes through the MTD character device past it. `BLKFLSBUF` does not reach that cache
(the mtdblock driver answers the ioctl itself) and the stock kernel has no `fadvise`. Through the
cache, a mount after a deploy would read stale blocks; with direct I/O it reads the flash the slot
check read. A kernel that refuses direct I/O gets `drop_caches` instead. The device nodes it needs,
`loop-control` and the loop device, the loader creates in `/tmp` and removes again. A release that
cannot be mounted, or whose daemon is missing or fails to exec, rescues.

Before any exec the loader waits until its host process is 500 ms old.
Each start is logged to `/tmp/awtrix-loader.log` and `/data/awtrix-ng/state/loader.log`
(2 x 8 KiB). The decisions are in `loader_policy.c`; `loader.c` holds the host's three entry points,
the slot, the mount, the USB role and the rescue, `rescue_panel.c` the panel message and
`loader_state.c` the start counter and the log files.

**Start counter.** The loader counts each start in `state/boot-attempts` and syncs the file before
the exec. When `/data` cannot take it, it counts in `/tmp/awtrix-loader.attempts`, which lasts until
the next boot; the larger of the two decides. The daemon deletes the counter once the runtime has
been healthy for 60 s, when the ready runtime requests a reboot and when the daemon is stopped while
the runtime is ready. While a new release waits for its confirmation, only the confirmation clears
it ([Flow](#flow)). A stop while the runtime is starting, crashing or backing off keeps the count,
so a release that never becomes ready reaches 3. `deploy` and a knob start clear it too.

**Rescue** replaces every automatic fallback to the vendor app. The loader enters it when the slot
holds no valid release, when the release cannot be mounted or its daemon cannot run, after three
starts that never became healthy (or with an unreadable counter), when neither `/data` nor `/tmp`
can count a start, and when the vendor library cannot be loaded although the knob or a pending
update chose it. It then:

- reports the app as running, so the stock recovery check leaves `res` alone;
- writes the reason (for example
  `the image differs from its header; the AWTRIX release in res is damaged: rescue`) to
  `/tmp/awtrix-loader.rescue` and both logs;
- shows `USB` over `RECOVERY` in amber on the panel, drawn in the 3x5 terminal font and sent over
  `spidev` with the GPIO35 latch;
- replaces the GUI host with an idle shell (`awtrix-loader-rescue` in `ps`).

The exec also ends the host's other threads, so nothing brings up Wi-Fi, and the GUI service keeps
its process, so init does not restart it in a loop. A timer process forked before the exec
(`awtrix-rescue-t`) wakes the shell every 2 s, and the shell keeps USB ADB with the rule above, each
role change appended to the rescue marker (up to 64 lines). A rescue after
unhealthy starts clears the counter; every other rescue repeats at each power-on until `deploy`
writes a release. Every start that does not rescue removes the marker.

**Deploy hold.** When a USB `deploy` stops the daemon to write the release slot, init restarts
the GUI service, and the loader must not mount the slot while it is being written. While the token
`/tmp/awtrix-install/lock.token` is fresh, the loader stays inside the GUI host, checks the token about
once a second, keeps USB ADB with the rescue rule and logs the hold once. When the token is gone or
90 s passed since its last write, it decides again from the next row. A hold lasts at most 30 min;
the knob, a pending vendor update and the vendor marker come first. The token holds `deploy` and
the device's uptime at the write. A token met for the first time is judged by that uptime and every
later rewrite on the monotonic clock, so a wall clock that jumps when SNTP sets it neither ends the
hold early nor stretches it.

### The supervisor, `awtrix-tc002d`

One `EventLoop` polls every service. Services start in the order they were added and stop in
reverse, each after the previous one reports stopped or its timeout passed. Signals arrive through
a self-pipe: `SIGCHLD` reaps every exited child and offers it to the services, `SIGTERM` and
`SIGINT` request an orderly stop. Services never block, own no threads and install no signal
handlers.

| Service | Class | What it owns |
|---|---|---|
| hardware lease | `HardwareLease` | the board model check, the USB device role, the panel latch GPIO35 (exported, output, idle high) with a snapshot of the SPI settings restored on shutdown, `EVIOCGRAB` on the key and knob devices, handed to the runtime as descriptors 100 and 101 |
| MCU | `McuService` | the MCU UART (raw 1.5 Mbaud 8N1), its only owner: firmware version and AWTRIX identity, USB power every second, battery every ten seconds, microphone capture on demand ([Microphone](microphone.md)), MCU firmware updates ([MCU updates](mcu-update.md)) |
| Wi-Fi | `WifiService` | ADB over TCP off before anything associates, the stock supplicant stopped, the release's aic8800 pair loaded when `wlan0` is missing (`aicwf_dbg_level=0`), our own `wpa_supplicant` with a generated configuration (the saved network, or the setup access point when there is none or it does not connect), the link state from its control interface; the credential record in `/data/awtrix-ng/network` holds the SSID and the derived PSK, never the passphrase |
| Bluetooth | `BtService` | attaches the Bluetooth half of the AIC8800DC on `/dev/ttyS3` while the runtime asks for it, and gives it back ([Bluetooth](bluetooth.md)) |
| IP | `IpService`, `IpController` | a fixed address from the runtime's `address` message, otherwise `udhcpc` on `wlan0` whose callback records are applied through rtnetlink (for the setup access point: `192.168.4.1`, `udhcpd` and captive DNS), our `resolv.conf` bind-mounted over `/etc/resolv.conf`, SNTP, the kernel hostname (`awtrixng-` and the last six hex digits of the MAC unless set in the web UI) and an mDNS responder for `<hostname>.local` with `_awtrixng._tcp` (TXT `id`, `name`, `type`) |
| control | `ControlSocket` | `/tmp/awtrix-tc002d/control.sock` for `awtrix-tc002d ctl COMMAND`: `commands`, `status`, `bluetooth`, `restart-runtime`, `stop`, `wifi-set`, `wifi-status`, `wifi-scan`, `wifi-scan-results` |
| runtime | `RuntimeChild` | `awtrix-linux` and the speaker helper next to it; restarts after an exit with a backoff of 1, 2, 5, 10 and 30 s; a runtime without readiness after 30 s is ended; 60 s of readiness count as healthy |
| update | `UpdateService` | the hand-off of a verified web update to the flash helper and the confirmation of a new release ([Web update](#web-update)) |

`DeviceState` holds what the services learn (power, network, time) and fans every change out to the
runtime, the `status` reply and the log. At start the daemon:

- sets `kernel.panic=5` and `kernel.panic_on_oops=1`;
- checks that the release carries every program and module it runs;
- creates `app/` and `state/`;
- reconciles the update record with what the flash helper recorded in `state/update-result`;
- reads the release slot's capacity (`/proc/mtd` and `mtd<res>ro`);
- decides the first start reason (`firstStartReason`): `software` when a daemon already ran in this
  boot or a daemon rebooted the clock (recorded in `state/rebooted` first), else `poweron`.

`--boot-intro first|always|never` picks the runtime starts that show the power-on intro: the 5 s
animation with the boot sound, followed by the info screen (version, and the address when Wi-Fi is
up). `first`, the default (the loader passes no value), shows it only on the first runtime start
after power-on; `always` on every start; `never` on none. The intro is claimed once per power-on
through `/tmp/awtrix-boot-intro.shown`, because a deploy or a restart of the GUI service starts a new daemon in
the same boot. A start with the intro gets `--boot-intro`, and `--boot-sound <root>/share/boot.mp3`
when the release carries the file. The runtime starts the animation when the sound becomes audible,
and runs it silent without a speaker, with sounds off, without the file or when the sound is not
audible within 1.5 s.

The clock's id (`uid` in `/api/v1/device`, the default MQTT prefix and client id, the Home
Assistant device id) is the Wi-Fi MAC as twelve lowercase hex digits, as on the ESP32 and in the
mDNS `id`. The driver reports the MAC seconds after the daemon started, so `DeviceId` keeps it in
`state/uid` and every runtime gets it as `--uid`. While no id is kept, the first start waits up to
20 s for the MAC; a runtime that got no id (it takes a random one for that run) or another one is
restarted once when the MAC arrives.

The log is `/tmp/awtrix-tc002d/daemon.log`, bounded and rotated to `daemon.log.1`, mirrored to
stderr; the runtime's own output appears there as `awtrix-linux`. Lines of the components `audio`,
`ip`, `mcu`, `runtime`, `sntp`, `update` and `wifi` also go to the runtime and appear in the web UI
log (`ForwardedLog`, `RuntimeChild::logLine`). Lines from while no runtime listens (among them the
update notes of the daemon's start and how the previous runtime ended) wait for the next runtime's
hello, the last 16 of them.

### The runtime on the TC002

`RuntimeChild` starts `awtrix-linux` in its own session with `PATH`, `HOME` and `TZ` only and exactly
these descriptors (`RuntimeContract.h`): 100 the keys, 101 the knob, 103 the supervisor channel
(`SOCK_SEQPACKET`), 104 the socket to the speaker helper when the speaker runs. The arguments:

```
awtrix-linux --board tc002 --tc002-input-fds 100,101 --supervisor-fd 103
  --data /data/awtrix-ng/app --webui <root>/share/index.html.gz --lan --port 80
  --start-reason poweron|software|panic|watchdog [--uid <12 hex digits>]
  [--tc002-audio-fd 104 [--speech-voice <root>/share/speech/voice.atts]]
  [--boot-intro [--boot-sound <root>/share/boot.mp3]]
  [--ca-file <root>/share/ca-certificates.crt]
  [--update-state /data/awtrix-ng/state/update-state.json
   --release-root <root> --update-dir /tmp/awtrix-update]
```

The start reason follows how the previous runtime ended: `software` after a requested restart, an
update or a clean exit; `panic` after a crash or an error exit; `watchdog` after the daemon ended a
runtime that did not report readiness in time or reported an invalid one. The update flags are
passed only when the clock has a release slot and the release carries `bin/awtrix-tc002-flash`.
The voice comes with the speaker when the release carries it ([Speech](speech.md#loading)).

**Supervisor protocol** (`SupervisorProtocol.h`): one JSON object per datagram, version 2, bounded
in size. The daemon sends `hello` (its version, and the update status with the release slot's
capacity), `power`, `network`, `time`, `bluetooth`, `microphonePcm`, `microphoneStreamEvent`,
`wifiScanResult` and `log`. The runtime sends `hello`, `ready` (board, panel size and input, once its
HTTP listener is bound and its first frame is on the panel), `wifi` (credentials from the web UI),
`ntp`, `hostname`, `address` (the fixed IPv4 setup, or DHCP), `reboot`, `factoryReset`, `wifiScan`,
`bluetooth`, `microphonePcmRequest`, `microphoneStreamControl` and `updateReady`. The daemon's `hello`
goes first in every snapshot. When the channel closes, the runtime saves its state and stops.

The TC002 code of the runtime is in `src/platform/tc002/runtime/` unless noted:

| Code | Role |
|---|---|
| `Tc002Board` | the panel: 52 x 16 over `spidev` with the GPIO35 latch, and `kPanelLight`, the measured light per code (the MCU applies its own gamma and repeats codes; re-measure after an MCU firmware change) |
| `Tc002Input`, `Tc002QuickSettings` | decodes the key and knob events from descriptors 100 and 101; the knob screen for brightness and volume |
| `SupervisedRuntime`, `SupervisorLink` | the runtime end of the channel: applies power, network and time state, forwards settings, requests reboot and factory reset |
| `LinuxDeviceFacts`, `LinuxMemory` (`src/platform/linux/`) | `/api/v1/device` on Linux: start reason, uptime since boot (`CLOCK_BOOTTIME`), `MemAvailable` as free heap with its low-water mark |
| `Tc002Update` | the web update in the runtime ([Flow](#flow), steps 1 to 3 and 8) |
| `Tc002ClockApp`, `Tc002StatusApp` | the TC002's clock (registered as `Time`, five faces by `clockFace`) and status page |
| `Tc002Speaker`, `src/platform/tc002/audio/` | the speaker behind the engine's tone and PCM sinks: melodies, MP3 and radio over descriptor 104 |
| `MicrophoneInput` | microphone snapshots for the audio visualisation ([Microphone](microphone.md)) |
| `src/platform/tc002/voice/` | Home Assistant Voice ([Home Assistant Voice](ha-voice.md)) |
| `Tc002BootIntro`, `BootIntroWide`, `BootInfoWide`, `TerminalFont` | the power-on intro timed by the boot sound, then the info screen with the version and the address, both in the 3x5 terminal font |
| `tls/` (`src/platform/linux/tls/`) | the one TLS layer: the policy the `--hardened` listener and the MQTT client use, the public CA store every HTTPS client verifies against ([CA certificates](ca-certificates.md)), broker trust with pinned fingerprints and `/api/v1/mqtt/tls`, and the `--hardened` server identity |
| `LinuxLanLogin`, `LinuxScriptHttp`, `LinuxMqttTls` (`src/platform/linux/`) | the web login as on the ESP32, the scripts' HTTP worker, the MQTT TLS transport |

The web UI is the ESP32's `webui/index.html`, served minified and gzip-encoded from the release. It
is one file for both platforms: it asks the device what it can do and shows no TC002-only notices.

### Files on the clock

The shared paths are in
[`src/platform/tc002/contract/tc002_layout.h`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/tc002/contract/tc002_layout.h);
`tools/tc002/install/test_layout.py` holds the installer to them.

| Path | Content |
|---|---|
| `/tmp/awtrix-release/` | the release, mounted read-only from the release slot: `bin/`, `lib/modules/`, `share/`, `manifest.json` |
| `/data/awtrix-ng/app/` | the runtime's data: settings, icons, MP3s, scripts |
| `/data/awtrix-ng/network/` | the Wi-Fi credential record |
| `/data/awtrix-ng/state/` (0700) | `boot-attempts`, `update-state.json`, `update-result` (what the flash helper recorded for a web update), `uid`, `rebooted` (the daemon rebooted the clock), `loader.log`, `res-image.json` (what `flash-loader` wrote), `keep-adb-tcp` (developer flag, see [Debugging](#debugging-on-the-clock)), `autostart` (own program, see [Own program at start](#own-program-at-start)) |
| `/res/lib/libawtrix-loader.so`, `/res/etc/EasyUI.cfg` | the loader and the configuration that names it |
| `/res/awtrix-ng/loop.ko` | the loop driver the loader mounts the release with |
| `/tmp/awtrix-tc002d/` | the daemon's run directory: `control.sock`, `daemon.log`, `lock`, `started`, `wifi/`, `ip/` |
| `/tmp/awtrix-loader.*` | the loader's log, RAM counter and the `vendor` and `rescue` markers |
| `/tmp/awtrix-update/` | a web update's package, lock and the copy of the flash helper |
| `/tmp/awtrix-install/` | a USB install: the flash helper, the release image while `deploy` writes it, `lock.token` |

## Code map and toolchain

### Source tree

| Path | Content |
|---|---|
| `src/` outside `platform/` | the portable engine the ESP32 firmware builds too |
| `src/platform/linux/host/` | host implementations of the engine's services (stores, HTTP server, MQTT, TLS, media), shared by `awtrix-linux` and the native tests |
| `src/platform/linux/` | `awtrix-linux`: `main_linux.cpp` and generic Linux infrastructure ([AWTRIX on Linux](../linux/index.md)) |
| `src/platform/tc002/runtime/` | TC002 runtime adapters, target policy, built-in apps and update integration |
| `src/platform/tc002/voice/` | Home Assistant Voice |
| `src/platform/tc002/update/` | `UpdateState`, the update state file and its lock |
| `src/platform/posix/` | file, descriptor, clock and SHA-256 helpers without OpenSSL, for the static programs |
| `src/platform/tc002/contract/` | what the programs agree on: `tc002_layout.h`, `RuntimeContract.h`, `SupervisorProtocol`, `MicrophoneStream`, `release_slot.h` |
| `src/platform/tc002/daemon/` | `awtrix-tc002d`, with `wifi/`, `ip/` and `update/` |
| `src/platform/tc002/loader/` | `libawtrix-loader.so` |
| `src/platform/tc002/update/` | the package format and verifier and the release manifest reader; `verify_main.cpp` is the host tool `awtrix-update-verify` |
| `src/platform/tc002/mcu/` | the MCU firmware preparation (`awtrix-linux --prepare-mcu`) |
| `src/platform/tc002/audio/` | the runtime's speaker (`Tc002Audio*`) and the speaker helper (`helper/`, `pcm/`) |
| `src/platform/tc002/flasher/` | `awtrix-tc002-flash`, and the slot reader the loader shares |
| `src/platform/tc002/kmod/` | the `awtrix_pcm` driver and the aic8800 patches |
| `tests/tc002/` | contracts per program: `contract/`, `daemon/`, `loader/`, `flasher/`, `runtime/`, `speaker/`, and `arm/` for the ones that run under `qemu-arm` |
| `tools/tc002/` | host tooling and the recipes of the third-party programs a release ships ([tools overview](tools.md)) |

### CMake

The root `CMakeLists.txt` includes one module per product from `cmake/`: `Core.cmake` (the engine),
`Host.cmake` (host services), `Posix.cmake`, `Linux.cmake` (`awtrix-linux`), `Tc002.cmake` (the
contract, the update library, the daemon, the DHCP callback, the speaker and flash helpers),
`Tc002Bundle.cmake` (the install component `tc002`) and `Tc002Loader.cmake` with
`Tc002LoaderCheck.cmake` (the loader, and a check after every link that it exports only the
three entry points the GUI host calls and needs no symbol version above the stock glibc 2.30). `Warnings.cmake` gives
every first-party target `-Wall -Wextra` (`-Werror` with `AWTRIX_WERROR`). `AWTRIX_DEPS` defaults to
`.pio/libdeps/native`; run `pio pkg install -e native` once.

| Preset | Toolchain | Builds |
|---|---|---|
| `host` | the machine's compiler | `awtrix-linux` and every regression test |
| `tc002` | `cmake/toolchains/tc002-musl.cmake` (`TC002_MUSL_PREFIX`) | the static release programs, `MinSizeRel` with LTO, `-fno-pie`, function sections and a terminate handler that aborts; `awtrix-linux` links the trimmed static OpenSSL of `tools/tc002/openssl` ([OpenSSL](openssl.md)); the ARM contracts when `qemu-arm` is found |
| `tc002-loader` | `cmake/toolchains/tc002-glibc.cmake` (`TC002_GLIBC_PREFIX`) | `libawtrix-loader.so` |

`cmake --install BUILD --component tc002` puts the programs, the web UI and the boot sound into a
bundle directory; `build_bundle.sh` adds the rest ([A release
bundle](#a-release-bundle)).

### Toolchains

[`tools/tc002/toolchain/env.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/toolchain/env.sh)
is sourced by every recipe and says where each toolchain comes from when one is missing:

- **musl** (`TC002_MUSL_PREFIX`, `arm-linux-`): the cross compiler of the pinned Buildroot tree,
  built by `tools/system/build.py` ([System build tooling](../linux/system-build.md)). It builds
  every program of a release.
- **glibc** (`TC002_GLIBC_PREFIX`, `arm-none-linux-gnueabihf-`): ARM GNU Toolchain 9.2-2019.12,
  fetched and checked by `tools/tc002/toolchain/fetch-arm-gnu-9.2.sh`. It builds the loader and the
  kernel modules.
- **Kernel tree**: `tools/tc002/kernel/fetch-source.sh` and `prepare-tree.sh` prepare the Pioneer3
  SDK kernel 4.9.84 with a configuration that matches the clock's kernel (module ABI in
  [Kernel modules](kernel.md)).
- Source tarballs come through `TC002_DL_DIR` and must match their pinned SHA-256
  (`tools/tc002/lib/fetch.sh`); work files go below `TC002_CACHE`.

The release manifest records both compilers (version and a digest of what `gcc -v` reports), so a
release names the toolchains it came from.

### Tests

Every contract is a CTest. `ctest --preset host` runs the host suite; `ctest --preset tc002` runs the
ARM contracts (speaker helper, owner scan, the shipped helper's refusals, the flasher) under
`qemu-arm -cpu cortex-a7`. Integration tests that need a service the machine lacks exit 77 and are
skipped (label `needs-mosquitto`); `AWTRIX_REQUIRE_INTEGRATION_SERVICES=ON` turns the skip into a
failure. The daemon's tests run the real services against a fake runtime
(`tests/tc002/daemon/fake_runtime.cpp`) and a fake audio helper, the runtime's supervisor tests run
the real `awtrix-linux` against a socket pair (`tests/linux/test_supervisor_channel.py`), and the
loader's host tests run `loader_policy.c` against fake daemons and a fake vendor library.
`python scripts/test_native.py` runs the engine's unit tests; they and the host build use the
engine library from `cmake/Core.cmake`. See also [Host tests](../host-tests.md).

## Build, install, debug

### Host

```
pio pkg install -e native
cmake --preset host && cmake --build --preset host && ctest --preset host
```

`awtrix-linux` runs on the development machine as well (`--board headless`, loopback by default)
and serves `webui/index.html` from disk.

### A release bundle

In WSL, with both toolchains set; the kernel tree needs preparing once:

```
source tools/tc002/toolchain/env.sh
tools/tc002/kernel/fetch-source.sh && tools/tc002/kernel/prepare-tree.sh
bash tools/tc002/install/build_bundle.sh OUT
```

The script builds one commit (`--commit`, default `HEAD`), exported with `git archive`, so nothing
outside the commit enters the release: the version is `<version>-g<commit>` and the manifest
counter the commit time. It builds the two presets in fresh build directories, installs their
`tc002` component (with the voice when `assets/speech/voice.atts` exists, [Speech](speech.md#a-voice-for-the-release)),
and adds `udhcpc`, `wpa_supplicant`, the CA bundle, the licence texts, the three kernel modules of
the release, `loop.ko` and the pinned MCU patch ([MCU updates](mcu-update.md)).
It checks that every program is a static hard-float binary, writes `manifest.json` and ends with
`release.img`, the release image (`bundle.py image`: mksquashfs 4.6 or later, xz, 128 KiB blocks,
owner root, every time the release counter).

`--working-tree` builds the checkout as it is, for trying a change on a clock; the version then ends
in `-dirty`, and `package` refuses such a bundle without `--allow-dirty`. When git cannot read the
checkout (a Windows checkout seen from WSL), set `SOURCE_DATE_EPOCH` to the commit time.

### Onto a clock

[`tools/tc002/install/tc002_install.py`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/install/tc002_install.py)
does everything over USB ADB; every command that talks to the clock needs `--serial`. In the order a new clock needs them:

1. `connect`: a clock that never ran AWTRIX offers USB ADB for about 2.7 s after power-on; the
   command catches that window and starts a USB keeper.
2. `backup --output DIR`: every partition, into a private directory outside the checkout.
3. `secure-vendor`: removes the vendor app's cloud tokens and Wi-Fi network and switches its Wi-Fi
   off.
4. `build-res` and `flash-loader`: the `res` image with the loader and `loop.ko`, built from the
   clock's own backup (the vendor files are not redistributable), written and read back, and the
   bundle's release written into the release slot behind it.
5. `deploy --bundle OUT/bundle`: every later release, written into the release slot.
6. Wi-Fi: `python tools/tc002/tc002_wifi.py --serial SERIAL set` asks for the passphrase at a hidden
   prompt (or reads it from stdin with `--password-stdin`), never from the command line.

`package --bundle B --out F` builds the `.awup` for the web update; `status` shows what runs, the
update state, the start counters and the loader's logs; `restore-stock` brings the vendor app back.
[Install tools](install-tools.md) describes each command. End users install with the
[desktop installer](desktop-installer.md), which runs the same steps; the
[browser image tools](browser-image.md) build the packages it and the browser installer use.

### Debugging on the clock

- Daemon: `adb -s SERIAL shell /tmp/awtrix-release/bin/awtrix-tc002d ctl status` (all services, the
  runtime, the speaker, the MCU, the update), `ctl wifi-status`, `ctl bluetooth`,
  `ctl restart-runtime`.
- Logs: `/tmp/awtrix-tc002d/daemon.log` (and `.1`), `/tmp/awtrix-loader.log`,
  `/data/awtrix-ng/state/loader.log`, `/tmp/awtrix-loader.rescue` after a rescue. The web UI log
  (`/api/v1/logs`) shows the runtime and the daemon's network, audio, MCU and update lines.
- The panel content: `GET /api/v1/display/screen`.
- The Wi-Fi driver: `ctl wifi-status` reports `driver` as `release`, or `already loaded` when a
  daemon restarted since power-on found `wlan0`; `/sys/module/aic8800_fdrv/srcversion` identifies
  the build.
- ADB over Wi-Fi: a root-owned regular file `/data/awtrix-ng/state/keep-adb-tcp` (not a symlink;
  `adb shell touch` creates it that way) makes the daemon leave ADB's TCP port as it is, and from its
  next start it warns every hour. Everyone on that network can then take over the clock as root:
  create the file only on a network you control, then remove it
  (`adb -s SERIAL shell rm /data/awtrix-ng/state/keep-adb-tcp`) and reboot.

### Own program at start

The daemon starts one program of your own next to AWTRIX NG: `/data/awtrix-ng/state/autostart`.
Only USB ADB reaches that directory; the web UI and the API cannot write there.

```bash
adb -s SERIAL push myprog /tmp/autostart
adb -s SERIAL shell "cp /tmp/autostart /data/awtrix-ng/state/autostart && chmod 700 /data/awtrix-ng/state/autostart && rm /tmp/autostart"
adb -s SERIAL shell /tmp/awtrix-release/bin/awtrix-tc002d ctl restart-autostart
```

- It runs as root, when the daemon starts, without waiting for Wi-Fi. It must be a regular file
  (not a symlink), owned by root, executable, and not writable by group or others; otherwise the
  daemon logs why and does not start it.
- A script needs a `#!/bin/sh` line. The clock's shell has no `grep`, `head`, `tr` or `sleep`;
  bring your own tools.
- Environment: `PATH=/bin:/sbin:/usr/bin:/usr/sbin`, `HOME=/data`, and `TZ` when set. Standard
  input is empty; standard output and error go to `daemon.log` as `autostart:` lines, 100 lines per
  10 minutes, the rest counted.
- Keep the program in the foreground. Exit status 0 ends it until the next start; any other end
  restarts it after 1 s, 5 s, 30 s and then every 60 s. After 5 failures in a row it stays off until
  the daemon starts again; a run of at least a minute resets that count.
- Whenever the daemon stops (deploy, update, reboot) it sends SIGTERM to the program's process
  group, SIGKILL after 3 s. It never touches the panel, buttons, speaker or the
  start counters: a crashing program cannot put the clock into rescue.
- `ctl status` shows it under `autostart` (`state`, `pid`, `starts`, `lastExit`, `refusal`).
  `ctl restart-autostart` checks the file again and starts it with a fresh failure count.
- Remove it with `adb -s SERIAL shell rm /data/awtrix-ng/state/autostart` and
  `ctl restart-autostart`, or reboot.

### Traps

- All TC002 units report the USB serial `0123456789ABCDEF`: attach one at a time, and always pass
  `adb -s`, because other ADB devices may be attached.
- From Git Bash, set `MSYS_NO_PATHCONV=1` for adb commands with device paths.
- The clock's shell has no `grep`, `head`, `tr` or `sleep`: filter on the host. Never
  `adb shell cat` a binary; use `adb pull`.
- In `/sys/bus/platform/devices/soc:usbotg/` read only `otg_role`. The `usb_device`, `usb_host` and
  `usb_null` nodes next to it act on read.
- Never print raw `/proc/kmsg` or `dmesg`: in a boot in which a Wi-Fi driver without patch
  `0003-never-log-key-material.patch` ran, the kernel log can hold session keys.
- `adb push` into JFFS2 wedges USB: every tool stages through `/tmp`.
- `/tmp/awtrix-release` is a read-only squashfs; a release changes only as a whole, through the
  slot.
- A deploy or a restart of the GUI service starts a new daemon in the same boot. Whatever must happen once per
  power-on needs a marker in `/tmp`.
- Every power-on plays the boot sound: switch sounds off through the API before test reboots.
- The vendor app's files in `/data` hold device identifiers; never print or commit them.
- `WebUiAsset.h` (the ESP32's embedded web UI) is generated by the PlatformIO build; never merge
  it by hand.

## Web update

### Requirements

1. The owner updates AWTRIX NG from the web UI, without USB and without WSL. Creating a package
   needs Linux or WSL (`tc002_install.py package`).
2. Icons, MP3s, scripts and settings never block an update and survive it.
3. A power cut at any moment leaves a clock that either runs a release or shows USB RECOVERY and
   keeps USB ADB, from where `deploy` writes a release. A release whose runtime never becomes
   healthy ends in the same rescue after three starts, and every power-on after that tries it
   again.
4. Packages must pass format, target and integrity checks, with a counter above what the clock
   accepted. Their download source is chosen by the operator.
5. The ESP32's update behaviour stays as it is: the web UI branches on `updateImage`.

A web update writes the release slot only: never the squashfs of `res` (the loader, `loop.ko`, the
vendor app), the kernel, root filesystem or radio firmware. A release can carry our pinned MCU
extension; after a healthy, confirmed boot the daemon installs it over the UART when USB power is
present ([MCU updates](mcu-update.md)). The package format is shared with generic Linux targets
([Update packages](../linux/updates.md)).

### Flash and space

| Partition | Content | Written by |
|---|---|---|
| `res` (mtd3, 8 MiB) | the squashfs (xz): the vendor app (knob fallback), the loader and `loop.ko`; behind it the release slot | the USB installer; the flash helper writes the slot for a web update |
| `data` (mtd6, 8 MiB JFFS2) | `app/`, `network/`, `state/`: settings and user data | the runtime, the daemon, the installer (`state/`) |
| `/tmp` (tmpfs) | the uploaded package during a web update; the release image during a USB deploy | the runtime, the installer |

The release slot ([`release_slot.h`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/platform/tc002/contract/release_slot.h))
starts at the first erase block behind the res squashfs (its `bytes_used` rounded up to 64 KiB).
That block holds the header, 154 bytes, big-endian: `AWSLOT01`, the image length (u64), the counter
(u64), the image's SHA-256, the release name's length (u16) and the name (64 bytes, zero-padded),
and a SHA-256 over all of that. The image starts at the next erase block and may run to the end of
the partition.

`slot_write` (`flasher/slot_io.c`) first checks that its source is a release image (a squashfs whose
`bytes_used`, rounded up to 4 KiB, is the source length) of the length and SHA-256 the new header
names. It then erases the header block, writes and verifies the image, and writes and verifies the
header last, so an interrupted write leaves no release rather than a damaged one. Blocks that
already hold the right bytes are skipped. Since the helper writes past the block device's page
cache, `write-slot` then drops the clean page cache (`/proc/sys/vm/drop_caches`, reported as
`blockCache`), for loaders that mount the slot through that cache. With the stock vendor content
the res squashfs with the loader and `loop.ko` takes 2.57 MB, which leaves a slot of 5.70 MB; a
release image takes about 2.8 MB.

- Uploads (icons, MP3s, scripts) are refused when they would leave less than 1 MiB free
  (`kTc002UploadReserveBytes`). Settings may use that reserve, and script stores may use free space
  down to a quarter of it.
- A web update takes no space on `/data`: the runtime refuses an image larger than the slot, whose
  capacity the daemon reports in its `hello` ("the firmware image takes X MB, the clock holds
  Y MB"), and the upload itself needs RAM ([Flow](#flow), step 1).

### Package

`AWUPD003`, all integers unsigned big-endian:

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 8 | ASCII `AWUPD003` |
| 8 | 2 | major version, 3 |
| 10 | 2 | minor version, 0 |
| 12 | 4 | manifest length: 68 + target + release |
| 16 | 8 | payload length, 1 to 256 MiB (the runtime takes 8 MiB) |
| 24 | 8 | counter, above 0 |
| 32 | 2 | target length, 1 to 128 |
| 34 | 2 | release length, 1 to 64 |
| 36 | 32 | SHA-256 of the payload |
| 68 | variable | the target, then the release |
| manifest length | 32 | SHA-256 of the whole manifest |
| manifest length + 32 | payload length | the payload; nothing may follow |

- **Target**: `awtrix-ng:tc002`, whose payload must be a release image; other targets only as
  `experimental:` and at least one of `[A-Za-z0-9._:-]`.
- **Release**: `validReleaseName` (`src/platform/tc002/contract/ReleaseName.h`): 1 to 64 of
  `[A-Za-z0-9._+-]`, starting with a letter or digit, not ending in `.partial`. The slot's header
  carries it, and it must equal the `updateReady` message. A bundle names its release
  `<version>-<12 hex digits of a SHA-256 over its installed files>`, the version being
  `<version file>-g<12 hex digits of the commit>`, with `-dirty` for a working-tree build.
- **Counter**: the committer time of the release commit. A package installs only when its counter is
  above both the accepted counter of `state/update-state.json` and the `counter` of the running
  release's `manifest.json`. The supervisor protocol and `bundle.py` stop at 2^63 - 1; the release
  image stores it as its squashfs time, so a release needs a counter below 2^32.
- **Payload**: the release image, the squashfs of the installed release (`manifest.json` without
  `hostOnly` and `image`, `bin/`, `lib/modules/`, `share/`), made by `bundle.py image` with
  mksquashfs 4.6 or later: xz, 128 KiB blocks, no extended attributes, owner root, directories
  0755, files with their manifest mode, every time the counter, padded to 4 KiB. The verifier
  checks the squashfs magic and that `bytes_used`, rounded up to 4 KiB, is the payload length.
- **Integrity**: SHA-256 covers the manifest and payload separately. There is no signing key or
  publisher authentication; the operator chooses a trusted download source. `AWUPD001` and
  `AWUPD002` are not accepted; a clock with an updater for those needs `build-res` and
  `flash-loader` over USB.
- **Tools**: `tc002_install.py package` wraps a bundle's `release.img` through
  `tools/update/package.py --image`, which needs no key or OpenSSL command; `awtrix-update-verify`
  checks a package on a host.

`tc002_update` (`PackageFormat`, `ReleaseManifest`; no OpenSSL) is linked by the runtime and the
daemon; `tc002_update_verify` (`PackageVerifier` with OpenSSL, and `release_slot.c`) only by
`awtrix-linux` and `awtrix-update-verify`. The daemon relies on the runtime's integrity check, the
private update directory owned by its own user and the match with the staged record; the flash
helper checks the image's length, SHA-256 and squashfs superblock again before it erases anything.

### Flow

| Step | Where | What happens |
|---|---|---|
| 1 upload | runtime, `Tc002Update::receive` | `POST /update` with exactly one multipart part `firmware` that has a file name, streamed into `<update-dir>/package.awup`: the directory owned by the runtime's user with mode 0700, the file created exclusively with mode 0600. At most 8 MiB and `min(tmpfs free, MemAvailable) - 4 MiB`. The web login applies when it is on; a request with an `Origin` header must name its `Host`. The runtime holds `flock` on `<update-dir>/lock` from here on and refuses while another update runs, a package is staged or the daemon reports `boot-pending`. |
| 2 verify | runtime, `Tc002Update::verifyPackage` | `tc002::update::verify` with manifest and payload checksums, target `awtrix-ng:tc002`, the counter rule and the release image check, then the image against the slot's capacity from the daemon's `hello`. A refusal removes the package, frees the lock and answers `{"error":{"code","message"}}`; nothing on flash changes. |
| 3 hand-off | runtime | Answers `{"ok":true,"applying":true}` and draws the "UPDATE" frame; once the answer went out and the frame is on the panel, it sends `updateReady{package, release, counter, payloadSha256}`. |
| 4 stage | daemon, `UpdateService::handOff` | The package must be a regular file owned by the daemon's user directly in the update directory (not a symlink, no group or other permissions), its header must name the message's release, counter and digest with target `awtrix-ng:tc002`, the image must fit the slot, the running release must be confirmed and no MCU firmware operation may run. The daemon copies the running release's `bin/awtrix-tc002-flash` into the update directory, records the update as staged and stops its services: the runtime and every other child end, and the panel is released without blanking it. A refused hand-off keeps everything running: a new `hello` carries `update.error` `update <release> refused: <reason>`, and the runtime removes the package and frees the lock. A runtime that gets no answer within 60 s does the same with "the supervisor did not install the update". |
| 5 write | daemon, `UpdateService::prepareExec`; flash helper `write-slot` | The daemon records the update as boot-pending and execs the helper's copy: `awtrix-tc002-flash write-slot <package> --offset <payload offset> --length <n> --sha256 <digest> --release <release> --counter <n> --mount /tmp/awtrix-release --result state/update-result --reboot`. The helper unmounts the release (it refuses while the mount is busy), writes the slot as described in [Flash and space](#flash-and-space), records `done: <release>` or `<stage>: <error>` in `state/update-result` and reboots once it changed the flash. A refusal before any erase changes nothing: the helper exits, init restarts the GUI service and the loader starts the release that was there. |
| 6 confirm | new daemon, `UpdateService` | A boot-pending release that runs, under the counter the update named, is confirmed once its runtime has been healthy (ready for 60 s), checked every 5 s: its counter becomes accepted and the loader's start counter is cleared. Until then runtime health does not clear that counter, so a release that never becomes healthy reaches three starts, and the loader rescues. |
| 7 rescue | loader | There is no second release. A slot write that did not finish leaves no valid release and the loader rescues with USB RECOVERY until `deploy` writes one; a release that failed three starts rescues too, and the next power-on tries it again. |
| 8 report | runtime, `Tc002Update::addFacts` | `/api/v1/device` and the MQTT device state carry `updateImage` and `update` (`state`, `release`, `error`) while the web update is configured. The web UI reads `updateImage`: it offers `.awup` uploads and the online package (checking at most 8 MiB, the `AWUPD003` magic and the SHA-256 from the OTA index), waits for the reboot (15 s, then `/api/v1/device` every 3 s for up to 3 min) and reloads. |

| Status | Code | When |
|---|---|---|
| 400 | `badRequest` | not exactly one `firmware` part with a file name |
| 400 | `invalidPackage` | digest, format, limits or path |
| 400 | `wrongTarget` | a target other than `awtrix-ng:tc002` |
| 401 | | the web login is on and the request has no valid credentials |
| 403 | `forbiddenOrigin` | a web page of another site sent it: the `Origin` header does not name the `Host`, or `Sec-Fetch-Site` is neither `same-origin` nor `none` |
| 409 | `notNewer` | the counter is not above the accepted one |
| 409 | `insufficientStorage` | the release image is larger than the release slot |
| 409 | `updateBusy` | another update, a staged package or `boot-pending` |
| 413 | `payloadTooLarge` | above 8 MiB |
| 413 | `insufficientMemory` | above the RAM limit of step 1 |
| 500 | `internalError` | an unusable update directory, an upload that cannot be stored, unreadable free space, a supervisor that reported no release slot |
| 501 | `notSupported` | the runtime has no supervisor-backed update configuration |

### State, power cuts and locks

`state/update-state.json` is written through `UpdateState` (`src/platform/tc002/update/`) by
`UpdateRecord`: `idle`, `staged`, `activating`, `boot-pending`, `confirmed`, or `rolled-back` with
the failure `<release>: <reason>` (printable, at most 256 bytes). `UpdateRecord::status()` reports
`staged` and `activating` as `applying`, `rolled-back` as `failed`, an interrupted activation as
`failed` "the installation was interrupted", a missing file as `idle` and an unreadable one as
`idle` with its error; the runtime reports `applying` while it holds a staged package. `state/` is
0700 and the file 0600. A USB `deploy` and `flash-loader` write the same file: the release
`confirmed` with its counter accepted and the SHA-256 of its release image as payload digest.

At boot the daemon reads and removes `state/update-result`, and `UpdateRecord::reconcile` settles
what a power cut left:

- a corrupt file is kept as `update-state.json.corrupt` and started afresh;
- `staged` fails with "the installation did not start", `activating` with "the installation was
  interrupted";
- `boot-pending` with the new release running, under the counter the update named, opens the
  confirmation;
- `boot-pending` with another release running fails with "the release slot was not written (<what
  the helper recorded>)", or with "release X runs instead" when the helper recorded nothing or its
  success.

A power cut therefore leaves the old release (before the helper erased the header block), no
release and the clock in rescue (between that erase and the header write), or the complete new
release waiting for its confirmation.

Four locks keep writers apart: the update lock `<update-dir>/lock` (the runtime from the upload on,
a USB `deploy` through the flash helper while its token is fresh); the daemon's instance lock
`<run-dir>/lock`; the state file's lock on `state/`, held per `UpdateRecord` call and through the
hand-off; and the runtime's `<data>/.lock`.

### Bundle manifest

`manifest.json`, schema 3: `version`, `commit`, `dirty`, `release`, `counter`, `compilers`
(`{name: {version, sha256}}`), `image` (`size` and `sha256` of `release.img`), `files` (`path`,
`size`, `sha256`, `mode`) and `hostOnly` (the same members: `lib/libawtrix-loader.so` and
`lib/modules/loop.ko`, which go into the res squashfs), modes 0644 or 0755 only. The release image
carries it without `hostOnly` and `image`.

### Update tests

`update-package-contract` (the container, against `awtrix-update-verify` and `package.py`, with
fault injection), `update-state-policy`, `tc002-supervisor-protocol` and `tc002-supervisor-update`,
`tc002-flasher` and `tc002-arm-flasher` (the slot's layout, header and write order, refusals before
any erase, interrupted writes), `tc002d-update` (the slot's capacity, boot states, hand-off,
confirmation), `tc002-update` (the runtime's capacity check and panel ownership),
`linux-web-update` (the HTTP side against the real runtime), `tc002-loader-policy`,
`tc002-loader-host` (slot, mount and rescue against a fake `res`) and `tc002-install-tools`.

## Recovery

A TC002 has one release and no second copy. Every failure ends in one of three states:

| State | How it looks | Way out |
|---|---|---|
| rescue | `USB RECOVERY` in amber on the panel, no Wi-Fi, USB ADB open | `tc002_install.py --serial SERIAL deploy --bundle DIR` writes a release; a rescue after three unhealthy starts also tries the release again at the next power-on |
| vendor app | the Ulanzi app runs (knob held at power-on) | power off and on without the knob |
| reset key | holding the reset key for 5 s restores the Ulanzi firmware and wipes `/data` | the clock runs the Ulanzi app; install AWTRIX NG again from scratch |

`restore-stock --bundle DIR --res-dir DIR` writes the verified original `res` from the clock's own
backup over the whole partition, the release slot included; `--remove-data` also clears
`/data/awtrix-ng`. AWTRIX NG writes no partition outside `res`, and its tools restore none. The
customer-facing steps are in
[Reset & recovery](../../guides/reset-recovery.md#when-awtrix-ng-cannot-start).

## Security model

The TC002 follows the ESP32 model of
[`SECURITY.md`](https://github.com/Blueforcer/awtrix-ng/blob/main/SECURITY.md): plain HTTP on port
80 of a trusted LAN, the API open until the Basic login is turned on, Berry scripts trusted.
`SECURITY.md` states the user-facing summary; this section is its source.

**Trust boundaries.**

- USB access is root access: USB ADB gives root to anyone with the cable. Physical access is out
  of scope.
- `awtrix-tc002d` and `awtrix-linux` run as root. The runtime needs root only for port 80 and the
  device descriptors it already receives from the daemon.
- The manufacturer's kernel, boot chain and SoC modules are trusted as they are; fixes in them
  are out of reach without an own kernel and root filesystem.

**Network exposure.**

- ADB over Wi-Fi is closed before anything associates: `WifiService` sets `service.adb.tcp.port=-1`,
  restarts `adbd` and refuses to start the supplicant while a port may still listen. The developer
  flag `keep-adb-tcp` is the only exception and warns every hour.
- Without a saved Wi-Fi network, or when the saved one does not connect, the daemon opens an
  unencrypted setup access point named after the hostname, with the clock at `192.168.4.1`, a
  DHCP server and captive DNS. Anyone in radio range can then reach the web UI and set Wi-Fi until
  the clock joins a network. Wi-Fi can also be set over USB or from the web UI of a connected
  clock. The credential record holds the SSID and the PSK derived from the passphrase, never the
  passphrase itself, and `tc002_wifi.py` never takes it from the command line.
- The release's aic8800 driver writes no key material to the kernel log (patch
  `0003-never-log-key-material.patch`, see [Wi-Fi driver](wifi-driver.md)), and the daemon sets
  `aicwf_dbg_level=0`.
- The vendor Bluetooth setup is disabled. AWTRIX NG offers an on-demand BLE service for trusted
  Berry scripts. Its capabilities and encryption requirements are in the [BLE guide](../../guides/ble.md).

**The vendor app.** It starts only with the knob held at power-on or for a pending vendor update,
and `secure-vendor` keeps it offline. The reset key (5 s) restores the Ulanzi firmware and wipes
`/data`, which brings the full vendor app back; a clock that must never reach the vendor cloud
therefore also needs a block in the router.

**Updates.** Packages are unsigned and the operator chooses their source. Manifest and payload
checksums, the target `awtrix-ng:tc002` and a counter above the accepted one are required (no
downgrade over the web). A release whose runtime never becomes healthy ends in rescue after three
starts, from where `deploy` over USB writes a working one.

**Crash handling.** `kernel.panic=5` and `kernel.panic_on_oops=1` turn a kernel crash into a reboot,
and the loader's start counter turns a crash loop into rescue. A bus or CPU hang is not caught: AWTRIX
NG feeds no hardware watchdog.

**In scope** on the TC002, besides the ESP32 list: bypassing package integrity, target checks or
the accepted release counter; writing the release slot with an image the package checks refused;
reaching the Wi-Fi key over the network. **Out of scope:** anything that needs the USB cable.

## Design decisions

| Decision | Why |
|---|---|
| The loader in `res` starts AWTRIX; the vendor app runs only with the knob held or for a pending vendor update, and rescue replaces every automatic fallback to it | the clock stays off the vendor cloud unless its owner chooses the vendor app |
| The release lives in a slot behind the res squashfs, mounted read-only through our own `loop.ko`; there is no factory release, and a failed update ends in rescue with USB RECOVERY and USB ADB | `/data` keeps about 4 MB more for MP3s, icons and scripts, and a release is written and verified as one image; repairing a failed update over USB is acceptable |
| The Assist WebSocket uses OpenSSL | TLS uses the runtime CA store |
| The speaker is `awtrix_pcm` on the `mhal` exports, not an own BACH (audio block) driver | the stock system loads `mhal` whatever the release does, so replacing the backend alone gains nothing |
| The release ships the aic8800 pair rebuilt from source and never loads the stock pair | a driver whose source, build and logging AWTRIX NG controls; it never logs key material |
| No SigmaStar MI userspace code in the repository | the speaker and the panel need none of it |
| AWTRIX NG writes only `res` and `/data` | every other partition stays as the manufacturer shipped it |
| The terminal-style boot intro plays once per power-on, with `assets/tc002/boot.mp3` | shows startup before the app rotation begins |
| The TC002's id is the Wi-Fi MAC, kept by the daemon | the same id as the ESP32, the hostname and mDNS; a factory reset keeps it |

## Related

- [Tools overview](tools.md), [Install tools](install-tools.md),
  [Desktop installer](desktop-installer.md), [Browser image tools](browser-image.md)
- [Kernel modules](kernel.md), [Wi-Fi driver](wifi-driver.md), [Speaker driver](speaker-driver.md),
  [Bluetooth](bluetooth.md)
- [CA certificates](ca-certificates.md), [OpenSSL](openssl.md), [wpa_supplicant](wpa-supplicant.md)
- [MCU updates](mcu-update.md), [Microphone](microphone.md), [Audio gate](audio-gate.md),
  [Home Assistant Voice](ha-voice.md)
- [AWTRIX on Linux](../linux/index.md), [Update packages](../linux/updates.md)
- User docs: [Ulanzi TC002](../../getting-started/tc002.md),
  [Updating firmware](../../guides/updating.md#tc002-updates),
  [Reset & recovery](../../guides/reset-recovery.md)
- Earlier community work on the TC002:
  [atomicstack/tc002-customisation](https://github.com/atomicstack/tc002-customisation) (access to
  the stock system and a replacement application environment),
  [Daniel-Nashed/tc002-tools](https://github.com/Daniel-Nashed/tc002-tools) (device-side
  experiments and diagnostics).
