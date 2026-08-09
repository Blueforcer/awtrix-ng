# Sound

AWTRIX has up to three sound outputs, and a panel may have any combination of them:

| Output | Hardware | Plays |
|---|---|---|
| Buzzer | a piezo at `pinBuzzer` | **melodies** - the short ringtones from the Nokia era, typed into the request or stored under a name |
| Speaker | an I2S amplifier | **MP3s** you upload, and internet radio |
| DFPlayer | a DFPlayer Mini on `pinDfRx`/`pinDfTx` | **tracks** from the module's own SD card, by number |

Each output has its own volume, and each answers for itself: asking for a melody on a board with
no buzzer says so rather than playing something else. Which outputs your panel has is in
`audio` in [`GET /api/v1/capabilities`](../reference/http.md#get-apiv1capabilities):

```json
{"audio":{"buzzer":true,"track":false,"mp3":true,"radio":true}}
```

## MP3s

Only on boards with a speaker. Drag them onto the **MP3s** section of the web UI's Audio tab, the way icons are uploaded, or send one from the command line:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/mp3 -F "file=@ding.mp3"
```

The file name without `.mp3` is the name you play it by:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"mp3":"ding"}'
```

The same name works as `sound:"ding"` in a [notification](notifications.md#sound), and as `sound.play("ding")` in a script.

**Name your files carefully.** An MP3 is played by its file name, so it may only use `A-Z`, `a-z`, `0-9`, `_` and `-`, up to 32 characters. `My Song (2024).mp3` is refused when you upload it - rename it to `my-song-2024.mp3` first. An MP3 and a melody may share a name; asking for an `mp3` or a `melody` says which one you mean.

Worth knowing:

- Ordinary MP3 files, the kind any converter produces. Something that is not one is refused when you upload it.
- An MP3 interrupts a running radio stream, and the stream comes back by itself once the MP3 has finished.
- MP3s use the `mp3Volume` setting, the radio uses `radioVolume`. Same amplifier, separate knobs, so a station turned down does not also turn down your doorbell.
- Delete one with `DELETE /api/v1/audio/mp3/ding`, or with the bin next to it in the Audio tab.
- MP3s share the device's storage with icons, melodies and scripts, so keep them to a few seconds. The bar at the top of the Audio tab shows what is left.

## Play a melody right now

Paste this:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"beep:d=4,o=5,b=120:c,e,g"}'
```

Three ascending notes, and:

```json
{"ok":true}
```

`POST /api/v1/audio/play` takes exactly one key, and the key says which output answers:

| Key | Type | What it plays |
|---|---|---|
| `sound` | string | a name, and AWTRIX decides - see [Letting AWTRIX choose](#letting-awtrix-choose) |
| `mp3` | string | a stored MP3, `/MP3/<name>.mp3` |
| `melody` | string | a stored melody, `/MELODIES/<name>.txt` |
| `track` | integer | a DFPlayer track, 1 to 2999 |
| `rtttl` | string | the melody string in the request |

Send **exactly one** key. A body carrying more than one is rejected with `422 validationFailed`
and nothing plays; the `field` in the reply names the first of them in the order above.

The four explicit keys never fall back. Asking for a `melody` will not play an MP3 of the same
name, and a key whose output your panel does not have answers `503 unavailable` - the request was
not wrong, the hardware is simply not there. A malformed `rtttl` or `track` is still `422`, on
every panel: what you sent is checked before the hardware is.

The `Content-Type` header is
[mandatory on every write](../reference/conventions.md#content-type-is-mandatory).
Full status-code table: [Audio](../reference/http.md#audio).

## Letting AWTRIX choose

`sound` is the one key that looks at more than one output. It is also what a notification's
`sound` key and a script's `sound.play()` mean, so all three resolve a name the same way:

1. a stored MP3 called that, if the panel has a speaker;
2. a stored melody called that, if the panel has a buzzer;
3. a DFPlayer track, if the name is a plain number from 1 to 2999 and the panel has a DFPlayer;
4. otherwise `404 nothing called "…"`.

The DFPlayer comes last because it is the only one AWTRIX cannot check first: the module owns its
SD card and there is no way to ask it what is on there. Putting it earlier would swallow every
numeric name and make `/MP3/7.mp3` unreachable by name.

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
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
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
curl -X POST http://<awtrix-ip>/api/v1/audio/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"bell:d=4,o=5,b=100:e,c"}'

# Descending "something went wrong"
curl -X POST http://<awtrix-ip>/api/v1/audio/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"loose:d=8,o=5,b=120:16c,16b,16a,4g"}'

# Jackpot fanfare
curl -X POST http://<awtrix-ip>/api/v1/audio/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"jackpot:d=8,o=5,b=120:16c,16e,16g,c6,16p,16c6,16e6,4g6"}'
```

A melody string is capped at **512 characters**, whether it arrives inline or sits in a file. See [Limits](../reference/limits.md).

### Stop one

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/stop
```

Silences everything, radio included. An optional `scope` narrows it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/stop \
  -H 'Content-Type: application/json' \
  -d '{"scope":"sounds"}'
```

| `scope` | Stops |
|---|---|
| `all` | one-shot sounds **and** the radio stream (the default) |
| `sounds` | melodies, MP3s and tracks; a radio stream keeps playing |
| `stream` | the radio stream only |

Stopping ignores `soundEnabled`, so a playing melody can always be stopped.

## Melody files

Instead of sending a melody every time, store it on AWTRIX and play it by name.

### The Audio tab

The web UI has an editor for this - one row per melody stored on AWTRIX, with the name in one field and the melody in the other. It checks the string as you type, plays it in your browser without sending anything, and plays it out loud when you want to hear it for real. See [Melodies](../getting-started/web-ui.md#melodies).

### Save one

```bash
curl -X PUT http://<awtrix-ip>/api/v1/audio/melodies/doorbell \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"d=4,o=5,b=100:e,c"}'
```

`201` when the melody is new, `200` when it replaced one, `422` when the string does not parse.

The melody is stored at `/MELODIES/doorbell.txt`, and its title is set to the name you filed it under. Send `d=4,o=5,b=100:e,c` and the file holds `doorbell:d=4,o=5,b=100:e,c`; send `something-else:d=4,o=5,b=100:e,c` and it still holds `doorbell:…`.

Names are 1 to 24 characters of `A-Z`, `a-z`, `0-9`, `_` and `-`.

### List them

```bash
curl http://<awtrix-ip>/api/v1/audio/melodies
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
curl -X DELETE http://<awtrix-ip>/api/v1/audio/melodies/doorbell
```

`404 notFound` if there is no such melody. To **rename**, save under the new name and delete the old one; there is no rename route.

### Play one

Pass the file's name **without the `.txt`**:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"melody":"doorbell"}'
```

Asking for a `melody` never reaches an MP3 of the same name. If no melody called `doorbell` is stored - or it is stored but AWTRIX cannot read it - you get:

```json
{"error":{"code":"notFound","message":"no melody called \"doorbell\""}}
```

with status `404`. On a panel with no buzzer at all the answer is `503 unavailable` instead.

## Volume

One setting per output, all on the same 0–100 scale, applied the moment you write it:

| Key | Range | Default | Meaning |
|---|---|---|---|
| `buzzerVolume` | 0–100 | `80` | melodies on the buzzer |
| `dfplayerVolume` | 0–100 | `80` | tracks on the DFPlayer |
| `mp3Volume` | 0–100 | `70` | stored MP3s on the speaker |
| `radioVolume` | 0–100 | `60` | internet radio on the speaker |

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"buzzerVolume":40}'
```

Out-of-range values are rejected with `422`; you do not have to clamp them yourself. `0` is a
valid volume, not a mute. The web UI shows a slider only for the outputs your panel has. See
[Sound](../reference/settings.md#sound).

## Muting

`soundEnabled` (default `true`) mutes one-shot sounds - MP3s, melodies, tracks and a
notification's own melody. A **radio stream keeps playing**: you started that one deliberately,
while a notification arrives uninvited.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"soundEnabled":false}'
```

While muted, AWTRIX still accepts every sound command and still answers `200 {"ok":true}`. That includes a name that does not exist, which would otherwise answer `404` - so test your names with sound **on**. Use `{"scope":"stream"}` on `/audio/stop` to silence the radio as well.

For genuine silence on the buzzer, set `pinBuzzer` to `-1` in the [pin map](../reference/gpio.md#the-pin-map). With no buzzer pin, nothing is ever played there.

## Sound in notifications and apps

A notification can carry its own melody, so you do not need a second request:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"DOORBELL","soundRtttl":"bell:d=4,o=5,b=100:e,c"}'
```

| Key | Type | Default | Meaning |
|---|---|---|---|
| `sound` | string \| int | `""` | a name, resolved as in [Letting AWTRIX choose](#letting-awtrix-choose) |
| `soundRtttl` | string | `""` | Inline RTTTL melody. A melody that does not parse is rejected with `422 validationFailed` and nothing is pushed |
| `soundLoop` | bool | `false` | Re-trigger the melody each time it finishes, while the notification is shown |

`soundRtttl` wins if both are present. The melody fires when the notification first appears; `soundLoop: true` restarts it whenever it finishes, for as long as the notification is on screen. Pair it with `hold: true` for an alarm that keeps going until someone dismisses it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALARM","soundRtttl":"a:d=4,o=5,b=200:c,p,c,p","soundLoop":true,"hold":true,"textColor":"#FF0000"}'
```

Notification melodies respect `soundEnabled` - with sound off, the notification is shown silently.

These three are **notification-only keys** - a pushed app that sends one is rejected with `422 validationFailed`. Apps do not make sound. See [Notification-only keys](../reference/payload.md#notification-only-keys).

## Over MQTT

Same body, same keys, same exactly-one rule:

```bash
mosquitto_pub -h <broker> -t 'awtrixNG/cmd/audio/play' \
  -m '{"rtttl":"beep:d=4,o=5,b=120:c,e,g"}'
```

Errors come back on `awtrixNG/cmd/audio/play/result` as `{"ok":false,"error":{…}}`. Storing and listing melody files are HTTP-only. See [Command topics](../reference/mqtt.md#command-topics).

## DFPlayer boards

An AWTRIX 2 mainboard conversion can drive a DFPlayer Mini MP3 module. AWTRIX talks to it when `dfplayer` is `true` **and** both `pinDfRx` and `pinDfTx` are set. The buzzer keeps working alongside it - the two outputs share nothing, so adding the module costs you no melodies. See [Sound hardware](../reference/system.md#sound-hardware) and [The pin map](../reference/gpio.md#the-pin-map).

A DFPlayer plays numbered files from its own SD card and cannot produce notes:

- `track` is its key. `{"track":3}` plays track 3; anything outside 1–2999 is `422 validationFailed`.
- The module owns its card, so AWTRIX cannot tell you whether a track is on it. `{"track":3}` on an empty card answers `200` and stays silent.
- `rtttl` and `melody` go to the buzzer as they always do. On a panel that has no buzzer they answer `503 unavailable`, because there is no output for the notes.
- A notification's `sound` reaches the DFPlayer when the name is a plain number and nothing is stored under that name - see [Letting AWTRIX choose](#letting-awtrix-choose).

`dfplayerVolume` sets its level, and `soundEnabled` mutes it like every other one-shot output.

## Related

- [Audio](../reference/http.md#audio) - full status codes for every audio route
- [Audio tab](../getting-started/web-ui.md#audio) - MP3 sounds and the melody editor
- [Sound](../reference/settings.md#sound) - the four volumes and `soundEnabled`
- [Sound hardware](../reference/system.md#sound-hardware) - `dfplayer` and the buzzer pins
- [Notification-only keys](../reference/payload.md#notification-only-keys) - `sound`, `soundRtttl`, `soundLoop`
