---
only: [tc002]
---

# TC002 voice module

`import voice` lets a script app record what someone says, play audio clips, and send and fetch
them over HTTP. With it, an app can answer the knob, the wake word, MQTT and the API in place of
[Home Assistant Voice](ha-voice.md). The module and the start of an app as the assistant
(`AssistantRouter`) are in
[`src/platform/tc002/voice/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/tc002/voice),
and the HTTP pieces it uses in `src/platform/linux/net/`. The ESP32 builds have none of it.

The module exists where `/api/v1/capabilities` reports `"voice": true` (a supervised TC002 with a
speaker) and scripting is on. `voice` is also the `@needs` name of `import voice`. The module reads
the voice activity model (`--vad FILE`) when a recording starts and frees it when the recording ends.
Without it, every recording answers `BUSY` and the log says `voice: recording failed: <reason>`, for
example `no voice activity model`.

## Parts

| Part | File | Role |
|---|---|---|
| Module | `VoiceScripting` | The script extension: the Berry source of `import voice`, its natives, the `on_voice` hook and the results it delivers. It owns the rules: who may record and play, the limits, the voice channel and the music hold. `LinuxRuntime::composeExtensions` builds it. |
| Capture | `VoiceCapture`, `WavWriter` | A consumer of the [microphone hub](microphone.md#microphone-hub). One recording at a time, from a mark or from now, into a WAV file (PCM16 mono, 16 kHz). The [endpointer](wake-word.md#speech-end-detection) ends it. |
| Clips | `ClipStore` | The clips of all apps as files in a private folder in RAM, `/tmp/awtrix-clips-<pid>-XXXXXX` (`posix::PrivateTemporaryDirectory`, mode 0700). It first removes the folders of runtimes that no longer run, and removes its own with its files at the end. A clip is reserved before its file is written and usable after `commit()`. A reservation must fit into `MemAvailable` beyond the 4 MB the runtime keeps free (`MemoryRoom`, `kKeepFreeBytes`, the same margin as sounds from a URL). |
| Speaker | `VoiceSpeaker`, `Tc002VoiceSpeaker` | The speaker as the module and Home Assistant Voice see it: claim the voice channel, hold the music, quiet, play, stop, finished, heard. |
| Transfers | `VoiceTransfers` | The module's `send` and `fetch`. It checks a script's options, builds the request, reserves a download's clip and commits it when the file is WAV or MP3, and hands each result to the module with its status. |
| Transfer queue | `net::TransferQueue` | HTTP requests of named owners on one worker thread, started by the first request, one after the other, with a limit per owner and a cancel per owner. An upload keeps its answer through `net::sendFiltered`, the filter of the script `http`. Its body (`net::RequestBody`) can stream a mapped file (`posix::MappedFile`) between two texts, so a clip goes out without a copy in memory. A download runs through `net::exchange`, which sees the status and the `Content-Type` before the body. It writes each block to its file as it arrives, asks a room hook before the body and before each block, and its check runs on the finished file. Nothing in it is voice-specific. |
| Keys | `secrets::Service` | Fills [`{secret:key}`](#key-filling) on the way out. It is in `src/platform/linux/secrets/` and knows nothing of voice. |
| Forms | `net::buildMultipart` | A `multipart/form-data` body from fields and one file: a head and a tail that go around the file's bytes. The boundary is random and checked against the parts. |
| Voice channel | `Tc002AudioSink::claimVoiceChannel()` | A move-only `posix::Hold`, empty while another holder has the channel. |
| Music hold | `Tc002AudioSink::holdMusic()` | A move-only `posix::Hold`. While any holder lives, the station and a script's music loop wait; the loop starts over afterwards. Nothing else is refused or stopped. |
| Assistant | `AssistantRouter` | Every trigger goes to the assistant the Voice settings name: Home Assistant Voice, or an app. It sees Voice and the module through two small interfaces (`AssistantRouter::Voice`, `AssistantRouter::Module`) and the engine through ports. It also ticks the knob's hold, holds the hub and gates the wake word. `LinuxRuntime::composeAssistant` builds it. |

The Berry side keeps each callback under a number: recordings, playbacks and transfers each count
up across all apps. So a `play` that replaces another, or a `cancel` followed by a new `record`,
never reaches the wrong callback. Results reach the script through `_voice_dispatch` on the script
tick, at most 32 per tick. What a callback starts waits for the next tick.

## A trigger

The `assistant` setting names who answers: `ha` or `app:<name>`
([Credentials and configuration API](ha-voice.md#credentials-and-configuration-api)). Four
triggers go through `AssistantRouter::start()`:

| Trigger | Name in `on_voice` | Mark |
|---|---|---|
| [Wake word](wake-word.md) | `wake` | the detection's position in the stream (`markOf`) |
| Knob held for 500 ms | `knob` | the hub's current position (`mark()`) |
| MQTT `cmd/voice/start` | `mqtt` | the hub's current position |
| `POST /api/v1/voice/start` | `api` | the hub's current position |

`mark()` has epoch 0 when no stream runs. Nothing starts during the boot intro, an update or setup
mode, and `blockNavigation` stops the knob only. With Home Assistant as the assistant, the trigger
starts Voice (`VoiceRuntime::begin`, [Operation](ha-voice.md#operation)). With an app:

1. `startable()` checks that the app is installed with `@needs voice`, is neither a module
   nor headless, is a script app and is switched on. The module must be idle, no trigger may wait,
   the app may hold nothing, and Home Assistant Voice must be idle. Otherwise the log says
   `voice: assistant app <name> unavailable` and nothing starts.
2. The runtime opens the app with `switchApp` (`{"name": "<name>", "fast": true}`). It starts an
   `@ondemand` app as a session and ends another session. A refusal writes
   `voice: assistant app <name> did not open: <reason>`.
3. It closes the quick settings and switches a panel that was off on (`setDisplayPower`, as
   `display.power()` does). It takes a music hold and hands the trigger and its mark to the module
   (`trigger()`).
4. While the trigger waits, the runtime holds the hub, so the stream and its last second stay.
5. The app's first frame marks the trigger as drawn (`beginFrame`). The script tick after that frame
   calls `on_voice(trigger)`. A call inside `beginFrame()` would run in the render's binding scope
   and blank that first frame.
6. `voice.record()` starts from the trigger by default (`from: 'trigger'`). A mark of the running
   stream (`MicrophoneHub::live`) starts the recording at once: the hub replays its held audio from
   the mark, then the live stream. A mark further back than the held second starts at the oldest held
   sample. Without a live mark (no stream ran at the trigger, or it has ended since), the recording
   starts like one from now and waits for a quiet speaker. A mark serves one recording; the next one
   starts from now.
7. If the app does not draw within 3 s, the trigger lapses: `voice: assistant app <name> did not
   show`.

Hiding or removing the app drops its trigger and its mark, and so does the end of the interaction
(`dropTrigger()`), also when `on_voice` started no recording.

### Panel and music during an interaction

An interaction runs from the trigger until one of these:

- the trigger lapses after 3 s,
- the app leaves the panel,
- the app has held nothing for 10 s (`holds()`: no recording, playback, transfer or undelivered
  result).

Meanwhile the runtime's music hold keeps the station and the music loop waiting, so the music does
not come back between question and answer. The app's own speech still plays. At the end, the runtime
switches the panel off again if it was off at the trigger and is still on, and lets go of its hold.
A new trigger in the last 10 s replaces the interaction and keeps the panel state of the first one.

## Who owns what

**Microphone.** The module has its own hub consumer. One recording runs at a time, for all apps
together. Only the app on the panel may record.

- A recording from now waits until the speaker is quiet, at most 1 s, so it does not record fading
  music. It counts as recording meanwhile: `voice.state()` is `recording` and the hub stays held.
- A recording ends where the endpointer hears the speech end (`silence` without voice), after
  `wait` without speech (`SILENCE`), at `max` with what it has, or on `voice.stop()` with what it
  has (`SILENCE` before any audio).
- It fails when the stream ends or skips audio, when the microphone does not answer within 2 s, or
  when it has not ended 2 s after its limit (`max`), counted from the start of its stream, because
  audio stopped arriving. The callback then gets `BUSY`, and the log
  `voice: recording failed: <reason>`.
- `voice.level()` is the loudness of the latest block, 0 at −60 dBFS and below, 255 at full scale,
  and 0 when the app does not record.

**Speaker.** The voice channel (`claimVoiceChannel()`) has one holder at a time: Home Assistant
Voice or the module. A claim fails while the other one holds it. While it is held, the station and
a script's music loop wait and every other sound is refused, local speech included.

- The module holds the voice channel while it records (also while it waits for a quiet speaker) or
  plays, and until those results are delivered. A `play` from the record callback keeps the claim
  without a gap.
- One playback runs at a time. A new `play` replaces the running one, whose callback gets `false`.
  A clip plays like Voice's answer: as an alert, WAV PCM16 or MP3.
- While a transfer of any app runs, or its result waits, the module holds the music instead of the
  voice channel. Local speech (`sound.play({'speech': …})`) and other one-shots then play.
- The module and Home Assistant Voice exclude each other through the claim. Voice also starts only
  while the module is not busy (`busy()`: it holds the voice channel), and the module records and
  plays only while Voice is not busy. A transfer holds only the music, so Voice may run during it.
- The [wake word](wake-word.md#how-it-listens) is muted while the module records or plays: it keeps
  its stream but reports nothing.

**Apps.** A clip belongs to its app.

- While the app records, plays, transfers or has a result waiting, it holds the rotation: it stays
  on the panel past its duration.
- Hiding the app stops its recording (`CANCELLED`) and its playback (`false`), and deletes its
  finished clips. Its transfers go on, and so does the clip a download fills.
- Removing or unloading the app cancels everything it started, deletes all its clips and drops its
  callbacks.
- The garbage collector releases a clip the script dropped (`deinit`). `deinit` never raises. A release
  names the app and the clip id, because the collector may run while another app is the caller. Clip
  ids are random numbers below 2^63, so an app cannot guess another app's clips, and a release reaches
  only a finished clip, never one a recording or download still fills.
- `voice.cancel()` ends the app's recording, playback and transfers, also results already polled
  but not yet delivered. Recordings and transfers get `CANCELLED`, a running playback `false`.

## Limits

| Limit | Value |
|---|---|
| Recording options | `silence` 300 to 3000 ms (800), `wait` 1 to 15 s (5), `max` 1 to 30 s (15), `from` `'trigger'` or `'now'` (`'trigger'`) |
| Clips | 4 and 4 MB per app (`ClipStore::kClipsPerApp`, `kBytesPerApp`). A recording reserves its WAV header plus (`max` + one 30 ms model step) × 32 bytes. A download reserves 2 MB until it ends. Every reservation and every block of a download must leave 4 MB of `MemAvailable` free. |
| Clip length | `clip.ms` is exact for recordings and 0 for downloads |
| Transfers | 2 per app, queued or running. All apps share one worker. |
| `voice.send` | a clip or a string. A clip goes as a form by default: field `file`, file name `speech.wav` or `speech.mp3`, at most 16 further fields. With `as: 'raw'` the clip is the body, with its own type unless a header names one. A form with a `Content-Type` header is `BAD`. A string is the body, as `application/json` unless a header names a type, and `as`, `field` and `fields` do not apply. POST by default, the answer kept and capped as for `http` (8 KB by default). |
| Transfer time | `timeout`, 1 to 120 whole seconds, 30 for `send` and 15 for `fetch`. It is the budget from connecting to the last byte and also the socket's read limit, so a service may take all of it before its first byte. The connection itself has 3 s. |
| `voice.fetch` | status 200 only. WAV or MP3 by `Content-Type` (`audio/mpeg`, `audio/wav`, `audio/x-wav` or `application/octet-stream`) and by its bytes, at most 2 MB. Redirects only for a GET without a filled key. POST when a `body` is given. |
| Results | 32 per script tick |
| Interaction | 3 s for the app to draw. It ends 10 s after the app last held something. |

## Key filling

A script writes [`{secret:key}`](../../guides/scripting/network.md#api-keys) into a header value or
the path or query of a URL. It stays a plain string in the VM. The transports fill it through one
hook, `net::FillRequest`, a function of owner, URL and headers that answers a `net::RequestFill`.
`LinuxRuntime` binds it to `secrets::Service::fill`.

| Transport | Where it calls the hook | On a refusal |
|---|---|---|
| `LinuxScriptHttp` (`http.*`) | on its worker, before it splits the URL | no request; the callback gets `nil` and status 0 |
| `net::TransferQueue` (`voice.send`, `voice.fetch`) | in `perform()`, on its worker | no request; the result has status 0 |

The hook takes the owner the transport recorded when the script asked, not the current script. It
answers one of three:

- `Unchanged`: no placeholder. The transport sends the request as it is.
- `Filled`: URL and headers hold the values. The transport turns redirects off, so a key never
  follows one. `TransferQueue` splits the filled URL again.
- `Refused`: URL and headers stay as they were. The debug log names the app, the reason and the host,
  never the URL or a value.

A placeholder is checked before it is filled, against the `Secret` from the app's header: the key
must be declared and set, `script::urlRefusal` must allow the URL (`https://`, or `http://` to
`net::isPrivateHost`, no user information, `hosts=` if given), and in the URL it must be in the path or
query. `isPrivateHost` judges the literal host without DNS. A placeholder in the scheme, host, port
or user information already fails `net::splitUrl` when the script asks, so `http.*` answers `nil`, 0
and the module `BAD`.

`VoiceTransfers` splits the URL when the script asks and hands the queue origin and target. A
fragment is gone at that point, so a placeholder in a fragment never reaches the hook: the request
goes out without it. In `http.*` the hook sees the whole URL and refuses.

## Status values

A callback gets an HTTP status or one of the module's constants. A recording delivers 200 and a
WAV clip, a download 200 and a WAV or MP3 clip (`clip.kind` is `wav` or `mp3`). `send` and `fetch`
deliver the server's status otherwise, or 0 when no server answered.

| Constant | Value | When |
|---|---|---|
| `voice.SILENCE` | −1 | A recording heard nothing within `wait`, or `voice.stop()` came before any audio. |
| `voice.BUSY` | −2 | The app is not shown, Voice is busy, another recording runs, there is no voice activity model or no clip folder, or the recording failed. Also a third transfer of the app. |
| `voice.CANCELLED` | −3 | `voice.cancel()`, or the app was hidden while it recorded. |
| `voice.FULL` | −4 | The new clip would exceed the app's 4 clips or 4 MB, or leave less than 4 MB of the clock's memory free. Also a download that runs out of memory. |
| `voice.GONE` | −5 | `voice.send` with a clip that was released, for example because the app was hidden. |
| `voice.BAD` | −6 | An option out of range or of the wrong type, a bad URL, method or header, an `as` other than `form` or `raw`, a 17th form field, a `Content-Type` header for a form, a negative `keep`, a `timeout` outside 1 to 120 or not whole, `data` neither a clip nor a string. Also a `fetch` answer 200 that is no WAV or MP3, or larger than 2 MB. |

`voice.play()` returns `false` when the clip is not the app's, Voice is busy or the speaker cannot
play it. Its callback gets whether the clip was heard.

## Checks

| Test | Covers |
|---|---|
| `tests/tc002/runtime/test_voice_scripting.cpp` (`tc002-voice-scripting`) | the module from scripts: record and play, refusals, `SILENCE`, stop or cancel followed by `record` in one turn, the wait for a quiet speaker and its 1 s limit, a recording from the trigger that starts at once at its mark, a mark that ends with the interaction or with its stream, clips released by the garbage collector, `FULL` and `level`, record, send and fetch in a row against a local server, the options and limits of `send`, the statuses of `fetch`, `cancel` reaching results already polled, transfers that go on after hiding, the music hold during transfers, a module destroyed mid-recording, `FULL` when memory runs short for a recording or a download, the model read per recording, a release of a clip a download fills, `on_voice` after the first frame |
| `test_voice_capture.cpp` (`tc002-voice-capture`, also on qemu-arm) | a recording on the hub: a sentence of the voice pack, silence, a start from a mark compared sample by sample, stop and cancel, a stalled stream, a microphone that never answers, the model read for each recording and freed after it (it prints the load time), the level |
| `test_clip_store.cpp` (`tc002-clip-store`) | the WAV header, the limits per app and of memory, random ids, commit and release (by the module, by a script only when finished), folders of a crashed runtime |
| `test_microphone_hub.cpp` (`tc002-microphone-hub`) | marks and whether they are live, the held second and starts from a mark |
| `test_audio_sink.cpp` (`tc002-audio-sink`) | the voice channel's one holder; the music hold: speech plays while it holds, a second holder, a moved hold |
| `test_assist_session.cpp` (`tc002-assist-session`) | the `assistant` setting: default, valid and refused names, a change drops the Home Assistant connection, a restart keeps it |
| `tests/linux/test_transfer_queue.cpp`, `test_multipart.cpp`, `test_posix_files.cpp`, `test_file_download.cpp` | the transfer queue (limits, cancel, redirects, files of cancelled downloads and of results never polled, a download out of room, uploads streamed from a file, no thread before the first transfer), the form builder, the private temporary folder, mapped files, a body streamed around a mapped file |
| `test_assistant_router.cpp` (`tc002-assistant-router`) | the router with fakes for Voice and the module: every trigger name and its mark (the wake word on the smoke model), the refusals and their log lines, the panel, the music hold and the trigger's mark over an interaction (3 s, 10 s, the app leaving, a second trigger), the hub hold, when the wake word listens and when it is muted, a frame without allocations, Home Assistant as the assistant, the list of assistant apps |
| `test_voice_runtime.cpp` (`tc002-voice-runtime`) | `POST /api/v1/voice/start` (405, 403, 409, 200), `wakeWordAvailable` and `assistantApps` in `GET /api/v1/voice`, a wake word switch that keeps the Home Assistant connection while any other change drops it |
| `webui/test/voice-card.test.js` | the assistant choice in the web UI |
| `tests/linux/test_secret_spec.cpp`, `test_secret_service.cpp`, `test_secret_api.cpp`, `test_secret_script.cpp`, `test_secret_contract.py`, `test_script_http.cpp` | the `@secret` lines, what is filled and what refused, `/api/v1/secrets`, `secret.has()`, the keys in a running runtime, the hook in `LinuxScriptHttp` |

## Related

- [TC002 developer guide](index.md)
- [Home Assistant Voice](ha-voice.md), [Wake word](wake-word.md)
- [Microphone](microphone.md), [Speech](speech.md)
- [Voice in scripts](../../guides/voice-scripts.md), the page for script authors
