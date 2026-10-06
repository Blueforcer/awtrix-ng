---
only: [tc002]
---

# TC002 speaker driver

`awtrix_pcm.ko` is a GPL kernel module that drives the TC002 speaker through the `MHAL_AUDIO_*`
functions exported by the stock kernel module `mhal.ko`. Its userspace side is the static musl
helper `awtrix-tc002-audio-pcm`. No closed userspace library is involved. The module lives in
[`src/platform/tc002/kmod/awtrix_pcm/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/kmod/awtrix_pcm),
the helper in
[`src/platform/tc002/audio/pcm/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/audio/pcm),
and the build recipe is
[`tools/tc002/kernel/build-awtrix-pcm.sh`](https://github.com/Blueforcer/awtrix-ng/blob/main/tools/tc002/kernel/build-awtrix-pcm.sh).

## How it works

### Speaker backend selection

The release carries `bin/awtrix-tc002-audio-pcm` and `lib/modules/awtrix_pcm.ko`. At its start,
`awtrix-tc002d` loads `lib/modules/awtrix_pcm.ko` of the mounted release
(`/tmp/awtrix-release`) with `finit_module` in a short-lived child, unless `/proc/modules` already
lists awtrix_pcm. The module must be a regular file owned by root that group and others cannot
write. Loading takes 10–40 ms.

The daemon then starts `awtrix-tc002-audio-pcm` next to every runtime, with the runtime's end of a
`SOCK_SEQPACKET` socket on fd 104. The helper opens `/dev/awtrix_pcm` once and keeps it open,
muted between sounds. Volume follows a curve from −60 to −3 dB, and every sound ends with `STOP`
once only silence is left. The helper uses about 0.24 MB of RAM.

The daemon logs the outcome as `speaker backend: …`, and `ctl status` shows it under `audio`
(`backend`, `backendNote`, `state`, `lastExit`):

| Case | `backend` | Speaker until the next boot |
|---|---|---|
| module loaded (`awtrix_pcm loaded in … ms`) or already listed | `pcm` | awtrix_pcm helper |
| while the module loads | `loading` | none yet |
| the helper or the module is missing, the module is writable by others or owned by someone else, `/proc/modules` is unreadable, the load fails, or it has not finished after 10 s | `off`, the reason in `backendNote` | none |
| the helper exits with anything but 0 or is killed (`lastExit`, e.g. `exit 3 (preflight refused)`) | `off` | none |
| no speaker configured | `none` | none |

The display and the rest of the runtime keep running in every case.

### Output ownership

The MHAL output device 0 has exactly one owner at a time.

1. `awtrix-tc002d` runs one speaker helper at a time and starts the next one only after the
   previous one was reaped. Before it opens `/dev/awtrix_pcm`, `awtrix-tc002-audio-pcm` refuses
   (exit 3) while the stock GUI host runs or `/proc/interrupts` shows an `aio_dma` interrupt.
   mhal requests `aio_dma2` in `OpenPcmOut` and frees it in `ClosePcmOut`, so the interrupt is
   present exactly while some owner has the output open.
2. Loading the module touches no audio hardware; only `open()` does.
3. Only `awtrix-tc002d` loads the module. The vendor app never runs the daemon, so it never loads
   or opens awtrix_pcm.
4. `awtrix_pcm` refuses a second opener with `EBUSY`. If mhal refuses `ConfigPcmOut` because
   another owner has the output open, `open()` also fails with `EBUSY` and nothing else is
   touched.
5. mhal is `[permanent]` and awtrix_pcm sits on top of it. The daemon never unloads awtrix_pcm, so
   it stays loaded until the next boot.

## Interface

`/dev/awtrix_pcm` is a misc character device (mode 0600, dynamic minor). It accepts one opener at
a time.

- **Format:** `write()` takes s16le mono PCM at 44 100 Hz. The byte count must be even.
  Resampling stays in userspace.
- **Backpressure:** `write()` blocks until the DMA ring has room. With `O_NONBLOCK` it returns a
  partial count or `EAGAIN`. `poll()` reports `POLLOUT` once a full period is free, and `POLLERR`
  after a HAL failure. Once a HAL call has failed, the device stays failed until it is closed.
- **Start:** playback starts once `start_bytes` are queued. `AWTRIX_PCM_DRAIN` and `close()` also
  start a stream that is shorter than that.
- **Close:** `close()` waits for the queue to play out, at most `buffer + 250 ms`, and not at all
  when the process is being killed. Then the module fades the gain to −64 dB, stops the DMA and
  closes the MHAL output, which switches the amplifier off.
- **Underruns:** when the queue runs empty, mhal stops the DMA and the next write restarts it.
  Idle gaps count as underruns too. When the DMA level stops moving for `buffer + 250 ms` while the
  ring is full, the module logs a warning and restarts the stream.

### ioctls

`awtrix_pcm.h` defines them.

| ioctl | Effect |
|---|---|
| `SET_GAIN` | Gain in dB, clamped to `[−64, max_gain_db]`. |
| `SET_MUTE` | Sets the gain to −64 dB and writes zeros for any data sent while muted. |
| `DRAIN` | Plays out what is queued. |
| `STOP` | Fades out and discards the queue. Writes after it start a new stream. |
| `GET_STATUS` | State, written and queued bytes, underruns, gain, the last HAL error and the measured consumption rate. |

### Module parameters

Read-only after load. Byte values count DMA bytes, which is twice the input bytes when
`channels=2`.

| Parameter | Default | Meaning |
|---|---|---|
| `ao_dev` | 0 | MHAL output device. 0 is line out, the speaker |
| `channels` | 1 | 1: mono DMA (HAL mono mode). 2: each sample duplicated into a stereo frame (the fallback if mono mode misbehaves) |
| `buffer_bytes` | 16384 | DMA ring, 186 ms |
| `period_bytes` | 2048 | transfer unit, 23 ms |
| `start_bytes` | 4096 | prefill before start, 46 ms |
| `initial_gain_db` | −28 | gain after open |
| `max_gain_db` | −3 | highest gain a client may set; the helper's volume curve ends at −3 dB |

### MHAL call sequence

| When | Calls |
|---|---|
| `insmod` | None. It only allocates the 16 KiB DMA ring with `dma_alloc_coherent` from CMA; MIU address = physical − `0x20000000` |
| `open` | `Init(NULL)` → `SetGainOut(−64, ch 0 and 1)` → `ConfigPcmOut(0, cfg)` → `OpenPcmOut(0)`, then the DMA level must read 0 |
| `write` | `GetPcmOutCurrDataLen`, then `WriteDataOut(…, block=0)` with multiples of 16 bytes and never more than free space − 256 bytes. Until the stream starts, the DMA level must equal the bytes written. At `start_bytes`: `SetGainOut(target)` → `StartPcmOut` |
| underrun | `WriteDataOut` fails while running → `PrepareToRestartPcmOut` → one retry |
| drain end, `STOP` | `SetGainOut(−64)` → `PrepareToRestartPcmOut` |
| `close` | `SetGainOut(−64)` → `StopPcmOut` → `ClosePcmOut` |

The module never calls `DeInit`, `AmpEnable` or `SetDacMute`. It also skips `IsPcmOutXrun` and every AI
(audio input) function, and it touches no device other than `ao_dev`.

`GetPcmOutCurrDataLen` and `StopPcmOut` dereference the HAL's per-DMA runtime data, which exists
only between `OpenPcmOut` and `ClosePcmOut`, so the module calls these two only after a
successful open.

## Design basis

The module is written from GPL sources: the SigmaStar audio driver in
`OpenIPC/u-boot-sigmastar@bf77aff5` (`drivers/mstar/aio`, whose `audio_ut_module.c` is the template
for the call order) and `mhal_common.h` of the Pioneer3 kernel `e53dccbc`. `BUILD_BUG_ON` pins the
40-byte layout of `MHAL_AUDIO_PcmCfg_t`. The build checks every import against the kernel and
`mhal` exports ([Build](#build)).

## Build

```sh
TC002_STOCK_MODULES=<dir with stock mhal.ko, aic8800_*.ko> \
TC002_STOCK_IMAGE=<decompressed stock Image> \
tools/tc002/kernel/build-awtrix-pcm.sh [out-dir]
```

The output directory defaults to `$TC002_CACHE/awtrix_pcm`. The script needs the tree from
`prepare-tree.sh` (see [Kernel modules](kernel.md)), runs `build-module.sh`, and checks that:

- `check-ko.sh` passes;
- every import is exported either by the kernel (`Module.symvers`) or by `mhal.symvers`;
- the MHAL imports are exactly the ten in `mhal.symvers`;
- `depends=mhal`;
- with the private stock files present, the MHAL imports are in the stock `mhal.ko` export table
  and the kernel imports are in the stock `Image` export table.

It also builds `awtrix-pcm-ctl` (static musl) and writes two test tones, both 440 Hz at −12 dBFS
with 10 ms fades: `tone-440hz-300ms.raw` (26 464 bytes) and `tone-440hz-3s.raw`. The build is
reproducible: the same source gives the same `awtrix_pcm.ko` SHA-256 in any directory, since the
module carries no build ID.

The helper `awtrix-tc002-audio-pcm` is a CMake target: the host build runs its contracts
(`tc002-audio-*`), and the `tc002` preset builds the static musl binary and runs them again under
qemu-arm (`tc002-arm-*`).

## Checking on a clock

The module reports on the kernel log (`dmesg`):

- On load: `awtrix_pcm: ready: ao_dev 0, 1 channel(s), buffer 16384, period 2048, start 4096
  bytes, dma 0x2… (MIU 0x0…)`, and a node `crw------- 10,<minor>` at `/dev/awtrix_pcm`.
- At the first stream start: `DMA level 2048 after 2048 prepared bytes`.
- On close: `closed: <bytes> bytes written, <n> underruns, measured ≈88200 B/s (nominal 88200),
  hal error 0`. After the close, `/proc/interrupts` shows no `aio_dma2` line.

`awtrix-pcm-ctl play FILE [GAIN_DB [STOP_AFTER_MS]]` plays a raw s16le mono 44.1 kHz file such as
`tone-440hz-3s.raw` directly on the device node. Since the device takes one opener at a time, this
works only while no speaker helper holds `/dev/awtrix_pcm`.

| Log line | Meaning |
|---|---|
| `Unknown symbol` at load | mhal is not loaded, or its exports differ |
| `ENOMEM` at load | no CMA memory for the DMA ring |
| `ConfigPcmOut refused (…): output 0 busy or configuration rejected` | another owner holds the output |
| `WriteDataOut failed (-2)` | the PCM config layout does not match `mhal` |
| `DMA level does not match the written bytes, not starting` | the DMA level counter behaves differently; the DMA never ran |
| `OpenPcmOut failed` | mhal could not open the output |
| measured rate near 176 400 or 96 000 B/s | stereo semantics (try `channels=2`) or 48 kHz |
| `DMA level stuck at … bytes, restarting the stream` | the DMA stopped moving; the module restarted it |

In every failure at `open` or before the first start, the module has closed the output again and
the DMA never ran. `write()` sleeps interruptibly, so no process waits in the `D` state.
`panic=5` does not catch a bus hang, which a BACH (audio block) register access while the audio
clock is off would cause; the module therefore uses only MHAL entry points.

### Through the AWTRIX runtime

Precondition: `awtrix-tc002d ctl status` shows `"backend":"pcm"` and `"state":"running"` under
`audio`. Then:

- `GET /api/v1/capabilities` reports `rtttl`, `mp3` and `radio` as `true` under `audio`.
- `POST /api/v1/audio/play {"rtttl":"t:d=8,o=5,b=180:c,e,g,c6"}` plays four rising notes at the
  alert volume.
- Upload a short MP3 in the web UI, then `POST /api/v1/audio/play {"file":"<name>"}`: it plays at
  the alert volume; `GET /api/v1/audio` shows `alert.playing` `true` while it plays and `false`
  afterwards.
- `POST /api/v1/audio/play {"station":"http://LAN_HOST/STREAM.mp3"}`: a plain HTTP MP3 station starts
  within about a second, and its title appears in the state. A melody played meanwhile interrupts
  it, and the station comes back afterwards. Decoding takes about 1.7 ms per 26 ms frame.
- `POST /api/v1/audio/stop {}`: silence within about 0.1 s.
- Changing `volume`, `alertVolume` or `radioVolume` while playing changes the loudness. A heard
  level of 100 (`volume` and the group both at 100) is −3 dB, the top of the helper's volume
  curve.
- After every sound, the idle speaker is silent: no hiss, hum or buzz.
- Killing the helper leaves the display running and logs `TC002 speaker unavailable`; the speaker
  then stays off until the next boot (`"backend":"off"`).

## Related

- [Kernel modules](kernel.md)
- [Microphone](microphone.md)
- [TC002 overview](index.md)
- [TC002 tools](tools.md)
