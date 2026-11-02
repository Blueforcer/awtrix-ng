---
only: [tc002]
---

# TC002 Home Assistant Voice

Home Assistant Voice lets a TC002 act as a microphone and speaker for Home Assistant's Assist,
started with a long press of the knob or the [wake word](wake-word.md). It runs in the Linux runtime and uses the continuous microphone stream
([Audio gate](audio-gate.md)). The transport is Home Assistant's authenticated Assist pipeline
WebSocket API; the clock does not register a satellite entity, and it detects the wake word itself.
The code lives in
[`src/platform/tc002/voice/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/voice),
with the stream in `daemon/McuStream` and `contract/MicrophoneStream`. The ESP32 firmware has no
voice code; its web UI image leaves the voice section out. Setup for end users is in the
[voice guide](../../guides/voice.md). A script app can answer the same triggers instead of Home
Assistant ([Voice module](voice-module.md)).

## Setup

The web UI shows **System → Voice assistant** only where `/api/v1/capabilities` reports
`"voice": true`, that is on a supervised TC002 with a speaker. Its first row, **Assistant**, picks
Off, Home Assistant or one of the apps in `assistantApps` and saves at once. Off closes the Home
Assistant connection and keeps its data, and no trigger then starts anything. Below it, for any choice
but Off, **Status** shows `error`, or `state` with `lastError` as a note. **Wake word** and
**Sensitivity** save at once and show once the assistant is set up; a note replaces the help while
`wakeWord` is on, no request runs and `wakeListening` is false. An app also gets a link to the Apps tab.

**Home Assistant settings** shows only for Home Assistant, open while the address or token is missing
or the connection fails and closed once it is connected, unless the user opened it. Address (for
example `http://homeassistant.local:8123`) and long-lived access token go together with **Connect**,
which stores both and reconnects; the page reduces the address to its origin first. Once a connection
listed them, **HA assistant** picks an Assist pipeline with both STT (speech-to-text) and TTS
(text-to-speech), and **Room device** takes the device. Both save at once. An empty pipeline follows
Home Assistant's preferred pipeline. No field changes the target, and neither does **Remove** next to
the token. Voice connects while the target is `ha` and both an address and a token are set. HTTPS verifies the certificate chain and host name against the
runtime's CA store; there is no insecure TLS option. After a failure the runtime reconnects at most
every 10 s.

## Operation

This section describes Home Assistant as the assistant. With an app, the knob's hold, the wake word,
MQTT and the API open the app instead ([A trigger](voice-module.md#a-trigger)).

- Turning the knob opens the brightness and volume panel; the first detent opens it, later detents
  change the selected value. A short click switches rows only while that panel is open and cannot
  open it.
- Holding the knob for 500 ms (`KnobGesture::kHoldMs`) starts a request. Voice closes the
  settings panel, stops competing playback and waits for the speaker's stop acknowledgement.
  Letting go afterwards ends nothing. A detected wake word starts a request the same way
  ([Wake word](wake-word.md)).
- The Home Assistant house on the panel wakes up into a face. The user speaks when the house opens
  its mouth and the level bars appear. While the house smiles next to a row of dim dots, Home
  Assistant is preparing recognition and no microphone audio is captured yet.
- Home Assistant's voice activity detection ends the input: the run request sends no `no_vad`, and
  every stock speech-to-text provider sets `requires_external_vad`, so the pipeline's
  `VoiceCommandSegmenter` stops after 0.7 s of silence following speech, and after 15 s in any
  case. The microphone then stops and the request completes. A short press while Voice is busy ends
  the input at once (`endInput`), or cancels a request whose `stt-start` has not arrived.
- Turning the knob during an interaction changes neither brightness nor volume. Turning a pressed
  knob before activation cancels the pending hold.
- A long press is consumed: its release never becomes a click. Another request needs a fresh press.
- With another target than `ha`, the microphone is never captured for Voice. The local audio visualisation is a
  separate feature and cannot switch Voice on.

The answer is an alert and plays at the alert volume, as MP3 (MPEG-1, 2 or 2.5 Layer III, 8 to
48 kHz; Home Assistant Cloud sends 24 kHz mono) or PCM16 WAV (mono or stereo, 16, 22.05, 24, 32,
44.1 or 48 kHz). A station that was playing comes back afterwards, and so does app music, from its
start; `Tc002AudioSink` holds both back while Voice owns the speaker. Effects that Voice interrupted
do not come back. The panel returns to the app when the speaker has actually finished, not at Home
Assistant's `run-end`. A failed interaction briefly shows the house with crossed eyes and an orange
exclamation mark and is never replayed.

`VoiceOverlay` draws the native 52 x 16 panel without I/O or allocations: the house from the Home
Assistant logo in columns 0 to 14, the phase in columns 17 to 51. The listening bars follow the
DC-rejected RMS envelope of validated microphone blocks, with fast attack, gentle decay and slow
motion during silence; loud input lifts the house by one pixel. The overlay keeps eleven scalar
levels, not PCM. Processing shows a thought cloud with bouncing dots; during the spoken answer the
mouth moves and arcs travel from blue to grey, both on a fixed rhythm rather than the TTS level.
Brightness follows the normal panel setting.

## How it works

**Microphone path.** `McuStream` runs one stream transaction inside `McuService`, the only UART
owner. It validates epoch, DMA part order, alternating halves, sample counts and MCU timing, and
sends control only inside matching receive windows. The MCU lease, the consumer lease and the stop
deadline are independent safeguards. `MicrophoneStream` carries owned PCM16 blocks (16 kHz, mono)
with epoch, sample position and the host's monotonic timestamp; a block holds at most 480 samples
(30 ms). The daemon queues eight events plus one reserved slot for the terminal event. A runtime
exit or restart revokes its capture, and callbacks never cross runtime generations. In the runtime,
Voice reaches the stream through the [microphone hub](microphone.md#microphone-hub), which shares
it with the wake word and the voice module. While Voice starts, the runtime holds the hub, so Voice's capture joins the
wake word's stream instead of waiting for a new one.

**Runtime.** `KnobGesture` arbitrates the knob before the settings panel or Voice sees it.
`AssistSession` is a state machine on the render thread with injected transport, capture and
playback ports. Its capture goes through `MicrophoneClient`
([microphone hub](microphone.md#microphone-hub)): it only asks whether the stream exists while it
connects, starts without another probe at `stt-start`, and fails its pipeline on a gap or an end
nobody asked for. `WebSocket` (`WebSocket.cpp`) is a WebSocket client on plain sockets and OpenSSL,
with DNS, TCP, TLS and socket I/O on its own worker thread; TLS peers are checked against the
runtime's CA store (`--ca-file`, `tls/TlsTrust`), the store every HTTPS client uses.
`net::FileDownload` (`platform/linux/net`) fetches the answer on another worker, at most 2 MiB
within 15 s and without redirects; cancelling uses a duplicated socket descriptor and never waits
for an HTTP client lock. MP3s played from a URL use the same downloader.

**Timing.** The microphone starts only after Home Assistant's `stt-start`, with a 5 s deadline for
Home Assistant and 1.5 s for the stream's start. A short press while Home Assistant is preparing
cancels at once; a late readiness cannot start a recording. The daemon's boot-relative
timestamp is converted into the runtime's clock by its transport age, and blocks older than 250 ms
are refused. Congestion, stale data or missing samples abort the pipeline rather than drop speech
and run a partial command. A clean microphone end is required before EOF is sent to Home Assistant.

| Limit | Value |
|---|---|
| capture length | 60 s |
| consumer keepalive / consumer lease | 150 ms / 2 s |
| stop deadline | 1.5 s |
| Home Assistant processing | 30 s |
| TTS answer | 2 MiB, same-origin `/api/tts_proxy/` URLs only, no redirects |

Network reconnects never replay audio.

## Credentials and configuration API

The configuration and token are stored in `voice-private/voice.json` beside the application data
directory (`/data/awtrix-ng/voice-private/`), a private 0700 directory with a 0600 file. The token
is left out of normal settings, backups, status and logs. Changing the Home Assistant address
requires entering the token again, so a kept token never moves to another server. A full factory
reset erases the private configuration. TTS temporary files live in a private directory under
`/tmp`, away from flash and the release, and are removed after completion or cancellation.
Voice never writes microphone audio to a file. The recordings of the
[voice module](voice-module.md) are clips in RAM.

`GET /api/v1/voice` returns the redacted configuration (`config`), `state`, the codes `error` and
`lastError`, `wakeWordAvailable` (the runtime loaded a wake word; the web UI shows the Wake word rows
only then), `wakeListening` (`AssistantRouter::listening`, the condition that subscribes the listener),
`assistantApps` and the bounded list of Home Assistant `pipelines`. `assistantApps`
names the installed scripts whose `@needs` names `voice`, whose source holds `on_assistant` and that
are neither modules nor headless, switched on or off; the web UI offers them under **Assistant**. With
an app as the target, `state` is
`ready` or `error`, and `error` comes from `AssistantRouter::appProblem`: `appMissing`, `appOff` or
`appBroken`. Otherwise `state` is the session's. `AssistSession` keeps two reasons apart, by whether
the session was busy when it failed. `error` is what blocks the assistant: `connectionLost` (also no
answer while connecting), `tokenRejected`, `haError`, `pipelineIncomplete`, `microphoneUpdate` (the MCU
does not offer the stream yet) or `configUnavailable`. It survives `connecting`, so a retry does not
hide it, and goes with `Ready`. `lastError` is why the last request failed, one of the session's codes
including `nothingUnderstood`, `audioTooSlow`, `timeout`, `microphoneFailed` and `playbackFailed`. The
reconnect after a failed request keeps it. It goes with the next `begin`, after
`AssistSession::kLastErrorMs` (five minutes) or with `cancel`. The web UI has a label and a fix for
every code. `POST /api/v1/voice` saves the supplied
fields (`assistant`, `url`, `token`, `pipeline`, `device`, `enabled`, `wakeWord`, `wakeSensitivity`)
and reconnects, unless only `wakeWord` or `wakeSensitivity` changed: that leaves the connection and a
running request alone.

`assistant` names who answers the knob, the wake word, MQTT and the API: `off` (the default), `ha` or
`app:<name>`. It is the only stored state. `enabled` is not written to the file. Reading, it is true for
`ha`. Writing `false` sets `off` and `true` sets `ha`, whatever the target was; `assistant` wins when a
request has both. `ConfigStore::load` reads files of older versions: `enabled: false` with `ha` or
without `assistant` is `off`, `enabled: true` without `assistant` is `ha`, and an app target stays. The name must be a valid app name: 1 to 32 of `A-Z`,
`a-z`, `0-9`, `_` and `-`, and not `active`, `next`, `previous` or `order`. Anything else answers
`422` with the field `assistant`. With an app, Voice keeps no connection to Home Assistant, and every
trigger opens the app. A change of `assistant` drops a running connection like any other change.

A non-empty `device` is sent as `device_id` with every `assist_pipeline/run`; Home Assistant takes
the area of that device as the room for area-aware sentences and ignores an unknown ID. An omitted
token is kept; `clearToken: true` removes it. The body is limited to 8192 bytes and needs a
matching `Origin`/`Host` pair and `X-Awtrix-Voice: 1`, in addition to the usual administration
guards (`403 forbiddenOrigin` otherwise). A rejected field answers `422 validationFailed` with the
`field` named. A successful save confirms that the settings were stored, not that Home Assistant
accepted the token; the next GET reports that. The user reference is
[`/api/v1/voice`](../../reference/http.md#apiv1voice).

### Start from the API

`POST /api/v1/voice/start` is the API trigger: it starts the assistant the settings name, as the
knob's hold does, and an app gets `on_assistant('api')`. It takes no body and follows the header
rule of `POST /api/v1/voice`: `X-Awtrix-Voice: 1` and a matching `Origin`/`Host` pair. It answers with
`Cache-Control: no-store`:

| Status | Answer |
|---|---|
| 200 | `{"ok":true}`: Voice started a request, or the app opened |
| 403 | `forbiddenOrigin`: the header or the origin does not match |
| 405 | `methodNotAllowed`, `allowed: POST`: another method |
| 409 | `voiceUnavailable`, `assistant not ready`: nothing started. The assistant is off, Voice is not connected or busy, a script records or plays, the app cannot start ([A trigger](voice-module.md#a-trigger), step 1), or the boot intro, an update or setup mode runs |

The user reference is
[`POST /api/v1/voice/start`](../../reference/http.md#post-apiv1voicestart).

## Tests

Regression suites cover MCU framing and failure deadlines, supervisor serialisation, the exact
knob hold threshold, overlay activation, Assist ordering, EOF, voice activity detection and
cancellation, delayed STT start, cross-process clock conversion, credential origin binding and
reset, real WebSocket binary transport and TLS rejection, WAV decoding and exclusive speaker
ownership (`tests/tc002/runtime/test_assist_session.cpp` and neighbours,
`tests/tc002/daemon/test_mcustream.cpp`). Runtime integration tests also cover an exhausted
eight-block queue with its reserved terminal slot, and callbacks from an exited runtime whose epoch
is reused by its replacement. `test_voice_on_hub.cpp` runs a session on the microphone hub against a
fake daemon: joining a stream that a second consumer keeps, the end of a capture, cancelling,
starting while the hub stops its stream, and an upstream failure that ends both consumers.
`test_assist_session.cpp` also covers the `assistant` setting, `enabled` in both directions, every
older file shape and which reason a failure leaves, for how long. `webui/test/voice-card.test.js` checks the setup from scratch, every state, the assistant choice and that every error code the firmware reports has a label and a fix in the
web UI.

Changes to the stream require [hardware qualification](audio-gate.md#qualification-before-changing-the-stream-path). An end-to-end check
needs a clock with the MCU extension and a Home Assistant instance with a working Assist pipeline.

## Related

- [TC002 developer guide](index.md)
- [Voice module](voice-module.md), [Wake word](wake-word.md)
- [Microphone](microphone.md), [Audio gate](audio-gate.md), [MCU updates](mcu-update.md)
- User docs: [Home Assistant Voice](../../guides/voice.md)
- Protocol references: [Assist pipelines](https://developers.home-assistant.io/docs/voice/pipelines/)
  and [WebSocket API](https://developers.home-assistant.io/docs/api/websocket/)
