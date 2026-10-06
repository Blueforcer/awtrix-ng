# Sound

<!-- only esp32 -->
This page shows how to play sounds on your clock: melodies, and tracks from a DFPlayer module. It
also shows how loud each kind of sound plays, and what happens when two sounds meet.
<!-- /only -->
<!-- only esp32-s3 -->
This page shows how to play sounds on your clock: MP3 files, melodies, songs and tracks from a
DFPlayer module. It also shows how loud each kind of sound plays, and what happens when two sounds meet.
<!-- /only -->
<!-- only tc002 -->
This page shows how to play sounds on your clock: MP3 files, melodies, songs and speech. It also
shows how loud each kind of sound plays, and what happens when two sounds meet.

See and hear it:

<div class="video">
  <iframe src="https://www.youtube-nocookie.com/embed/djzf0yz76sc" title="AWTRIX NG Sound Showcase" loading="lazy" allow="accelerometer; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share" referrerpolicy="strict-origin-when-cross-origin" allowfullscreen></iframe>
</div>
<!-- /only -->

A few words this page uses:

<!-- only esp32 esp32-s3 -->
- **Alert:** a sound you ask for from outside the clock. A notification's sound and a request to
  `/api/v1/audio/play` are alerts.
<!-- /only -->
<!-- only tc002 -->
- **Alert:** a sound you ask for from outside the clock. A notification's sound, a request to
  `/api/v1/audio/play`, the boot sound and the answer of Home Assistant Voice are alerts.
<!-- /only -->
- **App sound:** a sound a script plays.
<!-- only esp32-s3 tc002 -->
- **Radio:** an internet radio station. See [Internet radio](radio.md).
<!-- /only -->
- **Master volume:** the volume of the whole clock. Every other volume is a share of it.
- **RTTTL:** a short text format for ringtones, from old mobile phones.

## How it behaves

Every sound is described the same way, whether you play it with a request, over MQTT, with a
notification or from a script.
<!-- only esp32 -->
It plays at the master volume times the share of its group: alerts or app sounds. The clock plays
one sound at a time ([What plays over what](#what-plays-over-what)). A new alert replaces the one
that plays, and a script's sound never cuts an alert off. A sound plays once, unless you add
`"loop": true`.
<!-- /only -->
<!-- only esp32-s3 -->
It plays at the master volume times the share of its group: alerts, app sounds or the radio. The
clock plays one sound at a time, and the radio pauses while another sound plays
([What plays over what](#what-plays-over-what)). A new alert replaces the one that plays, and a
script's sound never cuts an alert off. A sound plays once, unless you add `"loop": true`, and a
station plays until you stop it.
<!-- /only -->
<!-- only tc002 -->
It plays at the master volume times the share of its group: alerts, app sounds or the radio.
Several sounds can play at once: a script's music and effects get quieter under an alert, and the
radio pauses while an alert or a script's sound plays
([What plays over what](#what-plays-over-what)). A new alert replaces the one that plays. A sound
plays once, unless you add `"loop": true`, and a station plays until you stop it.
<!-- /only -->

## What your clock can play

Which sounds your clock can play depends on its hardware. The web UI shows only what your clock
can play. To see it yourself, look at `audio` in
[`GET /api/v1/capabilities`](../reference/http.md#get-apiv1capabilities):

<!-- only esp32 -->
```json
{"audio":{"mp3":false,"rtttl":true,"song":false,"speech":false,"track":false,
          "radio":false,"url":false,"effect":false,"clip":false}}
```

| Flag | Your clock can |
|---|---|
| `rtttl` | play melodies on the buzzer |
| `track` | play numbered tracks from a DFPlayer module |

Every flag is always there, `true` or `false`. The other flags are always `false` on this clock.
<!-- /only -->
<!-- only esp32-s3 -->
```json
{"audio":{"mp3":true,"rtttl":true,"song":true,"speech":false,"track":false,
          "radio":true,"url":false,"effect":false,"clip":false}}
```

| Flag | Your clock can |
|---|---|
| `mp3` | play MP3 files you upload. This needs PSRAM and an I²S amplifier |
| `rtttl` | play melodies on the buzzer |
| `song` | play songs written as text with its synthesizer. Same hardware as `mp3` |
| `track` | play numbered tracks from a DFPlayer module |
| `radio` | play internet radio. Same hardware as `mp3` |

Every flag is always there, `true` or `false`. The other flags are always `false` on this clock.
<!-- /only -->
<!-- only tc002 -->
```json
{"audio":{"mp3":true,"rtttl":true,"song":true,"speech":true,"track":false,
          "radio":true,"url":true,"effect":true,"clip":true}}
```

| Flag | Your clock can |
|---|---|
| `mp3` | play MP3 files you upload |
| `rtttl` | play melodies |
| `song` | play songs written as text with its synthesizer |
| `speech` | read text aloud, when the clock has a voice |
| `radio` | play internet radio |
| `url` | play an MP3 straight from a web address |
| `effect` | play script effects and background music over each other |
| `clip` | play a recording sent whole to [`POST /api/v1/audio/clip`](../reference/http.md#post-apiv1audioclip) |

Every flag is always there, `true` or `false`. `track` is always `false` on this clock.
<!-- /only -->

## Play a sound

The quickest test plays three rising notes:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"beep:d=4,o=5,b=120:c,e,g"}'
```

The answer is:

```json
{"ok":true}
```

The body says what to play. It is called the **sound**, and a notification's `sound` takes the
same value.

A stored sound is played by its name:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"file":"ding"}'
```

A plain name in quotes is short for the same thing. `"ding"` and `{"file":"ding"}` play the same
sound:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '"ding"'
```

A sound has exactly one of these keys. The key says what plays:

| Key | Value | Plays |
|---|---|---|
<!-- only esp32 -->
| `file` | a stored name such as `"ding"` | a stored melody |
<!-- /only -->
<!-- only esp32-s3 -->
| `file` | a stored name such as `"ding"`, or a script's sound such as `"Racer/boost"` | a stored MP3 or melody |
<!-- /only -->
<!-- only tc002 -->
| `file` | a stored name such as `"ding"`, a script's sound such as `"Racer/boost"`, or an `http://` or `https://` address | a stored MP3 or melody, or an MP3 from the internet |
<!-- /only -->
| `rtttl` | RTTTL text, up to 512 characters | the melody in the request |
<!-- only esp32-s3 tc002 -->
| `song` | [song text](../reference/songs.md) | a song on the synthesizer |
<!-- /only -->
<!-- only tc002 -->
| `speech` | text, 1 to 512 bytes | the text, read aloud, when the clock has a voice |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `track` | a whole number from 1 to 2999 | a track from the DFPlayer |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `station` | a station name, a position in the list, or a stream address | internet radio, see [Internet radio](radio.md) |
<!-- /only -->

Next to the key, `"loop": true` repeats the sound until you stop it. See
[Repeat a sound](#repeat-a-sound).

### Give the clock a choice

A sound can also be a list of 1 to 4 sounds. The clock plays the first one it can play. It skips an
entry when it lacks the hardware for it, or when a `file` is not stored.

<!-- only esp32 esp32-s3 -->
This notification plays track 3 on a clock with a DFPlayer, and the melody on every other clock:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Door","sound":[{"track":3},{"rtttl":"bell:d=4,o=5,b=100:e,c"}]}'
```
<!-- /only -->
<!-- only tc002 -->
This notification speaks on a clock with a voice, and plays `ding` on every other clock:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Door","sound":[{"speech":"The front door is open."},"ding"]}'
```
<!-- /only -->

The display shows `Door` while the sound plays. A clock that can play none of the entries shows
the notification without sound.

### How a name is found

<!-- only esp32 -->
A plain name such as `"ding"` plays the melody `/MELODIES/ding.txt`.
<!-- /only -->
<!-- only esp32-s3 tc002 -->
A plain name such as `"ding"` is looked up in this order:

1. the MP3 `/MP3/ding.mp3`<!-- only esp32-s3 -->, on a clock that plays MP3s<!-- /only -->;
2. the melody `/MELODIES/ding.txt`<!-- only esp32-s3 -->, on a clock that plays melodies<!-- /only -->.

A script asks its own folder first. See [Sounds for your script](scripting/sound.md).

A name with a slash, such as `"Racer/boost"`, plays the sound `boost` of the script `Racer`. It is
looked up in that script's folder only.
<!-- /only -->

Names are 1 to 32 characters of `A-Z`, `a-z`, `0-9`, `_` and `-`.

<!-- only esp32-s3 tc002 -->
## MP3s

<!-- only esp32-s3 -->
MP3s need a clock with `mp3` in its capabilities.

<!-- /only -->
### Upload an MP3

1. Open the web UI and go to the **Audio** tab.
2. In the **MP3s** section, drag the file onto **⬆ Upload**, or click it to choose the file.

You play an MP3 by its file name without `.mp3`, so name the file the way you want to call it.

- Use normal MP3 files, the kind any converter makes. A file that is not an MP3 is refused.
- MP3s share the storage with icons, melodies and scripts, so keep them to a few seconds. The line
  above the list in the MP3s section shows how much space is left.
- An MP3 and a melody never share a name. If a melody called `ding` is stored, an MP3 called
  `ding.mp3` is refused, and the other way round. Rename one of them first.

### Play an MP3

1. Open the **Audio** tab.
2. Press **▶** next to the MP3.

Or use the file name without `.mp3`:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"file":"ding"}'
```

The same name works as `"sound":"ding"` in a [notification](notifications.md#sound), and in a
script.

<!-- only tc002 -->
### MP3s from the internet

The clock can play an MP3 straight from a web address. You do not upload it first.

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"file":"https://example.com/doorbell.mp3"}'
```

- The clock downloads the whole file first, then plays it. It does not keep the file.
- A file can be up to 4 MB. The clock also needs enough free memory for it.
- The answer comes at once. If you hear nothing, ask the clock why:
  [`GET /api/v1/audio`](../reference/http.md#get-apiv1audio) shows the reason under `alert.error`,
  for example `HTTP 404` (wrong address) or `not enough memory`.
- With `"loop": true`, a file that cannot be downloaded is not tried again.

<!-- /only -->
### Rename an MP3

Click the pen next to it on the Audio tab, type the new name and press Enter, or send:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/mp3/rename \
  -H 'Content-Type: application/json' \
  -d '{"from":"ding","to":"bell"}'
```

Alarms, notifications and scripts that play the old name stay silent until you change them. A
script's own MP3s keep their names.

### Delete an MP3

Click the bin next to it on the Audio tab, or send:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/audio/mp3/ding
```

### MP3s that come with a script

A script can bring its own MP3s. They live in the script's folder. The Audio tab lists them below
your own MP3s, in a group for each script. From outside the script, play one as
`"Racer/boost"`:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"file":"Racer/boost"}'
```

A script's sounds may have the same name as a melody or as an MP3 in `/MP3`. See
[Sounds for your script](scripting/sound.md).

Deleting a script with sounds asks what happens to them: **Delete with sounds**, or **Keep sounds**.
Kept sounds stay on the Audio tab, in a group marked **script removed**. They come back into use
when a script of the same name is installed again. Delete them there one by one when you do not
need them any more.

<!-- /only -->
## Melodies

A melody is a short tune in RTTTL text. You can send it with each request, or store it on the clock
and play it by name.
<!-- only esp32 esp32-s3 -->
Melodies need a clock with `rtttl` in its capabilities.
<!-- /only -->

### Writing RTTTL

An RTTTL melody has three parts, separated by colons. **All three are required**:

```
name:defaults:notes
```

```
beep:d=4,o=5,b=120:c,e,g
└─┬─┘ └──────┬─────┘ └─┬─┘
name    defaults     notes
```

- **name**: 1 to 24 characters. It is not played, but it must not be empty.
- **defaults**: `d` is the default note length, `o` the default octave, `b` the beats per
  minute. Each may appear once at most, in any order, and each may be left out.
- **notes**: separated by commas. Each note is an optional length, a letter `a` to `g` (`p` is a
  pause), an optional `#`, an optional `.` for a dotted note, and an optional octave. `16c6` is a
  16th note C in octave 6. A note without a length or octave uses the defaults.

Allowed values:

| Element | Allowed | If left out |
|---|---|---|
| `d` and any note's length | 1, 2, 4, 8, 16, 32 | `d`, default `4` |
| `o` and any note's octave | 4, 5, 6, 7 | `o`, default `6` |
| `b` | 10 to 300 | default `63` |
| note letter | `a` to `g`, or `p` for a pause | |

Anything else is refused, including `b#`, `e#`, a pause with `#` and a length such as `3`. The
answer names the reason and the position in the text:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"d=4,o=5,b=120:c,e,g"}'
```
```json
{"error":{"code":"validationFailed",
          "message":"missing ':' (at offset 19)",
          "field":"rtttl"}}
```

The status is `422`.

A melody is at most **512 characters**, sent in the request or stored in a file. See
[Limits](../reference/limits.md).

Some melodies to try:

```bash
# Two-tone doorbell
curl -X POST http://<awtrix-ip>/api/v1/audio/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"bell:d=4,o=5,b=100:e,c"}'

# Falling "something went wrong"
curl -X POST http://<awtrix-ip>/api/v1/audio/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"lose:d=8,o=5,b=120:16c,16b,16a,4g"}'

# Jackpot fanfare
curl -X POST http://<awtrix-ip>/api/v1/audio/play -H 'Content-Type: application/json' \
  -d '{"rtttl":"jackpot:d=8,o=5,b=120:16c,16e,16g,c6,16p,16c6,16e6,4g6"}'
```

### Store a melody

1. Open the **Audio** tab and go to **Melodies**.
2. Press **+ New melody**.
3. Enter a name and the notes, then save.

The editor checks the text while you type, plays it in your browser, and plays it on the clock when
you want to hear it there. See [Melodies](../getting-started/web-ui.md#melodies).

Or save one with a request:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/audio/melodies/doorbell \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"d=4,o=5,b=100:e,c"}'
```

The answer is `201` for a new melody and `200` when it replaced one. A text with a mistake answers
`422`.

- The melody is stored as `/MELODIES/doorbell.txt`. Its name part is always the name you saved it
  under. Send `d=4,o=5,b=100:e,c` and the file holds `doorbell:d=4,o=5,b=100:e,c`.
- Names are 1 to 24 characters of `A-Z`, `a-z`, `0-9`, `_` and `-`.
<!-- only esp32-s3 tc002 -->
- A melody never takes the name of a stored MP3. Saving `doorbell` while `/MP3/doorbell.mp3`
  exists answers `409`, `name taken`.
<!-- /only -->

### Play a stored melody

Use the name without `.txt`:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"file":"doorbell"}'
```

If nothing is called `doorbell`, the answer is `404`:

```json
{"error":{"code":"notFound","message":"nothing called \"doorbell\""}}
```

### List the stored melodies

```bash
curl http://<awtrix-ip>/api/v1/audio/melodies
```

```json
{"melodies":[{"name":"doorbell","rtttl":"doorbell:d=4,o=5,b=100:e,c",
              "bytes":26,"notes":2,"durationMs":2400,"valid":true}],
 "usedBytes":41216,"totalBytes":1048576}
```

- `notes` and `durationMs` tell you how long a melody is without playing it.
- A melody with a mistake is still listed, with `valid:false`, `error` and `index`. So you can find
  and fix it in the editor.
- `usedBytes` and `totalBytes` are for the whole storage. Melodies share it with icons, palettes
  and scripts.

### Delete or rename a melody

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/audio/melodies/doorbell
```

The answer is `404 notFound` if there is no such melody. To **rename** a melody, save it under the
new name and delete the old one.

<!-- only esp32-s3 tc002 -->
## Songs

The synthesizer plays music written as text: a few instruments and the notes they play. You send
the text in the request. Nothing is uploaded or stored. How to write a song:
[Song text](../reference/songs.md).

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"song":"bpm 120; inst lead wave=pulse volume=75; lead: c4 e g c5"}'
```

- The song plays once. Add `"loop": true` to repeat it until you stop it.
- Song text with a mistake is refused with `422 validationFailed`, `field` `song`. The message names
  the line and column.
- Scripts can play a song as music<!-- only tc002 --> and add effects over it<!-- /only -->. See the
  [scripting guide](scripting/sound.md).
<!-- /only -->

<!-- only tc002 -->
## Speech

A clock with a voice reads English text aloud. Your clock has a voice when
`speech` is `true` under `audio` in
[`GET /api/v1/capabilities`](../reference/http.md#get-apiv1capabilities).

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"speech":"Good morning. It is 7:30 and 21° outside."}'
```

A notification can speak too. The text on the display and the spoken text are separate:

<!-- panel motion=4 -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Door open","sound":{"speech":"The front door is open."}}'
```

The display shows `Door open`, and the clock says the whole sentence.

On a clock without a voice, that notification shows without sound. To play a sound there instead,
give a list: `"sound":[{"speech":"The front door is open."},"ding"]`.

How the clock reads the text:

- Up to 512 bytes of text. A plain letter is one byte. A letter with an accent, or a sign like
  `°`, is two or three.
- Letters with accents are read without them: `Café` sounds like `cafe`.
- Numbers are read as words: `23.5` is "twenty three point five", `-5` is "minus five".
- Times are read as times: `7:05` is "seven oh five", `12:00` is "twelve o'clock".
- `%`, `&` and `°` are read as "percent", "and" and "degrees".
- `.`, `,`, `?`, `!`, `;`, `:` and a new line make a short pause.
- Anything else, emoji for example, is skipped.
- A very long text stops after the last word that fits.

While [Home Assistant Voice](voice.md) uses the speaker, the clock does not speak.

If the clock does not speak, check that the text contains words and fits within 512 bytes.
The clock also needs speech support and an available speaker. The exact responses are listed in
[Audio playback errors](../reference/errors.md#audio-playback).

<!-- /only -->
## Repeat a sound

Add `"loop": true` to a sound to repeat it until you stop it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"rtttl":"siren:d=8,o=5,b=200:c,g,c,g","loop":true}'
```

- It repeats until you [stop](#stop-a-sound) it, or until a new alert takes its place.
<!-- only esp32-s3 tc002 -->
- The radio stays paused while it repeats and comes back once it has stopped.
<!-- /only -->
- In a notification it repeats while the notification is shown. See
  [Notifications](notifications.md#sound).
<!-- only esp32-s3 tc002 -->
- `loop` does not work with `station`. A station plays until you stop it anyway.
<!-- /only -->

## Volume

The clock has one **master volume** and <!-- only esp32 -->two<!-- /only --><!-- only esp32-s3 tc002 -->three<!-- /only --> groups. Each group is a share of the master volume:

| Setting | Range | Default | Volume of |
|---|---|---|---|
| `volume` | 0 to 100 | <!-- only esp32 esp32-s3 -->`60`<!-- /only --><!-- only tc002 -->`90`<!-- /only --> | the whole clock. Every group below is a share of it |
<!-- only esp32-s3 tc002 -->
| `radioVolume` | 0 to 100 | `80` | internet radio |
<!-- /only -->
| `appVolume` | 0 to 100 | `100` | everything a script plays |
| `alertVolume` | 0 to 100 | `100` | notification sounds<!-- only esp32 esp32-s3 --> and `/api/v1/audio/play`<!-- /only --><!-- only tc002 -->, `/api/v1/audio/play`, the boot sound and Home Assistant Voice<!-- /only --> |

What you hear is the master volume times the group's share. With `volume` at 50 and `alertVolume`
at 60, an alert plays at 30.

<!-- only tc002 -->
MP3s, melodies, songs and speech play about equally loud at the same volume. A script's music plays
a little quieter, under its effects.

<!-- /only -->
### In the web UI

1. Open the **Audio** tab.
2. Move the sliders in the **Mixer** section at the top: **Master**, <!-- only esp32-s3 tc002 -->**Radio**, <!-- /only -->**Apps** and
   **Alerts**.<!-- only esp32-s3 --> **Radio** shows only on a clock that plays radio.<!-- /only -->

A change applies at once, also to sounds that are playing.

<!-- only tc002 -->
[The knob](device-controls.md#the-knob) also sets the master volume.

<!-- /only -->
### With the API

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"volume":50,"alertVolume":60}'
```

- `0` is silence. `"volume": 0` silences every sound<!-- only esp32-s3 tc002 -->, the radio included<!-- /only -->.
- A value outside 0 to 100 is refused with `422`.

All <!-- only esp32 -->these<!-- /only --><!-- only esp32-s3 tc002 -->four<!-- /only --> settings: [Sound settings](../reference/settings.md#sound).

## What plays over what

| When | What happens |
|---|---|
<!-- only esp32 -->
| An alert starts | A script's sounds never cut an alert off |
<!-- /only -->
<!-- only esp32-s3 -->
| An alert starts | The radio pauses and comes back afterwards. A script's sounds never cut an alert off |
<!-- /only -->
<!-- only tc002 -->
| An alert starts | The radio pauses and comes back afterwards. A script's sounds never cut an alert off. A script's music and effects get quieter until the alert ends |
<!-- /only -->
| A new alert arrives while one plays | The new alert replaces the playing one |
| A script plays a single sound while an alert plays | The script's sound is not played |
<!-- only esp32-s3 -->
| A script plays a sound | The radio pauses and comes back when the script's sound has ended |
<!-- /only -->
<!-- only tc002 -->
| A script plays a sound, music or an effect | The radio pauses and comes back when the script's sounds have ended |
| A station starts | A script's music and effects end. A sound that is playing finishes first |
<!-- /only -->
| A notification with `loop` leaves the display | Its sound stops at once |
| A notification without `loop` leaves the display | Its sound plays to its end |
<!-- only tc002 -->
| Home Assistant Voice talks | The voice has the speaker. A station or a script's music that was playing comes back afterwards, the music from its beginning |
<!-- /only -->

<!-- only esp32 esp32-s3 -->
The clock plays one sound at a time.
<!-- /only -->
<!-- only tc002 -->
The clock can play several sounds at once.
<!-- /only -->

## Stop a sound

On the Audio tab, press **■** in the bar that shows what is playing. Or send:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/stop
```

This stops everything<!-- only esp32-s3 tc002 -->, the radio too<!-- /only -->. Add `group` to stop less:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/stop \
  -H 'Content-Type: application/json' \
  -d '{"group":"alert"}'
```

| `group` | Stops |
|---|---|
| `alert` | the alert that is playing |
| `app` | every sound a script plays<!-- only tc002 -->, its music and effects included<!-- /only --> |
<!-- only esp32-s3 tc002 -->
| `radio` | the radio |
<!-- /only -->

Any other value answers `422`, `must be alert, app or radio`.

## Over MQTT

The same sound goes to `<prefix>/cmd/audio/play`:

```bash
mosquitto_pub -h <broker> -t 'awtrixNG/cmd/audio/play' -m '{"file":"ding"}'
mosquitto_pub -h <broker> -t 'awtrixNG/cmd/audio/stop' -m '{"group":"alert"}'
```

- An empty payload on `cmd/audio/stop` stops everything.
- Errors come back on `<prefix>/cmd/audio/play/result` as `{"ok":false,"error":{…}}`.
- `<prefix>/state/audio` shows what is playing. It is kept by the broker, and it is sent again on
  every change.
- Storing and listing melodies works over HTTP only.

See [Command topics](../reference/mqtt.md#command-topics).

<!-- only esp32 esp32-s3 -->
## DFPlayer tracks {#dfplayer-boards}

<!-- only esp32 -->
A converted AWTRIX 2 mainboard can drive a DFPlayer Mini MP3 module.
<!-- /only -->
<!-- only esp32-s3 -->
A DIY board can drive a DFPlayer Mini MP3 module.
<!-- /only -->
AWTRIX uses it when the **DFPlayer** switch under **System → Audio** is on and both DFPlayer pins
are set. The buzzer keeps working next to it. See
[Sound hardware](../reference/system.md#sound-hardware) and
[The pin map](../reference/gpio.md#the-pin-map).

A DFPlayer plays numbered files from its own SD card:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"track":3}'
```

- A number outside 1 to 2999 answers `422 validationFailed`.
- A track plays at the volume of its group, like every other sound.

## The buzzer

The buzzer plays melodies only. To silence it for good, set `pinBuzzer` to `-1` in the
[pin map](../reference/gpio.md#the-pin-map).

<!-- /only -->
## When it goes wrong

| What you see | Why |
|---|---|
| `422 validationFailed` | the sound has a mistake, for example two keys, a name with `.mp3` or `.txt`, or a melody that cannot be read<!-- only esp32 esp32-s3 -->, or a track out of range<!-- /only -->. `field` names the key |
<!-- only esp32 -->
| `404 notFound`, `nothing called "x"` | no melody has that name. Check the spelling |
| `503 unavailable` | your clock cannot play this kind of sound, for example a melody without a buzzer. For a list, the clock can play none of its entries |
| `200`, but no sound | the master volume or the group's volume is `0`, a script's sound met a playing alert, or the DFPlayer track is not on the card |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `404 notFound`, `nothing called "x"` | no MP3 or melody has that name. Check the spelling |
| `404 notFound`, `no file "Racer/x"` | the script `Racer` has no sound called `x` |
<!-- /only -->
<!-- only esp32-s3 -->
| `503 unavailable` | your clock cannot play this kind of sound, for example an MP3 without an I²S amplifier. For a list, the clock can play none of its entries |
<!-- /only -->
<!-- only tc002 -->
| `503 unavailable` | your clock cannot play this kind of sound, for example `speech` without a voice. For a list, the clock can play none of its entries |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `409 nameTaken`, `name taken` | an MP3 and a melody would share a name |
<!-- /only -->
<!-- only esp32-s3 -->
| `200`, but no sound | the master volume or the group's volume is `0`, a script's sound met a playing alert, or the DFPlayer track is not on the card |
<!-- /only -->
<!-- only tc002 -->
| `200`, but no sound | the master volume or the group's volume is `0`, or a script's sound met a playing alert |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| MP3 upload refused | the file name has characters other than `A-Z a-z 0-9 _ -`, or the file is not an MP3 |
<!-- /only -->

Every status code and message: [Audio playback errors](../reference/errors.md#audio-playback).

## Good to know

- **An `rtttl` without a name in front is refused with `422`.** Write any name and a colon before
  the defaults: `beep:d=4,o=5,b=120:c,e,g`. Only a [stored melody](#store-a-melody) may leave the
  name out.
- **A stored sound is played by its name alone.**
  <!-- only esp32 -->`{"file":"ding.txt"}`<!-- /only --><!-- only esp32-s3 tc002 -->`{"file":"ding.mp3"}`<!-- /only -->
  is refused with `422`, `invalid name`. Send `{"file":"ding"}`.
<!-- only esp32-s3 tc002 -->
- **An MP3 whose file name has spaces or other signs is refused.** Names may only use `A-Z`, `a-z`,
  `0-9`, `_` and `-`, up to 32 characters. Rename `My Song (2024).mp3` to `my-song-2024.mp3` first.
- **`station` works only on its own.** In a list or in a notification's `sound` it is refused with
  `422`, `not here`. Start the radio with a request of its own.
<!-- /only -->
<!-- only esp32 esp32-s3 -->
- **AWTRIX cannot see what is on the DFPlayer's card.** A track that is not there answers `200`
  and plays nothing. Check the numbers of the files on the card.
<!-- /only -->

## Details

- [Audio](../reference/http.md#audio): every audio route, field and status code
<!-- only esp32-s3 tc002 -->
- [Song text](../reference/songs.md): how to write a song
<!-- /only -->
- [Sound settings](../reference/settings.md#sound): the <!-- only esp32 -->volumes<!-- /only --><!-- only esp32-s3 tc002 -->four volumes<!-- /only -->
<!-- only esp32 esp32-s3 -->
- [Sound hardware](../reference/system.md#sound-hardware): the DFPlayer and the buzzer pins
<!-- /only -->

## Related

<!-- only esp32-s3 tc002 -->
- [Internet radio](radio.md): stations and streams
- [Audio tab](../getting-started/web-ui.md#audio): the mixer, MP3s and the melody editor
<!-- /only -->
<!-- only esp32 -->
- [Audio tab](../getting-started/web-ui.md#audio): the mixer and the melody editor
<!-- /only -->
- [Notifications](notifications.md#sound): a sound with a notification
