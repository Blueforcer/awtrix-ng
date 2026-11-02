---
only: [tc002]
---

# TC002 wake word

The TC002 can start its voice assistant, [Home Assistant Voice](ha-voice.md) or an app that uses
the [voice module](voice-module.md), when it hears "Hey AWTRIX". The clock
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
| Model | `WakeModel` | One loaded model: its file, one static arena and its graphs. It runs them with no allocation. |
| Loader | `WakeLoader`, `WakeOperators`, `WakeFlat.h`, `WakeGraph.h` | Reads the int8 `.tflite` with a bounds-checked flatbuffer reader, checks the graphs and plans the arena at load. `WakeOperators` checks each operator and works out what its kernel needs. |
| Kernels | `WakeKernels` | The operators. They follow TFLite's reference integer arithmetic. |
| Stream | `ModelStream` | Runs a model on PCM: the frontend's features, quantised as the trainer fed them, one invoke every stride. The detector and the endpointer each own one. |
| Window | `ProbabilityWindow` | Averages the last `window` outputs and says when the mean reaches `cutoff`. It knows nothing of wake words: the detector and the endpointer each own one. |
| Detector | `WakeDetector` | Reports a detection when its window fires, at the cutoff of the sensitivity it was given. After a detection it waits 1 s of audio. In the first second after a reset nothing fires: the frontend's noise estimate starts at zero, so its first frames look loud. |
| Listener | `WakeListener` | Runs its stream through the [microphone hub](microphone.md#microphone-hub), owns a worker thread for the detector, reports each detection once to the runtime and keeps the model in memory only while it is wanted. |

The model must match the microWakeWord contract: input `[1, N, 40]` int8, one uint8 output. The
engine has the operators the shipped models use: CONV_2D, DEPTHWISE_CONV_2D, FULLY_CONNECTED,
AVERAGE_POOL_2D, LOGISTIC, QUANTIZE, CONCATENATION, STRIDED_SLICE, SPLIT_V, RESHAPE and the state
operators (CALL_ONCE, VAR_HANDLE, READ_VARIABLE, ASSIGN_VARIABLE). Anything else is refused at load with
a short reason: `not a TensorFlow Lite model`, `unsupported tensor N`, `unsupported operator N` (a
custom operator too), `unsupported graph` or `the model needs too much memory`.

Measured by hand, since no test prints it: the shipped model of 47 KB needs an arena of about 9 KB.
On the clock, detection takes about 6 to 7 % of one CPU core, and the frame rate stays at 42 fps.

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
rows.

The listener checks the model once at boot, which decides `wakeWordAvailable`. After a minute without
being wanted (`WakeListener::kModelOffMs`, the setting is off by default) it frees the model, the
copy of the file and the arena, and reads it again before the next stream. If the file cannot be used
by then, it writes that to the log and tries again every 30 s.

To ship another model, replace both files with the trainer's `model.tflite` and `custom.json` under
these names. The bundle tools accept the two files only together.

## Sensitivity

`wakeSensitivity` (`low`, `normal`, `high`, default `normal`) sets the cutoff the window uses.
`WakeSensitivity.h` holds one table and scales the doubt the model allows, one minus its
`probability_cutoff`:

| Level | Doubt | Cutoff of the shipped 0.86 |
|---|---|---|
| `low` | × 0.57 | 0.92 |
| `normal` | × 1 | 0.86, the model's own value |
| `high` | × 1.79 | 0.75 |

So a replaced model keeps three usable levels: 0.97 gives 0.983 and 0.946. A scale never passes 1.0,
as a fixed offset would, and a result below 0 counts as 0.

`AssistantRouter::tick` hands the level to the listener each frame, and the listener passes it to the
worker with every audio block, as it does the mute flag. `WakeDetector::setSensitivity` changes only
the window's threshold: the window, the warm-up and the cooloff go on. A model loaded again starts on
`normal` and takes the level from its first block.

## How it listens

- The settings are `wakeWord` and `wakeSensitivity` in the Voice configuration
  (`voice-private/voice.json`), off and `normal` by default. The web UI and `POST /api/v1/voice` change it; see
  [Credentials and configuration API](ha-voice.md#credentials-and-configuration-api).
- The listener streams while the wake word is on and the assistant is there
  (`AssistantRouter::listening`, reported as `wakeListening`):
  - With `off` as the assistant: never.
  - With Home Assistant as the assistant: while Voice is connected and idle.
  - With an app: while that app is installed with `@needs voice`, switched on and neither a module
    nor headless.

  The boot intro, an update and setup mode stop it, as they pause Voice. It listens through the
  [microphone hub](microphone.md#microphone-hub), which shares the stream with Voice and the voice
  module. Its epochs and sample positions are its own.
- It reports a detection only while the assistant could start, the same check as for the knob's
  hold. Otherwise it is muted: while a script records or plays, while a trigger waits for its app,
  while the app has a transfer or result of the [voice module](voice-module.md) outstanding, and with
  an app as the assistant while the clock speaks. A muted listener keeps its stream and its detector running, so "Hey AWTRIX" right after an answer
  needs no new stream and no warm-up. A detection in audio that arrived while it was muted is
  dropped, and its cool-off still runs, so the clock's own voice does not start the assistant.
- A detection starts the assistant the Voice settings name, as the knob's 500 ms hold does
  (`AssistantRouter::start`). For Home Assistant, Voice's overlay appears at once. Voice is then busy, so
  the listener stops its stream, and the hub keeps the stream open until Voice's capture joins it.
  For an app, the detection's position in the stream (`markOf`) becomes the start of the app's
  recording, and the hub keeps the stream and its last second while the app opens
  ([A trigger](voice-module.md#a-trigger)). Listening resumes when Voice is done, and reports resume
  once the app holds nothing.
- A detection that arrives after listening stopped being wanted is dropped.
- It sends a keepalive every 150 ms through its `MicrophoneClient`
  ([microphone hub](microphone.md#microphone-hub)). The hub ends the listener's stream when
  keepalives stop for 2 s.
- A stream that ends with an error restarts after 1, 2, 5, 10 and then 30 s. An MCU without the
  stream is asked again after 30 s.
- A jump in the sample position, or a full worker queue (16 blocks), resets the detector.
- While it listens, `music.level()`, `music.bands()` and `music.pitch()` read the hub's stream
  instead of separate snapshots.
- Each detection writes `wake word: heard (score …)` to the log. The listener keeps no audio beyond
  the blocks its worker has not processed yet. While the stream runs, the hub keeps its last second
  in memory ([lead-in](microphone.md#microphone-hub)), so it holds one all the time the listener
  streams. It overwrites that second continuously and drops it when the stream ends. Neither writes
  audio to a file.

## Speech end detection

`Endpointer` finds where speech in a recording begins and ends; `VoiceCapture` runs it for every
recording of the [voice module](voice-module.md). It runs the
[microWakeWord](https://github.com/kahrendt/microWakeWord) voice activity model from
[`assets/tc002/vad/`](https://github.com/Blueforcer/awtrix-ng/tree/main/assets/tc002/vad) on its
own `ModelStream`. The model, its ESPHome manifest `vad.json` and the repository's `NOTICE` are
unchanged from [esphome/micro-wake-word-models](https://github.com/esphome/micro-wake-word-models)
(v2, Apache-2.0).

- **Verdict:** a step of 30 ms has voice when the mean of the last `sliding_window_size` outputs
  reaches `probability_cutoff` (`ProbabilityWindow`), both from `vad.json` (5 and 0.5). Unlike the wake word, it needs no
  warm-up after a reset: on white, pink and brown noise, fan noise and hum from −50 to −20 dBFS, its
  first steps stay near 0. A weak 100 Hz hum at −50 dBFS gave a few voiced steps after about 0.7 s.
- **Decisions** (`EndpointTimer`):
  - The first voiced step starts the speech (`Started`).
  - The speech has ended after `silenceMs` without voice (`Ended`).
  - Without speech by `waitMs`, the recording heard nothing (`Silence`).
  - At `maxMs` it ends in any case (`Limit`).
- **Latency:** the model holds its verdict after speech. On calls recorded from the clock, voice began
  about 0.5 s after the speech and ended about 0.45 s after it. A recording therefore ends
  about `silenceMs` + 0.45 s after the last word. Silero VAD v6 ended about 0.28 s sooner at the same
  tolerance for pauses, but needs a float runtime of its own.
- **Cost**, measured by hand, since no test prints it: an arena of 2656 bytes and about 14 µs per step
  on a desktop CPU. `VoiceCapture` reads the model when a recording starts, in about 0.1 ms on a
  desktop CPU, and frees it when the recording ends. The release carries the model, `vad.json` and the `NOTICE` as `share/vad/`; the daemon passes it to a runtime with the
  speaker as `--vad FILE`. The bundle tools take the three files only together.

## Training notes

A model whose positive clips carry no background noise or room echo scores real calls low. Train
with noise and room impulse responses, and with recordings from the clock's own microphone. The
microphone delivers speech at about −35 dBFS RMS from a normal distance. The frontend's PCAN evens
out level differences, so the input gain does not change the scores. The shipped model was trained
with 46 real calls from the clock and detected 11 of 15 fresh calls on the clock.

## Checks

The checks run on the two shipped models, `hey_awtrix.tflite` and `vad.tflite`; there is no synthetic
model. `tests/tc002/runtime/test_wake.cpp` compares the frontend and every output of both models bit
for bit with pymicro_features and the LiteRT reference kernels on committed fixtures
(`tests/tc002/runtime/wake/`). It also checks the detector's window, cutoff, pause and warm-up, the
three sensitivity levels on a steady probability and on the reference signal, and refuses truncated and mutated files. `tools/tc002/wake/make_golden.py` writes the fixtures.
`tests/tc002/arm/` runs the same test on Cortex-A7 under qemu-arm. `test_wake_listener.cpp` covers the
settings file, the stream states, detections, muting, the stop while Voice is busy, restarts, gaps,
the model leaving memory while the setting is off and a level that applies at once and to a model
loaded again. `test_assist_session.cpp` covers the `wakeWord` and `wakeSensitivity` settings. `test_endpointer.cpp` covers the endpointer's decisions on exact voice patterns and the model
on speech of the English voice pack, a pause within a sentence and room noise, also on qemu-arm. No
test prints arena sizes or run times.

## Related

- [TC002 developer guide](index.md)
- [Home Assistant Voice](ha-voice.md), [Voice module](voice-module.md)
- [Microphone](microphone.md), [Continuous microphone audio](audio-gate.md)
