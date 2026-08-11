# Limits

Every cap AWTRIX enforces, and what it answers when you reach one.

## Requests

| Limit | Value | At the edge |
| --- | --- | --- |
| JSON request body (HTTP) | 8192 bytes | `413 payloadTooLarge`, nothing is applied |
| MQTT command payload | 8192 bytes | dropped before it is parsed: no error, no `/result` reply |
| App and script names | 1–32 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | `400 invalidName` |

The byte-count cap is not the only limit: nested objects and arrays may go at most **16** levels
deep. Deeper nesting makes the body invalid JSON (`400 invalidJson`). There is still no cap on the
number of members at a given level.

What AWTRIX publishes *to you* over MQTT has no size limit; only what you publish to it does.

## Apps and notifications

| Limit | Value | At the edge |
| --- | --- | --- |
| Pushed apps resident | 50 | `507 insufficientStorage`, nothing is stored - delete an app first |
| Notification queue | 32, counting the one on screen | a stacked push is rejected with `507 insufficientStorage`; `stack: false` replaces the notification on screen and is never rejected |
| Notifications per request | 1 | `422 validationFailed` - send one per request |
| `barChart` / `lineChart` points | 16 | the 17th and later entries are dropped, the chart still draws |

The 50 counts **new** names only: replacing a pushed app that already exists always works, whatever
the count says. An array payload is all-or-nothing against the cap - if the new names in the batch
would take the total past 50, the whole request is rejected with `507` and none of its apps are
created or updated.

## Scripting

Berry scripts run under their own caps. How each one behaves in practice is in
[App scripting](../guides/scripting.md).

| Limit | Value | At the edge |
| --- | --- | --- |
| Instructions per entry | 200 000 | the script stops and stays broken until you replace it; nothing else is affected |
| Shared script memory | 96 KB on a board without PSRAM; half the free PSRAM on a board with it | **new** installs refused until it drops; nothing already installed is removed |
| Free memory to install | about 8 KB plus the source; re-saving an existing script, about 4 KB plus the source | install refused, `507` - [what helps](../guides/scripting.md#not-enough-free-memory-to-compile) |
| Memory in one piece | at least the size of the source | install refused, `507`, *"heap too fragmented to compile"* - reboot |
| Memory held back while a script compiles | 24 KB, on a board without PSRAM | install fails with `out of memory` |
| HTTP response body | 8 KB, or `cap` if the request sets one - brought down to the free memory there is when the answer starts arriving | truncated at whichever of the two is smaller - or filtered, see [`find`](../guides/scripting.md#picking-one-field-out-of-a-big-answer) |
| Free memory while a response is collected | enough for the bytes still to come | the whole request fails: the callback gets `nil` and the **real** status code |
| HTTP connect and read timeout | 5 s each | the callback gets `nil, 0` |
| HTTP request unanswered | 30 s | the callback gets `nil, 0`, the slot is freed |
| HTTP requests in flight | 8 per script | `http.get()` calls back `nil, 0` immediately |
| MQTT subscriptions | 8 per script | further `mqtt.subscribe()` calls are ignored |
| MQTT messages waiting | 32, shared by every script | the oldest pending message is dropped |
| Setting key | 1–24 characters of `A–Z`, `a–z`, `0–9`, `_`, starting with a letter | the line is skipped and the settings panel says so |
| Setting text value | 256 characters, or `maxlen=` if you set one | the change is refused, `422`, nothing is written |
| Shared key names | 1–24 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | `shared.set()` returns `false`, nothing changes |

The instruction limit is per **entry into script code** - one `draw()`, one `loop()`, one button
press, one HTTP callback each get the full 200 000 again, and it is not a limit a `try`/`except`
can catch.

## Sounds and radio

| Limit | Value | At the edge |
| --- | --- | --- |
| Melody source | 512 characters | `422 validationFailed` |
| Melody name | 1–24 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | `422 validationFailed` |
| MP3 name | 1–32 characters of `A–Z`, `a–z`, `0–9`, `_`, `-` | refused at upload |
| DFPlayer track | 1–2999 | `422 validationFailed` |
| Radio stations | 32 | `422 validationFailed`, the whole list is rejected |
| Station name | 1–24 characters | `422 validationFailed`, naming the row that failed |
| Station URL | at most 255 characters, `http://` or `https://` | `422 validationFailed`, naming the row that failed |

A station list is applied whole or not at all: one bad row rejects the request and the stations
already on AWTRIX stay as they were.

## Storage

| Limit | Value | At the edge |
| --- | --- | --- |
| Icon and file storage | the free space on AWTRIX - the storage area is **512 KB** on a 4 MB board, 4.5 MB on 8 MB, 12.5 MB on 16 MB | `500 internalError`; no truncated file is left behind |

Which formats are accepted, and how each one is drawn, is in
[Icons & assets](../guides/icons.md).

## Display

| Limit | Value | At the edge |
| --- | --- | --- |
| Panel width | [`panelWidth × panels`](system.md#panel-and-orientation), default `32 × 1`, must come to 32–128 | outside the range: `422 validationFailed` on `panelWidth` |
| Panel height | 8 pixels | fixed; not configurable |
| Icon canvas | 32×8 pixels, regardless of the panel width | a GIF whose **first** frame is larger does not play at all; if a later frame is larger, decoding stops there and AWTRIX loops the frames decoded before it |

## What is *not* limited

- **Requests per second.** Neither the HTTP API nor MQTT rate-limits you.
- **State AWTRIX publishes.** `state/device` and `state/screen` go out at whatever size they are.
- **How long a script may run in total.** Only a single entry into script code is capped; a
  script that returns promptly may run for as long as AWTRIX is on.
