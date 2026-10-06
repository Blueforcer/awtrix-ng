---
only: [tc002]
---

# TC002 continuous microphone audio

Home Assistant Voice needs gapless microphone audio. The TC002's microphone sits on the MCU
(microcontroller), which also scans the panel and answers power queries over the same UART. This
page describes the continuous capture protocol ("S4") of the AWTRIX MCU extension, the rules that
keep it safe, and the hardware checks required before changing it. The daemon side is `McuStream` in
[`src/platform/tc002/daemon/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/daemon);
the MCU side ships as the extension described in [MCU update](mcu-update.md). Short
snapshots for the audio visualisation are a separate path ([Microphone](microphone.md)).

## Design

**Requirements.** A stream has an explicit start and stop, a session identity (epoch), ordered
sample positions, timing, and observable discontinuities. It stops by itself when the host goes
away. Power queries keep working while it runs. The existing snapshot and identity commands stay
unchanged. The MCU never waits for the UART inside its panel scan.

**Time division.** Acquisition runs continuously into a ring of four DMA halves (24 samples, 1.5 ms
each). The MCU sends audio and pauses its transmission for receive windows of 4 ms, in which the host
may send one control message. Linux answers each window locally on the clock; nothing on a PC is in
that loop. A window that closes without a control message is reported as missed. That costs only
the control opportunity, never audio.

**Wire format.** All messages are MCU command `0x06` frames (`FF 55 06 len payload sum`) with a
16-byte payload from the MCU:

| Payload | Direction | Content |
|---|---|---|
| `S4`, operation 1, epoch u32 | host to MCU | start a stream with a non-zero epoch |
| `S3` | MCU to host | metadata: operation (1 start, 2 end, 3 checkpoint), status, epoch, completed-half count, MCU tick in ms |
| `D3` | MCU to host | audio: half sequence u32, part (0 to 5), physical DMA half (0 or 1), four PCM16 samples |
| `W4` | MCU to host | a receive window opens: token u16, epoch, completed-half count |
| `C4` | host to MCU | the answer inside a window: operation (2 stop, 3 keepalive), epoch, the window's token |
| `A4` / `N4` | MCU to host | the window closes, control accepted (`A4`) or missed (`N4`): token, epoch, USB power, battery byte and word, half count |

**Checks in the daemon** (`mcu::Stream`). Every frame must carry the running epoch. Parts arrive
0 to 5 within one physical half, and consecutive halves alternate between the two DMA buffers.
Window tokens advance by one, and every closure must match the open window. Each checkpoint ties
the sample count to MCU time: at most 3 ms deviation between consecutive checkpoints, and 3 ms plus
0.5 % over the whole stream. Any violation, a non-zero MCU status or an unknown payload ends the
stream with an error; nothing is repaired or retried silently.

**Leases and deadlines.** Three safeguards act independently:

- The MCU stops acquisition by itself when no valid keepalive arrives for 1 s. Invalid or stale
  input cannot renew that lease.
- The daemon ends a stream whose consumer did not renew within 500 ms, and every stream after 60 s.
- A stop must complete within 1.5 s, otherwise the daemon aborts the stream.

A pending stop survives missed windows and is sent in the next one. After an error the daemon sends
no further control and lets the MCU lease expire, while keeping UART ownership until the stream
ended.

**Ownership.** `McuService` remains the only UART owner. Streams, snapshots and firmware transfer
never overlap, and power readings arrive inside the `A4`/`N4` closures while a stream runs. Queues
towards the runtime are bounded ([Home Assistant Voice](ha-voice.md)).

## Why time division

S4 separates the two directions in time so control messages do not compete with audio on the UART.

Windows must also tolerate a late host. Linux delivers some window answers several milliseconds
late; when a late answer is fatal, streams fail after a few seconds. A missed window is therefore
a normal `N4` closure, not an error.

## Qualification before changing the stream path

Repeat the hardware qualification with the production daemon before releasing a changed
stream path. Check these cases:

- A continuous ten-minute capture has no sequence, checksum, DMA or timing discontinuity.
- The panel remains stable at low, medium and full brightness, with capture on and off and
  during speaker playback. Physical observation is required. Screenshots and emulators
  cannot prove optical stability.
- Repeated short and long captures stop cleanly. A new session receives no previous-session data.
- Speaker playback completes normally while microphone capture runs.
- Host loss and missing keepalive stop capture within the documented deadlines.
- A blocked consumer is disconnected at the queue limit, and the next capture succeeds.
- Snapshot capture and normal power queries work after the stream ends.

Bench tools discard audio and record counters and timing. Any recordings stay private.
Run a bench probe only with the normal daemon stopped. Exactly one process owns the UART.
Measure the MCU stack reserve when changing MCU code.

## Related

- [TC002 developer guide](index.md)
- [Microphone](microphone.md), [MCU updates](mcu-update.md), [Home Assistant Voice](ha-voice.md)
