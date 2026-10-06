---
only: [esp32-s3, tc002]
---

# Song text

A song is a short piece of text that AWTRIX plays with its own synthesizer: you define a few
instruments, then write each part as a line of notes. No sound file is involved, so a song costs
no storage, and a script can build one on the spot.

```text
bpm 120
inst lead wave=pulse volume=75
lead: c4 e g c5
```

As background music in a script, this plays four quarter notes, C E G C, again and again until
the script stops them.

This page explains every part of the song text. For how songs fit in with MP3s, melodies and the
radio, see [Sound](../guides/sounds.md#songs).

## Play a song

Songs need a clock with a synthesizer: `audio.song` in
[`GET /api/v1/capabilities`](http.md#get-apiv1capabilities).<!-- only esp32-s3 --> On an ESP32-S3
clock that is the hardware that plays MP3s: PSRAM and an I²S amplifier.<!-- /only --> A song is a
sound with the key `song`, and it plays in one of <!-- only esp32-s3 -->two<!-- /only --><!-- only tc002 -->three<!-- /only --> ways:

| As | Sent with | Plays |
|---|---|---|
| an alert | `{"song":"…"}` on `POST /api/v1/audio/play`, MQTT or in a notification | once. With `"loop": true` it plays again each time it ends, until it is stopped |
<!-- only esp32-s3 -->
| a script's sound | `sound.play({'song': …})` in a script | once. With `'loop': true` it plays again each time it ends, until it is stopped or another sound replaces it |
<!-- /only -->
<!-- only tc002 -->
| background music | `sound.play({'song': …, 'loop': true})` in a script | underneath everything else, over and over until it is stopped, from the bar `loop` names |
| an effect | `sound.effect({'song': …})` in a script | once, over the music, next to the MP3 effects: a jingle, a blip, a game sound |
<!-- /only -->

To play a song from a script, see the [Scripting guide](../guides/scripting/sound.md).
From the command line:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' \
  -d '{"song":"bpm 120; inst lead wave=pulse volume=75; lead: c4 e g c5"}'
```

## How a song is written

- **One statement per line.** A `;` ends a statement too, so a whole song fits on one line - handy
  in JSON, where a line break has to be written as `\n`.
- **Comments** start at a `#` that begins a statement or follows a space, and run to the end of the
  line; a `;` inside a comment does not end it. `c#4` is a note, not a comment.
- Blank lines and indentation do not matter. Words are separated by spaces or tabs.
- **Statements, keys and note letters are lower case**: `C4` is refused. Names of instruments and
  tracks are 1 to 16 letters, digits, `_` or `-`, and `Lead` is not `lead`.
- **The order is free.** Settings and instruments may come after the tracks that use them, and
  settings may sit anywhere.

There are three kinds of statement:

| Statement | Example | Sets |
|---|---|---|
| a song setting | `bpm 96` | tempo, bar length, looping, level, echo - [Song settings](#song-settings) |
| an instrument | `inst bass wave=saw cutoff=600` | what a part sounds like - [Instruments](#instruments) |
| a track | `bass: c2 c c3 c2` | what a part plays - [Tracks](#tracks) |

## Song settings

Each setting may appear once. Leave one out and its default applies.

| Statement | N | Default | Meaning |
|---|---|---|---|
| `bpm N` | 20-300, decimals allowed | `120` | tempo, in beats per minute. A beat is a quarter note |
| `beats N` | 1-16, whole | `4` | beats per bar: `beats 3` is 3/4 time |
| `loop N` | a bar, 1-1024 | `1` | the bar a song goes back to when it reaches its end |
| `loop off` | - | - | the song plays once |
| `volume N` | 0-200 % | `100` | the level of the whole song |
| `echo time=N …` | see below | off | one echo for the whole song, which instruments send to with their own `echo=` |

`echo` takes these keys in any order; `time=` is required:

| Key | Value | Default | Meaning |
|---|---|---|---|
| `time=` | 0.25-64 sixteenths | - | the delay between repeats, in sixteenth notes: `time=3` is a dotted eighth |
| `feedback=` | 0-90 % | `0` | how much of each repeat comes back once more; `0` repeats once |
| `damp=` | 200-16000 Hz | `4000` | the repeats lose the highs above this, so each one is darker than the last |

The delay may be at most 2 seconds at the song's tempo. A sixteenth lasts 15 / `bpm` seconds, so at
120 bpm `time=` goes up to 16.

```text
bpm 100
echo time=3 feedback=35 damp=3000
inst lead wave=pulse duty=30 volume=80 echo=40
lead: e5:4 d:2 c:2 d:4 e:4 |
```

## Instruments

```
inst NAME key=value key=value …
```

An instrument is a sound: `inst` with a name, then any of the keys below in any order. A key left
out keeps its default, so `inst lead` alone is a plain square wave. A song has up to 32 instruments,
each defined once.

| Key | Value | Default | What it does |
|---|---|---|---|
| `wave=` | `pulse` `saw` `tri` `sine` `noise` | `pulse` | the basic sound: a hollow pulse, a bright sawtooth, a soft triangle, a pure sine, or noise with no pitch |
| `duty=` | 1-99 % | `50` | the width of a `pulse` wave: 50 is a square, 25 and 12 sound thinner and more nasal |
| `unison=` | 0-100 cents | `0` | a second oscillator this many cents away from the first, for a wide, chorused sound |
| `sub=` | 0-100 % | `0` | adds a triangle one octave below |
| `noise=` | 0-100 %, or `level/ms` | `0` | mixes noise into the sound. After a `/`, the noise dies away at that rate (0-10000 ms); without one, or with `0`, it lasts as long as the note. `noise=30/4` is a click at the start |
| `attack=` | 0-10000 ms | `2` | how long a note takes to rise to full level |
| `decay=` | 1-20000 ms | `200` | how fast it then falls toward `sustain=` |
| `sustain=` | 0-100 % | `70` | the level a held note settles at. With `0` a note dies away by itself, like a plucked string or a drum |
| `release=` | 0-20000 ms | `60` | how long a note takes to fade out once it lets go |
| `gate=` | 1-100 % | `90` | how much of its written length a note holds before it lets go; lower is more staccato |
| `pitch=` | -48 to 48 semitones, or `semitones/ms` | `0` | each note starts this far above its pitch (below, when negative) and falls to it. After a `/` the rate, 1-5000 ms, else 30. `pitch=24/25` is the thump of a kick drum |
| `vibrato=` | `cents`, `cents/Hz` or `cents/Hz/ms` | `0`, 5.5 Hz, 0 ms | wobbles the pitch by up to 0-200 cents, 0.1-20 times a second, starting after the given delay (0-5000 ms) and fading in over 200 ms |
| `glide=` | 0-5000 ms | `60` | how long a [slide](#slides) takes to reach its note |
| `filter=` | `lp` `bp` `hp` | none | a low-pass, band-pass or high-pass filter at `cutoff=` |
| `cutoff=` | 20-20000 Hz | `20000` | where the filter cuts. `cutoff=` without `filter=` is a low-pass |
| `resonance=` | 0-100 % | `0` | emphasises the sound around the cutoff; high values ring |
| `filterenv=` | -20000 to 20000 Hz, or `Hz/ms` | `0` | each note starts with the cutoff this much higher (lower, when negative) and falls back to `cutoff=`. After a `/` the rate, 1-10000 ms, else 50 - the "wow" of a synth bass |
| `drive=` | 0-100 % | `0` | overdrive, from warm to fuzzy |
| `volume=` | 0-200 % | `100` | the instrument's level - see [Levels](#levels) |
| `echo=` | 0-100 % | `0` | how much of it goes to the song's echo; nothing without an `echo` statement |
| `note=` | a note such as `c2` or `f#3` | `c4` | the pitch of the [steps](#step-patterns) a `%` pattern plays |

**Rates.** `decay=` and the time after the `/` in `noise=`, `pitch=` and `filterenv=` say how fast
something falls, not when it stops: in that time it covers about two thirds of the way, and after
three times as long it is practically there. `attack=`, `release=` and `glide=` are plain
durations.

Set `cutoff=` together with `filter=hp` or `filter=bp`: at the default 20000 Hz a high-pass lets
almost nothing through.

`unison=`, `sub=`, `noise=` and `resonance=` add to an instrument's level, so an instrument that
uses them sounds louder than its `volume=` suggests.

A few starting points:

```text
bpm 110
inst lead  wave=pulse duty=25 vibrato=15/5.5/250 release=150 volume=60
inst pad   wave=saw unison=14 attack=300 decay=800 sustain=60 release=600 cutoff=1800 volume=25
inst bass  wave=saw cutoff=500 resonance=30 filterenv=1200/80 decay=150 sustain=50 volume=60
inst pluck wave=tri decay=180 sustain=0 release=100 volume=70
inst kick  wave=sine note=c2 pitch=24/25 noise=30/4 attack=0 decay=200 sustain=0 volume=95
inst snare wave=tri note=g3 noise=80/90 attack=0 decay=140 sustain=0 volume=55
inst hat   wave=noise filter=hp cutoff=7000 attack=0 decay=35 sustain=0 volume=30

lead:  e5:4 d c d |
pad:   [a3 c4 e]:16 |
bass:  a1:2 a a2 a1 r a a2 a1 |
pluck: a4:2 c5 e a e c a4 e |
kick:  %X.......x.x..... |
snare: %....X.......X... |
hat:   %x.o.x.o.x.o.x.og |
```

## Tracks

```
NAME: item item item …
```

A track is one part of the song, and every track starts at the top of the song at the same time.
The name comes first, with the `:` right after it, then the items separated by spaces.

A track plays the instrument of the same name: `bass:` plays `inst bass`. `@name` switches to
another instrument from there on, so one instrument can play in several tracks, and a track with
no instrument of its own name picks one before its first note. A song has up to 16 tracks.

**A second line with the same name continues the track** where the first one ended, with
everything as it was left - octave, length, velocity, transposition and instrument. Long parts are
easier to read that way, a line per phrase or per few bars.

| Item | Example | Plays |
|---|---|---|
| note | `c4` `f#` `bb3:2` | a note - see [Notes](#notes) and [Lengths](#lengths) |
| rest | `r` `r:8` | silence |
| hold | `_` `_:4` | makes the note, chord or step before it longer |
| chord | `[c4 e g]:8` | several notes that start together - see [Chords](#chords) |
| slide | `^e5` | a note the track glides into - see [Slides](#slides) |
| velocity | `v60` | how hard the notes after it are struck, `v1` to `v100` |
| transposition | `t-12` `t+7` `t0` | shifts the notes after it by semitones, -48 to +48 |
| instrument | `@pad` | the notes after it play that instrument |
| repeat | `(c d e)4` | what is inside, that many times - see [Repeats](#repeats) |
| bar check | `|` | nothing - it checks that a bar ends here |
| steps | `%x..xx.o.` | a drum pattern - see [Step patterns](#step-patterns) |

**Octave, length, velocity, transposition and instrument carry on**: each stays as it was last
written until the track writes it again. A track starts in octave 4, with quarter notes, at `v100`,
untransposed, on the instrument of its own name.

### Notes

A note is a letter `a` to `g`, then optionally `#` (sharp) or `b` (flat), then optionally the
octave, one digit from 0 to 9. `c4` is middle C and `a4` is 440 Hz. `bb3` is B flat, `eb` is E
flat, `cb4` is the same as `b3`.

A note without an octave takes the one written last: in `e5 a c6 b5` the `a` is `a5`. The highest
note is `g9`.

### Lengths

`:` and a number after a note, rest, hold or chord is its length in sixteenth notes, from 1 to 256.
The length carries on to the notes after it, like the octave.

| Written | Length |
|---|---|
| `:1` | a sixteenth |
| `:2` | an eighth |
| `:3` | a dotted eighth |
| `:4` | a quarter - one beat |
| `:6` | a dotted quarter |
| `:8` | a half |
| `:16` | a whole note - one bar of 4/4 |

A length can be divided: `:4/3` is a third of a quarter, an eighth-note triplet - three of them fill
a beat. `:8/3` is a quarter-note triplet, `:2/3` a sixteenth-note triplet, `:1/2` a thirty-second.
A divided length must come out in whole twelfths of a sixteenth, so every triplet works, but
`:1/5` is refused.

```text
inst lead wave=tri volume=135
lead: c4:4/3 d e f:4 g:8 |
```

At 120 bpm a sixteenth lasts 125 ms. A bar holds four sixteenths per beat: 16 in 4/4, 12 in 3/4.

### Rests and holds

`r` is silence. `_` makes the note, chord or step right before it longer by its own length, which is
how a note is tied over a bar line:

```text
inst lead wave=saw cutoff=1200 volume=165
lead: r:12 g4:4 | _:8 e:8 |
```

After a rest there is nothing to hold, so `_` there is a mistake. Several `_` in a row keep holding.

### Chords

Notes in `[ ]` start together. Each may name its octave, the length goes after the `]`, and the
octave written last carries on after the chord. `_` holds the whole chord. A chord has up to 8
notes.

```text
inst keys wave=tri release=300 volume=85
keys: [c4 e g]:8 [f3 a c4] | [g3 b d4]:16 |
```

### Slides

`^` before a note glides into it from the note the track played before, taking the instrument's
`glide=` time. While the note before is still sounding - its `release=` included - the slide bends
it to the new pitch without a new attack; after a longer rest a new note starts at the old pitch
and glides from there. A slide can be used for a legato line, a bass drop or a siren.

```text
inst lead wave=saw glide=80 cutoff=2000 volume=155
lead: c4:4 ^e ^g:8 | c5:4 ^b4:2 ^a ^g:8 |
```

### Velocity and transposition

`v1` to `v100` sets how hard the notes after it are struck: `v50` plays at half the level of `v100`.

`t` and a number of semitones shifts the notes after it: `t+12` is an octave up, `t-5` a fourth
down, `t0` back to normal. It sets the shift, it does not add to it - `t+5` then `t+7` is 7 up, not
12. It shifts steps too.

```text
bpm 110
inst bass wave=saw cutoff=700 volume=165
bass: v100 (c2:2 c c3 c2)2 t+5 (c2:2 c c3 c2)2 | t+7 v80 (c2:2 c c3 c2)2 t0 (c2:2 c c3 c2)2 |
```

### Instruments in a track

`@name` makes the notes after it play another instrument, one defined anywhere in the song.

```text
inst piano wave=tri release=200 volume=115
inst organ wave=sine sub=50 volume=115
melody: @piano c4 e g c5 | @organ c5 g4 e c |
```

### Repeats

`( … )` and a count right after the `)` plays what is inside that many times, 1 to 256. Every pass
starts with the octave, length, velocity, transposition and instrument that held before the `(`, so
every pass sounds the same; after the last pass, what it left carries on. Repeats nest up to 4
deep, and a repeat opens and closes on the same line.

```text
inst lead wave=pulse duty=25 volume=75
lead: (c5:2 e g e)2 ((a4:1 b)2 c5:4)2 |
```

### Bar checks

`|` plays nothing. It checks that the track is exactly at the end of a bar, and refuses the song
when it is not:

```
this | falls 15 sixteenths into bar 1 (line 2, column 13)
```

Put one after every bar. Then a wrong length is reported where it happens, and a track cannot
drift out of time with the others.

### Step patterns

`%` and a row of steps is a drum pattern. Each step is a sixteenth and is one of:

| Step | Plays |
|---|---|
| `X` | a hit at the track's full velocity |
| `x` | a hit at 80 % of it |
| `o` | a hit at 55 % |
| `g` | a ghost note, at 30 % |
| `.` | nothing |

A step plays the instrument's `note=`, shifted by the track's transposition, so one instrument per
drum and one track per instrument is the usual layout. Sixteen steps fill a bar of 4/4.

A length right after the `%` sets the step length for that pattern alone: `%:2x.x.` plays eighths.
The steps follow the length with no space in between.

A pattern ends at a space, `|`, `(`, `)` or `[`, so each bar of steps gets its own `%`:

```text
bpm 110
inst kick wave=sine note=c2 pitch=24/25 attack=0 decay=200 sustain=0 volume=200
inst hat  wave=noise filter=hp cutoff=7000 attack=0 decay=35 sustain=0 volume=65
kick: %X.......x.x..... | %X.......x...x.o. |
hat:  %x.o.x.o.x.o.x.og | %:2xxxxxxxx |
```

## Song length and looping

A song is as long as its longest track, rounded up to whole bars, and it may be up to 1024 bars
long. Shorter tracks rest until the end.

<!-- only tc002 -->
- **A song that loops** as a script's music plays to the end of its last bar and goes
  back to the bar `loop` names, bar 1 unless it says otherwise. `loop 3` plays two bars once as an
  intro and repeats from bar 3 on:

    ```text
    bpm 96
    loop 3
    inst lead wave=saw cutoff=1500 volume=40
    lead: c4:16 | g3:16 | (c4:4 e g e)2 |
    ```

- **A song with `loop off`, every effect and every alert** ends where its longest track ends, not
  at the bar line. The last notes ring out with their `release=`, and the echo dies away. An alert
  plays the song once, whatever its `loop` says.
<!-- /only -->
<!-- only esp32-s3 -->
- **Every song ends where its longest track ends**, not at the bar line. The last notes ring out
  with their `release=`, and the echo dies away. With `"loop": true` the song then starts again
  from its first bar; its `loop` line makes no difference on this clock.
<!-- /only -->

## Levels

**One note at `v100` on an instrument at `volume=100` peaks at a quarter of full scale**, and notes
that sound together add up. A single line of melody plays as loud as the clock's other sounds at
about `volume=75` with `wave=pulse`, and at 130 to 170 with the softer `wave=tri`, `wave=sine` or a
filtered `wave=saw`. In a full arrangement the instruments share that level: keep each one between
about 25 and 140, as in the examples on this page, so that the sum stays clean.

At the very end everything passes a soft limiter. Peaks that go past full scale are rounded off
instead of clipping hard, but a mix that leans on the limiter sounds squashed and distorted. A
song's `volume N` sets the level of the whole mix before the limiter.

A song sounds up to 24 notes at once<!-- only tc002 -->, an effect up to 8<!-- /only -->. When all of them are busy, a new note takes
the place of the quietest one that is already fading out, else of the oldest.

On the clock a song plays at the volume of its group: a script's song at the app volume, a song
from `POST /api/v1/audio/play` at the alert volume. See [Volume](../guides/sounds.md#volume).

## Errors

A song that does not parse is refused whole and nothing plays. The message names the reason, then
the line and the column where it went wrong, both counted from 1. Lines are counted by line breaks,
so on a song written with `;` everything is on line 1 and the column finds the spot.

```
bpm 120
inst lead wave=pulse
lead: c4 x
```

```
unexpected 'x' (line 3, column 10)
```

A few more, each with the text that caused it. The track lines sit on line 2, under an
`inst lead` on line 1:

| Text | Message |
|---|---|
| `bpm 10` | `bpm must be between 20 and 300 (line 1, column 5)` |
| `inst lead wave=square` | `wave is pulse, saw, tri, sine or noise (line 1, column 16)` |
| `bass: c2`, with no `inst bass` | `track 'bass' has no instrument (line 2, column 7)` |
| `lead: c4:2d4` | `expected a space after 'c4:2' (line 2, column 11)` |
| `lead: c4 r _` | `_ without a note (line 2, column 12)` |
| `lead: (c d e)` | `repeat count missing after ) (line 2, column 14)` |
| `loop 3` on a song of two bars | `loop 3 is past the last bar (2) (line 1, column 6)` |

Over HTTP and MQTT the message comes back in a `422 validationFailed` with `field` `song`. In a
notification the field is `sound.song`. In a script, `sound.play()`<!-- only tc002 --> and `sound.effect()`<!-- /only --> raise<!-- only esp32-s3 -->s<!-- /only --> a
`value_error` carrying it.

## Limits

| Limit | Value | Message |
|---|---|---|
| Song text | 16384 bytes | `at most 16384 bytes` |
| Tracks | 16 | `at most 16 tracks` |
| Instruments | 32 | `at most 32 instruments` |
| Notes, repeats counted out | 8192 | `at most 8192 notes` |
| Length | 1024 bars | `at most 1024 bars` |
| Notes in a chord | 8 | `at most 8 chord notes` |
| Note length | 1-256 sixteenths | `length must be 1..256` |
| Repeat count | 1-256 | `repeat must be 1..256` |
| Repeats inside repeats | 4 deep | `groups nest 4 deep at most` |
| Echo delay | 2 s at the song's tempo | `echo over 2 s at this bpm` |
| Instrument and track names | 1-16 letters, digits, `_`, `-` | `invalid instrument name`, and the same for `invalid track name` |

<!-- only esp32-s3 -->
An HTTP request body and an MQTT message are at most 8192 bytes, the JSON around the song included.
From a script the full 16384 bytes work.
<!-- /only -->
<!-- only tc002 -->
An MQTT message is at most 8192 bytes, topic and the JSON around the song included. Over HTTP
and from a script the full 16384 bytes work.
<!-- /only -->

A song whose repeats unfold into far more work than any real song needs - repeats of repeats that
hold only a velocity, for instance - is refused with `too many repeats`.

## Examples

### A jingle that plays once

```text
# "Done!" - plays once
bpm 140
loop off
inst lead wave=pulse duty=25 decay=120 sustain=40 release=150 volume=90
inst bass wave=tri volume=100

lead: c5:2 e g c6:4 r:2 g5:2 c6:8
bass: c3:4 g2 c3:8
```

<!-- only tc002 -->As background music it plays once. As an effect it plays over whatever else is going on.<!-- /only -->

<!-- only tc002 -->
### A blip for a button

A short effect, one note that drops a little in pitch as it starts:

```text
inst blip wave=sine attack=0 decay=60 sustain=0 pitch=12/15 volume=175
blip: c6:1
```

```berry
  def on_button(btn)
    if btn == "select"
      sound.effect({'song': "inst blip wave=sine attack=0 decay=60 sustain=0 pitch=12/15 volume=175\n"
                             "blip: c6:1"})
    end
  end
```

<!-- /only -->
### A drum groove

One instrument per drum, one track per instrument, two bars:

```text
# A two-bar groove
bpm 110
inst kick  wave=sine note=c2 pitch=24/25 noise=30/4 attack=0 decay=200 sustain=0 volume=200
inst snare wave=tri note=g3 noise=80/90 attack=0 decay=140 sustain=0 volume=115
inst hat   wave=noise filter=hp cutoff=7000 attack=0 decay=35 sustain=0 volume=65

kick:  %X.......x.x..... | %X.......x...x.o. |
snare: (%....X.......X...)2
hat:   (%x.o.x.o.x.o.x.og)2
```

### A song that loops

Eight bars - A minor, F, C, G, twice - with pads, bass, drums and a lead that comes in after three
bars:

```text
# Night drive - 8 bars, loops for ever
bpm 100
volume 90
echo time=3 feedback=35 damp=3000

inst pad   wave=saw unison=14 attack=300 decay=800 sustain=60 release=600 cutoff=1800 volume=40 echo=20
inst bass  wave=saw cutoff=500 resonance=30 filterenv=1200/80 decay=150 sustain=50 release=40 volume=105
inst lead  wave=pulse duty=30 vibrato=15/5.5/250 glide=70 release=150 volume=75 echo=35
inst kick  wave=sine note=c2 pitch=24/25 noise=30/4 attack=0 decay=200 sustain=0 volume=140
inst snare wave=tri note=g3 noise=80/90 attack=0 decay=140 sustain=0 volume=75
inst hat   wave=noise filter=hp cutoff=7000 attack=0 decay=35 sustain=0 volume=40

# Am  F  C  G, twice
pad:  ([a3 c4 e]:16 | [f3 a c4]:16 | [e3 g c4]:16 | [d3 g b]:16 |)2
bass: (a1:2 a a2 a1 r a a2 a1 | f1 f f2 f1 r f f2 f1 | c2 c c3 c2 r c c3 c2 | g1 g g2 g1 r g g2 g1 |)2

kick:  (%X.......x.x.....)8
snare: (%....X.......X...)8
hat:   (%x.o.x.o.x.o.x.og)8

# the lead waits three bars, then comes in with a pickup
lead: r:16 | r:16 | r:16 | r:8 v70 b4:2 c5 d:4 |
lead: v100 e5:4 d:2 c:2 ^d:4 e:4 | c5:6 a4:2 _:4 r:4 | g4:2 a c5:4 e:4 d:2 c:2 | b4:8 ^d5:4 r:4 |
```

### Writing a song in a script

Berry joins string literals that follow each other, across line breaks and comments, so a song can
be written a line at a time without one long string:

```berry
class Theme
  var tune

  def init()
    self.tune = "bpm 100\n"
                "inst pad  wave=saw unison=12 attack=200 sustain=60 release=400 cutoff=1500 volume=65\n"
                "inst kick wave=sine note=c2 pitch=24/25 attack=0 decay=200 sustain=0 volume=200\n"
                "pad:  [a3 c4 e]:16 | [f3 a c4]:16 |\n"   # two chords
                "kick: (%x...x...x...x...)2"
  end
  …
```

Every piece but the last ends in `\n`, the line break between two statements. See
[Sound and music](../guides/scripting/sound.md) for the whole app.

## Related

- [Songs](../guides/sounds.md#songs) - how songs sit among the other sounds
- [`POST /api/v1/audio/play`](http.md#post-apiv1audioplay) - `song` and `loop`
<!-- only esp32-s3 -->
- [Sound and music in scripts](../guides/scripting/sound.md#songs) - `sound.play()`
<!-- /only -->
<!-- only tc002 -->
- [Sound and music in scripts](../guides/scripting/sound.md) - `sound.play()`, `sound.effect()`, `sound.beat()`
<!-- /only -->
- [Limits](limits.md#sounds-and-radio)
