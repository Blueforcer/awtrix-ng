---
only: [tc002]
---

# TC002 Home Assistant Voice

Home Assistant Voice lets a TC002 act as a microphone and speaker for Home Assistant's Assist,
started with a long press of the knob. It runs in the Linux runtime and uses the continuous microphone stream
([Audio gate](audio-gate.md)). The transport is Home Assistant's authenticated Assist pipeline
WebSocket API; the clock does not register a satellite entity and does not listen for a wake word.
The code lives in
[`src/platform/tc002/voice/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/voice),
with the stream in `daemon/McuStream` and `contract/MicrophoneStream`. The ESP32 firmware has no
voice code; its web UI image leaves the voice section out. Setup for end users is in the
[voice guide](../../guides/voice.md).

## Setup

The web UI shows **System → Home Assistant Voice** only where `/api/v1/capabilities` reports
`"voice": true`, that is on a supervised TC002 with a speaker. The section holds the connection
state, the Voice switch, the Home Assistant address (for example
`http://homeassistant.local:8123`), a long-lived access token and the Assist pipeline, which must
have both STT (speech-to-text) and TTS (text-to-speech). An empty pipeline follows Home Assistant's
preferred pipeline. **Save and connect** saves and switches Voice on. The Voice switch saves at once;
off closes the Home Assistant connection, and the knob's long press then does nothing. HTTPS
verifies the certificate chain and host name against the runtime's CA store; there is no insecure
TLS option. After a failure the runtime reconnects at most every 10 s.

## Operation

- Turning the knob opens the brightness and volume panel; the first detent opens it, later detents
  change the selected value. A short click switches rows only while that panel is open and cannot
  open it.
- Holding the knob for 500 ms (`KnobGesture::kHoldMs`) starts a request. Voice closes the
  settings panel, stops competing playback and waits for the speaker's stop acknowledgement.
  Letting go afterwards ends nothing.
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
- With Voice off, the microphone is never captured for Voice. The local audio visualisation is a
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
exit or restart revokes its capture, and callbacks never cross runtime generations.

**Runtime.** `KnobGesture` arbitrates the knob before the settings panel or Voice sees it.
`AssistSession` is a state machine on the render thread with injected transport, capture and
playback ports. `WebSocket` (`WebSocket.cpp`) is a WebSocket client on plain sockets and OpenSSL,
with DNS, TCP, TLS and socket I/O on its own worker thread; TLS peers are checked against the
runtime's CA store (`--ca-file`, `tls/TlsTrust`), the store every HTTPS client uses.
`net::FileDownload` (`platform/linux/net`) fetches the answer on another worker, at most 2 MiB
within 15 s and without redirects; cancelling uses a duplicated socket descriptor and never waits
for an HTTP client lock. MP3s played from a URL use the same downloader.

**Timing.** The microphone starts only after Home Assistant's `stt-start`, with a 5 s deadline for
Home Assistant and 1.5 s for the MCU's acknowledgement. A short press while Home Assistant is
preparing cancels at once; a late readiness cannot start a recording. The daemon's boot-relative
timestamp is converted into the runtime's clock by its transport age, and blocks older than 250 ms
are refused. Congestion, stale data or missing samples abort the pipeline rather than drop speech
and run a partial command. A clean microphone end is required before EOF is sent to Home Assistant.

| Limit | Value |
|---|---|
| capture length | 60 s |
| consumer keepalive / consumer lease | 150 ms / 500 ms |
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
Microphone audio is never written to a file.

`GET /api/v1/voice` returns the redacted configuration (`config`), the session `state`, an `error`
code and the bounded list of Home Assistant `pipelines`. The error codes are `connectionLost`,
`tokenRejected`, `haError`, `pipelineIncomplete`, `nothingUnderstood`, `audioTooSlow`, `timeout`,
`microphoneFailed`, `playbackFailed`, `microphoneUpdate` (the MCU does not offer the stream yet) and
`configUnavailable`; the web UI shows each as a few words. `POST /api/v1/voice` saves the supplied
fields (`url`, `token`, `pipeline`, `device`, `enabled`) and reconnects. A non-empty `device` is
sent as `device_id` with every `assist_pipeline/run`; Home Assistant takes the area of that device
as the room for area-aware sentences and ignores an unknown ID. An omitted token is kept;
`clearToken: true` removes it. The body is limited to 8192 bytes and needs a matching
`Origin`/`Host` pair and `X-Awtrix-Voice: 1`, in addition to the usual administration guards
(`403 forbiddenOrigin` otherwise). A rejected field answers `422 validationFailed` with the
`field` named. A successful save confirms that the settings were stored, not that Home Assistant
accepted the token; the next GET reports that. No HTTP route starts a microphone capture. The user
reference is [`/api/v1/voice`](../../reference/http.md#apiv1voice).

## Tests

Regression suites cover MCU framing and failure deadlines, supervisor serialisation, the exact
knob hold threshold, overlay activation, Assist ordering, EOF, voice activity detection and
cancellation, delayed STT start, cross-process clock conversion, credential origin binding and
reset, real WebSocket binary transport and TLS rejection, WAV decoding and exclusive speaker
ownership (`tests/tc002/runtime/test_assist_session.cpp` and neighbours,
`tests/tc002/daemon/test_mcustream.cpp`). Runtime integration tests also cover an exhausted
eight-block queue with its reserved terminal slot, and callbacks from an exited runtime whose epoch
is reused by its replacement. `webui/test/voice-card.test.js` checks that every error code the
firmware reports has a translation in the web UI.

Changes to the stream require [hardware qualification](audio-gate.md#qualification-before-changing-the-stream-path). An end-to-end check
needs a clock with the MCU extension and a Home Assistant instance with a working Assist pipeline.

## Related

- [TC002 developer guide](index.md)
- [Microphone](microphone.md), [Audio gate](audio-gate.md), [MCU updates](mcu-update.md)
- User docs: [Home Assistant Voice](../../guides/voice.md)
- Protocol references: [Assist pipelines](https://developers.home-assistant.io/docs/voice/pipelines/)
  and [WebSocket API](https://developers.home-assistant.io/docs/api/websocket/)
