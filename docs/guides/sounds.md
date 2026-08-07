# Sounds & melodies

AWTRIX plays two kinds of sound: **MP3 clips** on boards with a speaker (an I2S DAC, the same one the [internet radio](radio.md) uses), and **RTTTL melodies** - the ringtone format from Nokia phones - on the buzzer every board has. A melody can arrive as an inline string, as a melody file stored on AWTRIX, or as the built-in R2D2 chirp.

Everything below the [MP3 sounds](#mp3-sounds) section describes a board with a passive buzzer on `pinBuzzer`. If your board is an AWTRIX 2 conversion with a DFPlayer Mini module, read [DFPlayer boards](#dfplayer-boards) first - the same keys mean different things there.

## MP3 sounds

Only on boards with a speaker; without one, the section below does not apply and `name` always means a melody. Upload short MP3 clips in the web UI's Audio tab (drag & drop, like icons) or with the file API:

```bash
curl -X POST "http://<awtrix-ip>/api/v1/files?dir=/SOUNDS" -F "file=@ding.mp3"
```

The clip lands at `/SOUNDS/ding.mp3`, and the file name without `.mp3` is what you play:

```bash
curl -X POST http://<awtrix-ip>/api/v1/sounds/play \
  -H 'Content-Type: application/json' \
  -d '{"name":"ding"}'
```

The same name works as `sound:"ding"` in a [notification](notifications.md#sound). Names are up to 32 characters of `A-Z`, `a-z`, `0-9`, `_` and `-`.

How `name` is resolved on a speaker build: AWTRIX looks for `/SOUNDS/<name>.mp3` first and falls back to the melody file `/MELODIES/<name>.txt`. A clip with the same name as a melody wins.

Worth knowing:

- The upload accepts MP3 (MPEG-1 Layer III) only - the usual 32/44.1/48 kHz files from any converter. A file that only pretends to be one uploads fine but stays silent and leaves a note in the log.
- If the radio is streaming, a clip interrupts it; the stream reconnects by itself once the clip ends, with a short gap while it rebuffers.
- Clips use the `radioVolume` setting, not `volume` - one speaker, one knob.
- List and delete go through the same file API as icons: `GET /api/v1/files?dir=/SOUNDS` and `DELETE /api/v1/files?path=/SOUNDS/ding.mp3`. See [Files](../reference/http.md#files).
- Clips share flash with everything else, so keep them short - a few seconds each. The storage bar in the Audio tab shows where you stand.

## Play a melody right now

Paste this:

```bash
curl -X POST http://<awtrix-ip>/api/v1/sounds/play \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"beep:d=4,o=5,b=120:c,e,g"}'
```

Three ascending notes, and:

```json
{"ok":true}
```

`POST /api/v1/sounds/play` takes exactly one of three keys:

| Key | What it plays |
|---|---|
| `rtttl` | the melody string in the request |
| `name` | a stored sound - `/SOUNDS/<name>.mp3` on speaker builds first, else `/MELODIES/<name>.txt` |
| `builtin` | the R2D2 melody - see [below](#the-r2d2-builtin) |

Send **exactly one** key. A body carrying more than one is rejected with `422 validationFailed` and nothing plays.

The `Content-Type` header is
[mandatory on every write](../reference/conventions.md#content-type-is-mandatory).
Full status-code table: [Sounds](../reference/http.md#sounds).

## Writing RTTTL

An RTTTL melody is three colon-separated parts, and **all three are required**:

```
name:defaults:notes
```

```
beep:d=4,o=5,b=120:c,e,g
└─┬─┘ └──────┬─────┘ └─┬─┘
name    defaults     notes
```

- **name** - 1 to 24 characters. It is not played, but it must not be empty.
- **defaults** - `d` = default note duration, `o` = default octave, `b` = beats per minute. Each may appear at most once, in any order, and any of them may be left out.
- **notes** - comma-separated. Each note is an optional duration, a letter `a`–`g` (`p` = pause), an optional `#`, an optional `.` for a dotted note, and an optional octave. `16c6` = a 16th-note C in octave 6. A note that omits duration or octave inherits the defaults.

Every value has a fixed set it must come from:

| Element | Allowed | If omitted |
|---|---|---|
| `d` and any note's duration | 1, 2, 4, 8, 16, 32 | `d`, default `4` |
| `o` and any note's octave | 4, 5, 6, 7 | `o`, default `6` |
| `b` | 10–300 | default `63` |
| note letter | `a`–`g`, or `p` for a rest | - |

Anything outside those sets is refused, including `b#`, `e#`, a sharpened rest and a duration such as `3`. The reply names the reason and the position in the string:

```bash
curl -X POST http://<awtrix-ip>/api/v1/sounds/play \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"d=4,o=5,b=120:c,e,g"}'
```
```json
{"error":{"code":"validationFailed",
          "message":"missing ':' before the notes (at offset 19)",
          "field":"rtttl"}}
```

with status `422`. That string is the classic mistake - defaults and notes with no name in front.

Some melodies to try:

```bash
# Two-tone doorbell
curl -X POST http://<awtrix-ip>/api/v1/sounds/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"bell:d=4,o=5,b=100:e,c"}'

# Descending "something went wrong"
curl -X POST http://<awtrix-ip>/api/v1/sounds/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"loose:d=8,o=5,b=120:16c,16b,16a,4g"}'

# Jackpot fanfare
curl -X POST http://<awtrix-ip>/api/v1/sounds/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"jackpot:d=8,o=5,b=120:16c,16e,16g,c6,16p,16c6,16e6,4g6"}'
```

A melody string is capped at **512 characters**, whether it arrives inline or sits in a file. See [Limits](../reference/limits.md).

### Stop one

```bash
curl -X POST http://<awtrix-ip>/api/v1/sounds/stop
```

Silences whatever is playing. It ignores `soundEnabled`, so a playing melody can always be stopped.

## Melody files

Instead of sending a melody every time, store it on AWTRIX and play it by name.

### The Audio tab

The web UI has an editor for this - one row per melody stored on AWTRIX, with the name in one field and the melody in the other. It checks the string as you type, plays it in your browser without sending anything, and plays it out loud when you want to hear it for real. See [Melodies](../getting-started/web-ui.md#melodies).

### Save one

```bash
curl -X PUT http://<awtrix-ip>/api/v1/sounds/doorbell \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"d=4,o=5,b=100:e,c"}'
```

`201` when the melody is new, `200` when it replaced one, `422` when the string does not parse.

The melody is stored at `/MELODIES/doorbell.txt`, and its title is set to the name you filed it under. Send `d=4,o=5,b=100:e,c` and the file holds `doorbell:d=4,o=5,b=100:e,c`; send `something-else:d=4,o=5,b=100:e,c` and it still holds `doorbell:…`.

Names are 1 to 24 characters of `A-Z`, `a-z`, `0-9`, `_` and `-`.

### List them

```bash
curl http://<awtrix-ip>/api/v1/sounds
```

```json
{"melodies":[{"name":"doorbell","rtttl":"doorbell:d=4,o=5,b=100:e,c",
              "bytes":26,"notes":2,"durationMs":2400,"valid":true}],
 "usedBytes":41216,"totalBytes":1048576}
```

`notes` and `durationMs` come from parsing the file, so you can see how long a melody runs without playing it. A melody that does not parse is listed anyway, with `valid:false` plus `error` and `index`, so you can find and repair it in the editor.

`usedBytes` and `totalBytes` cover the whole filesystem - melodies share a small flash partition with your icons, palettes and scripts, so keep an eye on it.

### Delete one

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/sounds/doorbell
```

`404 notFound` if there is no such melody. To **rename**, save under the new name and delete the old one; there is no rename route.

### Play one

Pass the file's name **without the `.txt`**:

```bash
curl -X POST http://<awtrix-ip>/api/v1/sounds/play \
  -H 'Content-Type: application/json' \
  -d '{"name":"doorbell"}'
```

On a speaker build an MP3 clip called `doorbell` would play instead - see [MP3 sounds](#mp3-sounds). If `/MELODIES/doorbell.txt` does not exist - or exists but does not parse - you get:

```json
{"error":{"code":"notFound","message":"sound not found"}}
```

with status `404`.

## The R2D2 builtin

There is exactly **one** built-in sound:

```bash
curl -X POST http://<awtrix-ip>/api/v1/sounds/play \
  -H 'Content-Type: application/json' \
  -d '{"builtin":"r2d2"}'
```

The value is ignored - `{"builtin":"r2d2"}`, `{"builtin":"hello"}` and `{"builtin":""}` all play the same melody:

```
r2d2:d=4,o=5,b=240:16c6,16g6,16e6,16a6,16g6,16e7
```

Send that string as `rtttl` for the same result, and change it to taste.

## Volume

One setting, applied the moment you write it:

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `volume` | integer | 0–30 | `25` | - | Buzzer / DFPlayer volume |

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"volume":10}'
```

Out-of-range values are rejected with `422`; you do not have to clamp them yourself. `volume: 0` is a valid volume, not a mute. See [Sound](../reference/settings.md#sound).

## Muting

`soundEnabled` (default `true`) is the global mute. It covers every trigger - `rtttl`, `name`, `builtin`, and a notification's own melody:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"soundEnabled":false}'
```

While muted, AWTRIX still accepts every sound command and still answers `200 {"ok":true}`. That includes `{"name": …}` for a melody that does not exist, which would otherwise answer `404 sound not found` - so test your melody names with sound **on**.

For genuine silence, set `pinBuzzer` to `-1` in the [pin map](../reference/gpio.md#the-pin-map). With no buzzer pin, nothing is ever played.

## Sound in notifications and apps

A notification can carry its own melody, so you do not need a second request:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"DOORBELL","soundRtttl":"bell:d=4,o=5,b=100:e,c"}'
```
<!-- shot:begin id=doorbell hash=2a3e87d6 -->
![The panel showing "DOORBELL"](../assets/shots/sounds/doorbell.png){ .shot }
<!-- shot:end -->


| Key | Type | Default | Meaning |
|---|---|---|---|
| `sound` | string \| int | `""` | MP3 sound or melody file name (or DFPlayer track) |
| `soundRtttl` | string | `""` | Inline RTTTL melody |
| `soundLoop` | bool | `false` | Re-trigger the melody each time it finishes, while the notification is shown |

`soundRtttl` wins if both are present. The melody fires when the notification first appears; `soundLoop: true` restarts it whenever it finishes, for as long as the notification is on screen. Pair it with `hold: true` for an alarm that keeps going until someone dismisses it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALARM","soundRtttl":"a:d=4,o=5,b=200:c,p,c,p","soundLoop":true,"hold":true,"textColor":"#FF0000"}'
```
<!-- shot:begin id=alarm hash=ae1d028f -->
![The panel showing "ALARM" in red](../assets/shots/sounds/alarm.png){ .shot }
<!-- shot:end -->


Notification melodies respect `soundEnabled` - with sound off, the notification is shown silently.

These three are **notification-only keys** - a pushed app that sends one is rejected with `422 validationFailed`. Apps do not make sound. See [Notification-only keys](../reference/payload.md#notification-only-keys).

## Over MQTT

Same body, same three keys, same exactly-one rule:

```bash
mosquitto_pub -h <broker> -t 'awtrixNG/cmd/sounds/play' \
  -m '{"rtttl":"beep:d=4,o=5,b=120:c,e,g"}'
```

Errors come back on `awtrixNG/cmd/sounds/play/result` as `{"ok":false,"error":{…}}`. Storing and listing melody files are HTTP-only. See [Command topics](../reference/mqtt.md#command-topics).

## DFPlayer boards

An AWTRIX 2 mainboard conversion drives a DFPlayer Mini MP3 module instead of a buzzer. AWTRIX uses it when `dfplayer` is `true` **and** both `pinDfRx` and `pinDfTx` are set; if either pin is `-1` you get the buzzer. See [Sound hardware](../reference/system.md#sound-hardware) and [The pin map](../reference/gpio.md#the-pin-map).

A DFPlayer Mini plays MP3 files off an SD card and cannot produce notes, so nothing RTTTL-shaped makes a sound on that board:

- `rtttl` and `builtin` answer `422` rather than pretending to play;
- a notification's `soundRtttl` has no request to fail, so it is simply silent;
- `name` is the only key that works, and it means a **track number**, not a file in `/MELODIES`. `{"name":"3"}` plays `/MP3/0003.mp3`; anything that is not a positive number returns `404 sound not found`. A notification's `sound` key works the same way.

`volume` and `soundEnabled` behave exactly as they do on the buzzer.

## Related

- [Sounds](../reference/http.md#sounds) - full status codes for every sounds route
- [Audio tab](../getting-started/web-ui.md#audio) - MP3 sounds and the melody editor
- [Sound](../reference/settings.md#sound) - `volume` and `soundEnabled`
- [Sound hardware](../reference/system.md#sound-hardware) - `dfplayer` and the buzzer pins
- [Notification-only keys](../reference/payload.md#notification-only-keys) - `sound`, `soundRtttl`, `soundLoop`
