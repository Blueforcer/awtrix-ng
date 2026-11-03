---
only: [tc002]
---

# Voice module

Every call, option and status value of [`import voice`](../guides/voice-scripts.md). The limits are in
[Limits](limits.md#voice-in-scripts).

## Calls

| Call | Does |
|---|---|
| `voice.record(opts, cb)` | records. `opts` may be `nil`, its keys are in [Record what someone says](../guides/voice-scripts.md#record-what-someone-says). `cb(clip, status)` |
| `voice.stop()` | ends the recording now. The callback gets what it has |
| `voice.level()` | the loudness of the running recording, 0 to 255, otherwise 0 |
| `voice.state()` | `'idle'`, `'recording'` or `'playing'` |
| `voice.send(clip, url, opts, cb)` | sends a clip. `opts` may be `nil`. `cb(body, status)` |
| `voice.fetch(url, opts, cb)` | downloads WAV or MP3 into a clip. `opts` may be `nil`. `cb(clip, status)` |
| `voice.play(clip, done?)` | plays a clip and returns whether it started. `done(heard)` |
| `voice.cancel()` | ends your app's recording, playback, sends and fetches |

A callback that is not a function, `opts` that is not a map or anything but a clip for `voice.play()`
or `voice.send()` raises `value_error`. To send text, use [`http`](../guides/scripting/network.md#http-calls).

## A clip

| Member | Meaning |
|---|---|
| `ms` | the length of a recording in milliseconds, `0` for a download |
| `kind` | `wav` or `mp3` |

## Options of `voice.send`

| Option | Meaning | Default |
|---|---|---|
| `as` | `'form'` or `'raw'` | `'form'` |
| `field` | for a form: the name of the clip's field | `'file'` |
| `fields` | for a form: a map of further text fields, at most 16 | none |
| `method` | the HTTP method | `POST` |
| `headers` | a map of names to values. A form takes no `Content-Type` of yours | none |
| `find`, `keep`, `cap` | as in [`http`](../guides/scripting/network.md#http-calls) | |
| `timeout` | whole seconds for the whole request, also while it waits for the first byte, 1–120. Connecting has its own 3 seconds | 30 |

[`{secret:key}`](../guides/scripting/network.md#api-keys) works in the address and in `headers`, not in
a clip or in `fields`.

## Options of `voice.fetch`

| Option | Meaning | Default |
|---|---|---|
| `body` | the text to send | none |
| `method` | the HTTP method | `POST` with a `body`, else `GET` |
| `headers` | a map of names to values | none |
| `timeout` | whole seconds for the whole request, also while it waits for the first byte, 1–120. Connecting has its own 3 seconds | 15 |

[`{secret:key}`](../guides/scripting/network.md#api-keys) works in the address, in `headers` and in the
`body`. Only a `GET` without a key follows redirects.

## Status values

`voice.send` and `voice.fetch` give the HTTP status of the answer. `0` means no server answered in
time, or the clock refused to send a [key](../guides/scripting/network.md#api-keys). A recording gives
`200`. The negative values are constants of the module:

| Constant | Value | Means |
|---|---|---|
| `voice.SILENCE` | -1 | nobody spoke within `wait`, or `voice.stop()` came before any sound |
| `voice.BUSY` | -2 | the app is not shown, the assistant is busy, another recording runs or the recording failed. Also a third send or fetch of the app |
| `voice.CANCELLED` | -3 | `voice.cancel()`, or the app was hidden while it recorded |
| `voice.FULL` | -4 | the clip, or the memory for a send or fetch, would not fit into the [limits](limits.md#voice-in-scripts), or the clock is short of memory |
| `voice.GONE` | -5 | `voice.send` with a clip that was released |
| `voice.BAD` | -6 | an option out of range or of the wrong type, a bad address, method or header. Also a `voice.fetch` answer with status 200 that is not an [accepted type](limits.md#voice-in-scripts), or neither WAV nor MP3, or too large |

## Related

- [Voice in scripts](../guides/voice-scripts.md): record, send and play, step by step
- [Limits](limits.md#voice-in-scripts)
- [Network](../guides/scripting/network.md#api-keys): API keys with `@secret`
