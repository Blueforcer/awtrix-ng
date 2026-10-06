# Limits

Every cap AWTRIX enforces: how big or how many something may be, and what happens when you reach
it.

## Requests

| Limit | Cap | When you hit it |
| --- | --- | --- |
<!-- only esp32 esp32-s3 -->
| JSON request body (HTTP) | 8192 bytes | `413 payloadTooLarge`, nothing is applied |
<!-- /only -->
<!-- only tc002 -->
| Request body (HTTP) | 2 MiB for any request body, JSON or upload | `413 payloadTooLarge`, nothing is applied |
<!-- /only -->
| MQTT command payload | 8192 bytes for the whole message, topic included | the message is dropped unread: no error, no `/result` reply |
| App and script names | 1–32 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | `400 invalidName` |
| JSON nesting | 16 levels of objects and arrays | `400 invalidJson` |
<!-- only tc002 -->
| Update package (`.awup`) | 8 MiB | `413 payloadTooLarge`, nothing is installed |
<!-- /only -->

There is no cap on how many keys or array entries a body may hold, as long as it fits the size
limit.

<!-- only tc002 -->
A body larger than 64 KiB is received one at a time. If a second large body arrives while the
first is still coming in, it gets `503 serviceBusy` with `Retry-After: 2`. Send it again a moment
later.
<!-- /only -->

What AWTRIX publishes *to you* over MQTT has no size limit. Only what you send to it is capped.

## Apps and notifications

| Limit | Cap | When you hit it |
| --- | --- | --- |
| Pushed apps resident | 50 | `507 insufficientStorage`, nothing is stored - delete an app first |
| Notification queue | 32, counting the one shown | a new notification with `stack: true` gets `507 insufficientStorage`; `stack: false` replaces the one shown and always works |
| Notifications per request | 1 | `422 validationFailed` - send one per request |
| `barChart` / `lineChart` points | 16 | the 17th and later entries are dropped; the chart still draws |
| Additional positioned icons (`icons`) | 4 per pushed app or notification, plus the ordinary `icon` | `422 validationFailed` on `icons`; the whole request is rejected |

The 50 counts **new** names only. Replacing a pushed app that already exists always works.

An array payload is all-or-nothing: if its new names would take the total past 50, the whole
request gets `507` and none of its apps are created or updated.

<!-- only tc002 -->
## Pictures from the internet

These apply to an `icon` that is a web address.

| Limit | Cap | When you hit it |
| --- | --- | --- |
| Address | 2048 characters, `http://` or `https://`, no spaces | `422 validationFailed` on `icon` |
| File size | 1 MB | the picture is not shown; the log names the server |
| Download time | 15 seconds | the picture is not shown; the log names the server |
| JPEG | 8192 × 8192 pixels. A progressive JPEG depends on how it was saved: 1920 × 1280 with the usual color subsampling (4:2:0), 1600 × 1200 with 4:2:2, 1000 × 1000 without subsampling (4:4:4) | the picture is not shown |
| PNG | 4 million pixels, 2048 × 2048 for example, not interlaced | the picture is not shown |
| GIF | 8 KB, and the size of the square or region it fills | the picture is not shown |
| Pictures kept in memory | 64 pictures and 128 KB, each size of an address counting as one picture | the picture shown longest ago makes room and is downloaded again when it is next shown |
| Downloads at a time | 1, with up to 8 waiting | the others wait their turn |
<!-- /only -->

## Scripting

These caps apply to Berry scripts. How to work within them is explained in
the [Scripting guide](../guides/scripting/index.md).
<!-- only tc002 -->
Layouts have their own budgets, listed in `capabilities.layouts.limits`. See
[Layouts](../guides/layouts.md#limits).
<!-- /only -->

| Limit | Cap | When you hit it |
| --- | --- | --- |
| Instructions per entry | <!-- only esp32 esp32-s3 -->200 000<!-- /only --><!-- only tc002 -->2 000 000<!-- /only --> | the script stops until you save a new version, save its settings or data, or restart the device; nothing else is affected |
<!-- only esp32 -->
| Shared script memory | 96 KiB (98304 bytes) without usable PSRAM, about half the free PSRAM with it. Read the value in `scriptHeapBudgetBytes` | **new** installs are refused until memory is free again; installed scripts keep running |
<!-- /only -->
<!-- only esp32-s3 -->
| Shared script memory | about half the free PSRAM. Read the value in `scriptHeapBudgetBytes` | **new** installs are refused until memory is free again; installed scripts keep running |
<!-- /only -->
<!-- only tc002 -->
| Shared script memory | a quarter of the memory available at startup, at most 4 MiB. Read the value in `scriptHeapBudgetBytes` | **new** installs are refused until memory is free again; installed scripts keep running |
<!-- /only -->
| Free memory to install | about 8 KB plus the size of the source; re-saving an existing script, about 4 KB plus the source | the install is refused with `507` - see [what helps](../guides/scripting/troubleshooting.md#not-enough-memory-to-compile) |
<!-- only esp32 esp32-s3 -->
| Memory in one piece | at least the size of the source | the install is refused with `507` and *"heap too fragmented to compile"* - restart AWTRIX and try again |
<!-- /only -->
<!-- only esp32 -->
| Memory held back while a script compiles | 24 KiB (24576 bytes), on a board without usable PSRAM | the install fails with `out of memory` |
<!-- /only -->
| HTTP response body | 8 KB, or `cap` if the request sets one - lowered to the free memory when the answer starts | the body is cut at that size. To keep only the part you need, use [`find`](../guides/scripting/network.md#picking-one-field-out-of-a-big-answer) |
| Free memory while a response arrives | enough for the rest of the body | the whole request fails: the callback gets `nil` and the **real** status code |
| HTTP connect and read timeout | <!-- only esp32 esp32-s3 -->5 s each<!-- /only --><!-- only tc002 -->3 s to connect, 5 s to read<!-- /only --> | the callback gets `nil, 0` |
| HTTP request unanswered | 30 s | the callback gets `nil, 0` |
| HTTP requests in flight | 8 per script | `http.get()` calls back `nil, 0` at once |
| Script timers | 8 per app, 32 in total; each 25 ms to 1 day | `timer.after()` and `timer.every()` return `nil` |
| MQTT subscriptions | 8 per script | further `mqtt.subscribe()` calls are ignored |
| MQTT messages waiting | 32, shared by all scripts | the oldest waiting message is dropped |
| Setting key | 1–24 characters of `A–Z`, `a–z`, `0–9`, `_`, starting with a letter | the line is skipped and the settings show why |
| Setting text value | 256 characters, or `maxlen=` if you set one | `422`, nothing is saved |
| Shared key names | 1–24 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | `shared.set()` returns `false`, nothing changes |
<!-- only esp32-s3 tc002 -->
| Music bands | 32 | `music.bands(n)` returns at most 32 values; a smaller `n` merges neighboring bands |
<!-- /only -->
<!-- only tc002 -->
| Pitch range | 70–1600 Hz | `music.pitch()` returns `0.0` for a note outside it |
<!-- /only -->
| Different script icons per frame | 4 icon IDs | `icon()` returns `false` for a 5th different ID in the same `draw()` |

The instruction limit counts per **entry into your script**: each `draw()`, each `loop()`, each
button press and each HTTP callback starts again at the platform's limit. A `try`/`except` cannot catch it.

The same script icon ID drawn at several positions counts as one icon. All copies animate
together.

## Sounds<!-- only esp32-s3 tc002 --> and radio<!-- /only --> {#sounds-and-radio}

The messages for rejected sounds are listed in [Audio playback errors](errors.md#audio-playback).

| Limit | Cap | When you hit it |
| --- | --- | --- |
| Melody source | 512 characters | `422 validationFailed` |
| Melody name | 1–24 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | `422 validationFailed` |
| MP3 name | 1–32 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | `400 invalidName`, the upload is refused |
| MP3 and melody names | an MP3 in `/MP3` and a melody never share a name | `409 nameTaken`, nothing is stored |
| Entries in a sound list | 1–4 | `422 validationFailed` |
<!-- only tc002 -->
| Speech text | 1–512 bytes | `422 validationFailed` |
| MP3 from an address (`file`) | 4 MB, and only while enough memory is free | nothing plays, `alert.error` or `app.error` says `file too large` or `not enough memory` |
| MP3 effects at once (`sound.effect()`) | 4 | a fifth stops the oldest |
| Length of one MP3 effect | 10 s | the rest is not played |
| Background music at once (`sound.play()` with `loop`) | 1: an MP3 or a song | a different MP3 or song replaces it; the same name or song text changes nothing |
| Song effects at once (`sound.effect()`) | 4, in addition to the four MP3 effects | a fifth stops the oldest song effect |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| Song text | 16384 bytes | `422 validationFailed`; in a script, the call raises `value_error` |
| Tracks / instruments in a song | 16 / 32 | `422 validationFailed` |
| Notes in a song, repeats counted out | 8192 | `422 validationFailed` |
| Song length | 1024 bars | `422 validationFailed` |
| Notes in a chord | 8 | `422 validationFailed` |
| Song echo delay | 2 s at the song's tempo | `422 validationFailed` |
<!-- /only -->
<!-- only esp32-s3 -->
| Notes sounding at once | 24 | a new note replaces the quietest fading note, or else the oldest |
<!-- /only -->
<!-- only tc002 -->
| Notes sounding at once | 24 for a song, 8 for an effect | a new note replaces the quietest fading note, or else the oldest |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| DFPlayer track | 1–2999 | `422 validationFailed` |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| Radio stations | 32 | `422 validationFailed`, the whole list is rejected |
| Station name | 1–24 characters | `422 validationFailed`, naming the row that failed |
| Station URL | at most 255 characters, starting with `http://` or `https://` | `422 validationFailed`, naming the row that failed |

A station list is saved whole or not at all. One bad row rejects the request, and the stations
already on AWTRIX stay as they were.
<!-- /only -->

<!-- only esp32-s3 -->
A song sent over HTTP or MQTT must also fit the 8192-byte body or message, JSON and escapes
included. From a script, the full 16384 bytes work. Every song limit, with its error message, is
listed in [Song text](songs.md#limits).
<!-- /only -->
<!-- only tc002 -->
A song sent over MQTT must also fit the 8192-byte message, JSON and escapes included. Over HTTP
and from a script, the full 16384 bytes work. Every song limit, with its error message, is listed
in [Song text](songs.md#limits).
<!-- /only -->

## Storage

| Limit | Cap | When you hit it |
| --- | --- | --- |
<!-- only esp32 esp32-s3 -->
| Icon and file storage | the free space on AWTRIX. The storage area is **512 KB** on a 4 MB board, 4.5 MB on 8 MB, 12.5 MB on 16 MB | `507 insufficientStorage`; no half-written file is left behind |
<!-- /only -->
<!-- only tc002 -->
| Icon and file storage | the free space on AWTRIX | `507 insufficientStorage`; no half-written file is left behind |
| Space kept free | **1 MiB** of the storage, for settings, Wi-Fi setup and updates | an upload that would use it gets `507 insufficientStorage`. A script that would use it still runs, but is saved only once there is room again |
| Script store | **64 KiB** per script - everything it keeps with [`store`](../guides/scripting/storage.md#storage) | the store is not saved and the log says `store not saved`. The script keeps running with its values; after a restart it gets back the last store that was saved |

The `totalBytes` of a file listing already leaves out the space kept free.
<!-- /only -->

Which file formats are accepted is listed in [Icons](../guides/icons.md).

## Backup restore

| Limit | Cap | When you hit it |
| --- | --- | --- |
| `manifest.json` | 4 KiB | the whole backup is rejected |
| Each settings, configuration, app-order or radio JSON file in the backup | 64 KiB | that file is skipped with a warning |
| `config/icon-origins.json` | 16 KiB | that file is skipped with a warning |
| Detailed restore warnings | 128 | one extra message says how many more were left out |

Icons, sounds and other asset files have no size cap of their own here. They only need to fit
the free storage and follow their format's rules. If a file cannot be written, the old file stays.

A restore is applied file by file. If it stops halfway, the files restored before that point are
already changed.

## Display

| Limit | Cap | When you hit it |
| --- | --- | --- |
<!-- only esp32 esp32-s3 -->
| Panel width | [`panelWidth × panels`](system.md#panel-and-orientation), default `32 × 1`, total 32–128 | `422 validationFailed` |
| Display height | 8, cannot be changed | - |
| Total pixels | at most 1024 (128 × 8) | follows from width and height. Read the limits of your device in `capabilities.display` |
<!-- /only -->
<!-- only tc002 -->
| Display size | 52 × 16 = 832 pixels, cannot be changed | - |
<!-- /only -->
| GIF size | the display's width and height | resize larger GIFs before uploading; every frame must fit |

## What is *not* limited

- **Requests per second.** Neither HTTP nor MQTT limits how often you send.
- **Messages AWTRIX publishes.** `state/device` and `state/screen` are sent at whatever size they
  are.
- **How long a script runs in total.** Only a single entry into the script is capped. A script
  that returns quickly can run for as long as AWTRIX is on.

## Related

- [Errors](errors.md) - every error code and what it means
- [App & notification payload](payload.md)
- [Scripting guide](../guides/scripting/index.md)
