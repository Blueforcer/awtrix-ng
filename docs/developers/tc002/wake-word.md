---
only: [tc002]
---

# TC002 wake word

The TC002 can start [Home Assistant Voice](ha-voice.md) when it hears "Hey AWTRIX". The clock
detects the word itself: it runs a [microWakeWord](https://github.com/kahrendt/microWakeWord) v2
streaming model on the continuous microphone stream ([Continuous microphone audio](audio-gate.md)),
and nothing leaves the clock before a detection. The code is in
[`src/platform/tc002/wake/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/wake).
The ESP32 builds have none of it. Setup for end users is in the
[voice guide](../../guides/voice.md#start-it-with-hey-awtrix).

## What it runs

| Part | File | Role |
|---|---|---|
| Frontend | `WakeFrontend` | 16 kHz PCM16 in, one frame of 40 features every 10 ms (30 ms window). A port of the TFLite Micro `micro_speech` frontend, with noise reduction and PCAN gain control. Its FFT, `WakeFft`, is a port of KISS FFT's 16-bit real FFT. Its float tables are data (`WakeFrontendTables.h`, generated with glibc), so the clock never depends on musl's maths. |
| Model | `WakeModel` | Loads the int8 `.tflite` with a bounds-checked flatbuffer reader, plans one static arena at load and runs the model with no allocation. The kernels follow TFLite's reference integer arithmetic. |
| Stream | `ModelStream` | Runs a model on PCM: the frontend's features, quantised as the trainer fed them, one invoke every stride. The detector and the endpointer each own one. |
| Detector | `WakeDetector` | Averages the last `window` outputs and reports a detection when the mean reaches `cutoff`. After a detection it waits 1 s of audio. In the first second after a reset nothing fires: the frontend's noise estimate starts at zero, so its first frames look loud. |
| Listener | `WakeListener` | Runs its stream through the [microphone hub](microphone.md#microphone-hub), owns a worker thread for the detector, and reports each detection once to the runtime. |

The model must match the microWakeWord contract: input `[1, N, 40]` int8, one uint8 output. The
operators CONV_2D, DEPTHWISE_CONV_2D, FULLY_CONNECTED, AVERAGE_POOL_2D, MAX_POOL_2D, LOGISTIC,
QUANTIZE, ADD, MUL, MEAN, CONCATENATION, STRIDED_SLICE, SPLIT_V, RESHAPE and the state operators
(CALL_ONCE, VAR_HANDLE, READ_VARIABLE, ASSIGN_VARIABLE) are supported. Any other operator, a custom
operator or a malformed file is refused at load with a reason.

Measured cost: the shipped model of 47 KB needs an arena of about 9 KB. On the clock, detection
takes about 6 to 7 % of one CPU core, and the frame rate stays at 42 fps.

## The model

The release carries the model as `share/wake/hey_awtrix.tflite`, from
[`assets/tc002/wake/`](https://github.com/Blueforcer/awtrix-ng/tree/main/assets/tc002/wake). Beside
it, `hey_awtrix.json` is the trainer's `custom.json`; the listener reads its `probability_cutoff`
and `sliding_window_size` and ignores the other keys. `readModelFiles` reads both files; it also
takes the ESPHome model manifest, which keeps the two keys under `micro`, and refuses a
`feature_step_size` other than the frontend's 10 ms. The daemon passes the model to the runtime as
`--wake-word FILE` when the file exists and the runtime gets the speaker. A runtime whose model or
settings cannot be used writes `wake word: …` to the log and runs without the wake word. Its
`GET /api/v1/voice` then answers `wakeWordAvailable: false`, and the web UI hides the Wake word
switch.

To ship another model, replace both files with the trainer's `model.tflite` and `custom.json` under
these names. The bundle tools accept the two files only together.

## How it listens

- The setting is `wakeWord` in the Voice configuration (`voice-private/voice.json`), off by
  default. The web UI switch and `POST /api/v1/voice` change it; see
  [Credentials and configuration API](ha-voice.md#credentials-and-configuration-api).
- The listener streams while the wake word is on and Voice is on, connected and idle. The boot
  intro, an update and setup mode pause it, as they pause Voice. It listens through the
  [microphone hub](microphone.md#microphone-hub), which shares the stream with Voice. Its epochs
  and sample positions are its own.
- A detection starts Voice exactly as the knob's 500 ms hold does (`startVoice`). Voice's overlay
  appears at once. Voice is then busy, so the listener stops its stream. The hub keeps the stream
  open until Voice's capture joins it. Listening resumes when Voice is done.
- A detection that arrives after listening stopped being wanted is dropped.
- It sends a keepalive every 150 ms. The hub ends the listener's stream when keepalives stop for
  500 ms.
- A stream that ends with an error restarts after 1, 2, 5, 10 and then 30 s. An MCU without the
  stream is asked again after 30 s.
- A jump in the sample position, or a full worker queue (16 blocks), resets the detector.
- While it listens, `music.level()`, `music.bands()` and `music.pitch()` read the hub's stream
  instead of separate snapshots.
- Each detection writes `wake word: heard (score …)` to the log. The listener keeps no audio beyond
  the blocks its worker has not processed yet. The hub passes each block on without keeping it.
  Neither writes audio to a file.

## Speech end detection

`Endpointer` finds where speech in a recording begins and ends. It runs the
[microWakeWord](https://github.com/kahrendt/microWakeWord) voice activity model from
[`assets/tc002/vad/`](https://github.com/Blueforcer/awtrix-ng/tree/main/assets/tc002/vad) on its
own `ModelStream`. The model, its ESPHome manifest `vad.json` and the repository's `NOTICE` are
unchanged from [esphome/micro-wake-word-models](https://github.com/esphome/micro-wake-word-models)
(v2, Apache-2.0).

- **Verdict:** a step of 30 ms has voice when the mean of the last `sliding_window_size` outputs
  reaches `probability_cutoff`, both from `vad.json` (5 and 0.5). Unlike the wake word, it needs no
  warm-up after a reset: on white, pink and brown noise, fan noise and hum from −50 to −20 dBFS, its
  first steps stay near 0. A weak 100 Hz hum at −50 dBFS gave a few voiced steps after about 0.7 s.
- **Decisions** (`EndpointTimer`):
  - The first voiced step starts the speech (`Started`).
  - The speech has ended after `silenceMs` without voice (`Ended`).
  - Without speech by `waitMs`, the recording heard nothing (`Silence`).
  - At `maxMs` it ends in any case (`Limit`).
- **Lead-in:** `start(settings, from)` lets audio before stream position `from` warm up the model
  without counting.
- **Latency:** the model holds its verdict after speech. On calls recorded from the clock, voice began
  about 0.5 s after the speech and ended about 0.45 s after it. A recording therefore ends
  about `silenceMs` + 0.45 s after the last word. Silero VAD v6 ended about 0.28 s sooner at the same
  tolerance for pauses, but needs a float runtime of its own.
- **Cost:** an arena of 2656 bytes and about 14 µs per step on a desktop CPU. The release carries the
  model, `vad.json` and the `NOTICE` as `share/vad/`; the daemon passes it to a runtime with the
  speaker as `--vad FILE`. The bundle tools take the three files only together.

## Training notes

A model whose positive clips carry no background noise or room echo scores real calls low. Train
with noise and room impulse responses, and with recordings from the clock's own microphone. The
microphone delivers speech at about −35 dBFS RMS from a normal distance. The frontend's PCAN evens
out level differences, so the input gain does not change the scores. The shipped model was trained
with 46 real calls from the clock and detected 11 of 15 fresh calls on the clock.

## Checks

`tests/tc002/runtime/test_wake.cpp` compares the frontend and every model output bit for bit with
pymicro_features and the LiteRT reference kernels on committed fixtures (`tests/tc002/runtime/wake/`).
It also checks the detector's window, cutoff, pause and warm-up, and refuses truncated and mutated
files. `tools/tc002/wake/make_golden.py` writes the fixtures. `tests/tc002/arm/` runs the same test
on Cortex-A7 under qemu-arm. `test_wake_listener.cpp` covers the settings file, the stream states,
detections, the stop while Voice is busy, restarts and gaps. `test_assist_session.cpp` covers the
`wakeWord` setting. `test_wake.cpp` also checks the voice activity model bit for bit, and
`test_endpointer.cpp` the endpointer's decisions on exact voice patterns and the model on speech of
the English voice pack, a pause within a sentence and room noise; both run on qemu-arm too.

## Related

- [TC002 developer guide](index.md)
- [Home Assistant Voice](ha-voice.md)
- [Microphone](microphone.md), [Continuous microphone audio](audio-gate.md)
