---
only: [tc002]
---

# TC002 microphone

The TC002's microphone is wired to the MCU (microcontroller), not to the SoC. AWTRIX NG reads it
over the MCU's UART in two ways: bounded *snapshots* and a *continuous stream*. The
[microphone hub](#microphone-hub) shares the stream between [Home Assistant Voice](ha-voice.md),
the [wake word](wake-word.md) and the scripts' [voice module](voice-module.md). The audio visualisation (`music.bands()`, `music.level()`) reads
that stream while it runs, and snapshots otherwise. Both ways need the AWTRIX MCU extension
([MCU updates](mcu-update.md)). The analysis layer is shared with playback and lives in
[`src/core/audio/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/core/audio). The TC002
side is `MicrophoneInput` and `MicrophoneHub` in `src/platform/tc002/runtime/`, `McuService`,
`McuPcm` and `McuStream` in `src/platform/tc002/daemon/`, and the messages in
`src/platform/tc002/contract/`.

## Choosing the source

System > Audio > Music source in the web UI sets `musicSource`: `auto` (default), `playback` or
`microphone`. `auto` analyses playback while something plays and otherwise asks for microphone
audio. The selector appears only on devices that report the `microphone` capability.
No recording is saved or sent over the network by this feature.

Scripts use the same `music.bands()` and `music.level()` for both inputs. Microphone levels and
bands come from signed PCM samples through the same `SpectrumAnalyzer` as playback. Live microphone
input uses a fixed scale of -54 to -18 dBFS with the DC offset removed, so a quiet room never grows
towards full scale. Playback keeps its adaptive scaling. `music.playing()` keeps its playback
meaning and does not start the microphone, so a microphone visualisation must not use it as a
`should_show()` guard.

`music.pitch()` follows `musicSource` too. `musicPitch()` (`runtime/MusicPitch.h`) picks the
source as `AnalysisRouter` does: `auto` takes playback while a playback window was heard
within 300 ms, otherwise the microphone; the microphone is asked only when it is the one chosen.
Both answer the fundamental found by `PitchDetector` (`core/audio/`, YIN, 70 to 1600 Hz), and a
window older than 300 ms answers `0.0`.

- **Microphone.** The first call from `draw()` starts the snapshot requests on its own; calls
  from anywhere else answer from the newest window and request none. The newest window is analysed
  at most once and only while a script asks. While the microphone hub has a stream, the windows
  come from that stream and no snapshot is requested.
- **Playback.** `PlaybackPitch` (`src/platform/tc002/audio/`) sits on the speaker's analysis tap.
  While a script asked within the last second, the audio thread folds each decoded block to mono
  at no more than 16 kHz (44.1 kHz becomes 14.7 kHz), detects one 1,024-sample window at a time and
  stamps it with when its first sample is heard. The render thread reads the window heard now from
  a 16-slot seqlock ring, so the pitch matches the sound like the bands do.

The ESP32 builds have no pitch source; `music.pitch()` answers `0.0` there.

A source-independent visualisation needs only the normal script API:

```berry
# @name Audio Spectrum
class AudioSpectrum
  def draw()
    bar_chart(music.bands(16, 8), "Rainbow", false)
  end
end
return AudioSpectrum()
```

The [Spectrum example](https://github.com/Blueforcer/awtrix-ng/blob/main/docs/examples/spectrum.ax)
adds five palettes, a configurable bar fall time and optional peak and beat markers. It has no
playback-only visibility guard.

## Layers

- **Analysis** (`AnalysisSource.h`, `PcmAnalysis.h`, `AnalysisEnvelope.h`). `IAnalysisSource` and
  `AnalysisRouter` are hardware-independent: playback and capture adapters implement the same
  contract, and source selection stays outside scripts, the FFT and the drivers. `PcmView`
  (`core/audio/Pcm.h`) describes one borrowed interleaved PCM16 block with its frame count, channel
  count and sample rate; blocks do not imply gapless capture. `PcmAnalysis` owns the FFT and scaling
  state and a 500 ms analysis cache. Adaptive gain is kept between nearby windows and resets after a
  source change or a long gap. `AnalysisEnvelope` smooths display values by elapsed time (45 ms
  attack, 220 ms release), independent of the frame rate. Beat events stay discrete.
- **Runtime adapter** (`MicrophoneInput`). Adapts the supervisor channel to `IListeningSource`. An
  analysis call expresses demand; `heard()` answers from the cache without it. It keeps one request
  in flight, at least 100 ms apart, and drops stale or superseded replies. A failed request keeps the last valid window until its normal
  expiry, so a brief conflict with a power query neither blanks the visualisation nor resets the
  gain. While the microphone hub has a stream, it sends no snapshot requests. It gathers the
  stream's blocks into windows of 1,056 samples instead, and a gap in the stream starts a new window.
- **Supervisor** (`RuntimeChild`). Checks and rate-limits the runtime's requests. A generation
  check keeps replies from reaching a restarted runtime. Each request has an id; the reply carries
  either a complete PCM window or an error. One reply is kept across channel backpressure,
  separately from the replaceable device-state snapshots, so extra requests cannot displace it or
  grow a queue.
- **UART owner** (`McuService`, `mcu::PcmCapture`, `mcu::Stream`). `McuService` alone owns the UART.
  The capture code validates framing, start and end metadata, sequence numbers, parts, alternating
  DMA halves and the exact length. Capture and MCU firmware transfer never overlap. Battery polling
  resumes after a capture, with a fresh USB check before any firmware update. Empty reads on the raw
  tty (`VMIN=0`) count as idle; poll errors and real hangups still close the UART. A Linux update
  drains a running capture during shutdown; an MCU firmware operation blocks the update hand-off.

Snapshot capture happens only while an app on the panel asks for analysis. The bindings tell the
platform whether a reading comes from `draw()` (`onScreen` in `ScriptServices::audioStats`, the
canvas in `ScriptExtensionHost` for `music.pitch()`), and `AnalysisRouter` lets the microphone listen
only then. `loop()`, timers and handlers read what an app on the panel made it hear, so an app that
is hidden or headless never starts a capture. The hub's stream follows the same rule: its windows
reach apps only up to 200 ms after a reading from `draw()`, even though the wake word keeps the
stream running. `playback` never falls back to the microphone. Switching away drops cached and pending audio; an MCU capture already under way
finishes without being published.

## Snapshot capture

The messages `microphonePcmRequest` and `microphonePcm` carry fixed windows of 1,056 mono samples
at a nominal 16 kHz (66 ms), little-endian PCM16, in one bounded local datagram. The MCU is asked
for 44 DMA halves of 24 samples. Incomplete or corrupt windows never reach the FFT; the receiver
drains through the terminal frame or a 5 s deadline before it releases the UART.

Snapshots are separate windows, not a continuous signal. Never concatenate them and call the result
continuous audio.

The MCU also scans the panel. The extension keeps the panel scan running during a capture, so the
panel brightness does not change; this scheduling belongs in the MCU, not in the visualisation.

## Continuous stream

MCU build 13 and later (feature bit `0x08`) also offer the S4 stream: acquisition runs into a ring
of four DMA halves while the UART alternates between audio data and short control windows.
`McuStream` runs one stream transaction inside `McuService`, and the runtime receives owned
blocks of at most 480 samples (30 ms) as `microphoneStreamEvent`, each with its epoch, sample
position and host timestamp. The runtime controls it with `microphoneStreamControl` (probe, start,
stop, keepalive). The protocol and the hardware measurements that qualify it are in
[Audio gate](audio-gate.md). In the runtime, the microphone hub is the stream's only user.

## Microphone hub

`MicrophoneHub` (`runtime/MicrophoneHub.h`) shares the daemon's one stream among the runtime's
consumers: the [wake word](wake-word.md) listener, [Home Assistant Voice](ha-voice.md) and
`VoiceCapture`, the recorder of `import voice` ([Voice module](voice-module.md)). Each
consumer speaks the protocol of `contract/MicrophoneStream.h` as it would to the daemon, under its
own epochs. Its sample positions are its own and start at 0 with its stream. Like the daemon, the
hub never answers inside a control call. The answer arrives on its next `receive()` or `tick()`.

The wake word listener, `VoiceCapture` and Home Assistant Voice speak it through `MicrophoneClient`
(`contract/MicrophoneClient.h`). The client counts the epochs, sends a keepalive every 150 ms, and
checks that the positions follow on. `open()` probes and then starts, as the listener and
`VoiceCapture` do. Home Assistant Voice takes the two steps apart: `probe()` and `probeAgain()` only
ask whether the stream exists and have no deadline, and `start()` starts without a probe. The probe
of `open()`, a start and a stop each get 2 s (`kStreamAnswerMs`) for their answer. An unanswered probe
or start is stopped and reported as ended with `microphone did not answer`, an unanswered stop counts
as done. The client reports available or unavailable, started, audio, a gap, an end nobody asked for
and a stop it asked for.

- **Upstream.** The hub probes and starts the daemon's stream once, under its own epoch, and renews
  the daemon's lease with a keepalive every 150 ms. The daemon must answer a probe or a start within
  2 s. While the hub has a stream, it answers a consumer's probe itself.
- **Consumer lease.** Each consumer must renew its stream within 2 s (`kStreamLeaseMs`). A consumer
  that does not gets an end with the error `consumer lease expired`, and the others stream on. The
  daemon gives the hub the same 2 s, so a short stall of the main loop, such as the compile of an
  `@ondemand` assistant, ends nothing.
- **Linger.** Once the upstream stream runs, it runs on for 1 s after the last consumer stops. A
  consumer that starts within that second joins it. If the last consumer stops while the stream is
  still starting, the hub stops it at once. A start while the upstream stream stops starts a new one
  once the old one has ended, without probing again.
- **Hold.** The assistant router ([Voice module](voice-module.md#parts)) holds the hub (`hold()`)
  while Voice starts, while a trigger waits for its assistant app, and while a recording of the
  voice module starts. A starting or running stream then
  stays open without a consumer until the capture joins it, and lingers from the release. A hold
  opens no stream.
- **Lead-in.** While a stream runs, `SampleHistory` (`runtime/SampleHistory.h`) holds its last second
  in memory: 16 008 samples, 667 DMA halves of 24, because every block and position of the stream is a
  whole number of halves. The hub overwrites them continuously, drops them when the stream ends and
  writes none to a file.
  `mark()` names the sample after the latest block, `markOf()` turns a consumer's own position into
  a mark, and `startFrom()` makes a consumer's next start begin at a mark, or at the oldest held
  sample when the mark lies further back. A mark between two halves, such as the position of a wake
  word detection, starts at the half it falls in. A mark of another stream starts the consumer now. A gap
  in the stream drops what is held. The held blocks arrive at once after Started, with `hostAtMs`
  0. A recording of the voice module from a trigger starts at the trigger's mark. Voice uses no
  marks and keeps its check of 250 ms.
- **Gaps and failures.** A jump in the upstream sample position reaches each consumer as a jump in
  its own position. An upstream error, an MCU without the stream or a missing answer ends every
  consumer that wants a stream with that error. So does a supervisor link that refuses the hub's
  probe or start. A stream that delivers no audio for 2 s (`kStreamAnswerMs`), for example because the
  daemon revoked it, is asked to stop and ends every such consumer with `microphone stream stalled`.
  The runtime polls the link before it ticks the hub, so audio queued during a long frame counts
  first. The hub does not retry. Each consumer decides when to try again.

A consumer attaches with `attach()` and gets a `MicrophoneHub::Consumer` handle. The handle gives
its `port()`, `startFrom()` and `markOf()`, and detaches the consumer when it is destroyed. A block
reaches its consumers by reference, with each consumer's epoch and position filled in, so a consumer
that keeps samples copies them. `VoiceStack` (`voice/VoiceStack.h`) owns the hub and the handles: its
constructor attaches the wake word and Voice, and `composeScripting` attaches the voice module.
`receive()` passes every `microphoneStreamEvent` to the hub. `tick()` ticks the hub every frame, also
while idle, before Voice, the voice module and the wake word. `stop()` closes it (`close()`), which
stops the upstream stream at once, without lingering. From a consumer's start until the upstream
stream has ended, the hub is active (`active()`) and `MicrophoneInput` requests no snapshots. A probe alone leaves
the snapshots running. Every upstream audio block also goes to `MicrophoneInput`, which gathers the
windows for `music.level()`, `music.bands()` and `music.pitch()` from it.
`tests/tc002/runtime/test_microphone_hub.cpp` covers the hub, `test_sample_history.cpp` its held
second, `tests/tc002/contract/test_microphone_client.cpp` the client, `test_voice_on_hub.cpp` a Voice
session on the hub, and `test_voice_capture.cpp` a recording of the voice module.

## Adding an input or a consumer

A new visualisation input implements `IAnalysisSource`, optionally with `PcmAnalysis`; scripts and
`SpectrumAnalyzer` need no change. A new PCM consumer belongs before the analysis and consumes
validated blocks from the input adapter, never samples reconstructed from spectrum bands. A new
consumer of the continuous stream attaches to the [microphone hub](#microphone-hub).

## Related

- [TC002 developer guide](index.md)
- [MCU updates](mcu-update.md), [Audio gate](audio-gate.md), [Home Assistant Voice](ha-voice.md),
  [Wake word](wake-word.md), [Voice module](voice-module.md)
- User docs: [Scripting](../../guides/scripting/index.md), [Settings](../../reference/settings.md)
