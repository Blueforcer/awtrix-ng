---
only: [tc002]
---

# TC002 microphone

The TC002's microphone is wired to the MCU (microcontroller), not to the SoC. AWTRIX NG reads it
over the MCU's UART in two ways: bounded *snapshots* feed the audio visualisation (`music.bands()`,
`music.level()`), and a *continuous stream* feeds [Home Assistant Voice](ha-voice.md). Both need
the AWTRIX MCU extension ([MCU updates](mcu-update.md)). The analysis layer is shared with playback
and lives in [`src/core/audio/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/core/audio);
the TC002 side is `MicrophoneInput` in `src/platform/tc002/runtime/`, `McuService`, `McuPcm` and
`McuStream` in `src/platform/tc002/daemon/`, and the messages in `src/platform/tc002/contract/`.

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

- **Microphone.** The first call starts the snapshot requests on its own; the newest window is
  analysed at most once and only while a script asks. Any time while Voice streams it answers
  `0.0`.
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
- **Runtime adapter** (`MicrophoneInput`). Adapts the supervisor channel to `IAnalysisSource`. An
  analysis call expresses demand. It keeps one request in flight, at least 100 ms apart, and drops
  stale or superseded replies. A failed request keeps the last valid window until its normal
  expiry, so a brief conflict with a power query neither blanks the visualisation nor resets the
  gain. While Voice streams, it sends no snapshot requests.
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

Microphone capture happens only while a consumer asks for analysis. `playback` never falls back to
the microphone. Switching away drops cached and pending audio; an MCU capture already under way
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
[Audio gate](audio-gate.md); its only consumer is [Home Assistant Voice](ha-voice.md).

## Adding an input or a consumer

A new visualisation input implements `IAnalysisSource`, optionally with `PcmAnalysis`; scripts and
`SpectrumAnalyzer` need no change. A new PCM consumer belongs before the analysis and consumes
validated blocks from the input adapter, never samples reconstructed from spectrum bands.

## Related

- [TC002 developer guide](index.md)
- [MCU updates](mcu-update.md), [Audio gate](audio-gate.md), [Home Assistant Voice](ha-voice.md)
- User docs: [Scripting](../../guides/scripting/index.md), [Settings](../../reference/settings.md)
