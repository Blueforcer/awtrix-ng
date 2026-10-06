# Sound<!-- only esp32-s3 tc002 --> and music<!-- /only --> {#sound-and-music}

Play sounds from your script<!-- only esp32-s3 tc002 -->, and draw what the music does<!-- /only -->.

## What you get

<a id="sound"></a>

```berry
class Bell
  def on_button(btn)
    if btn == "select" && !sound.playing()
      sound.play({'rtttl': 'bell:d=4,o=5,b=100:e,c'})
    end
  end

  def draw()
    clear()
    text(1, 6, "Bell", 0xFFFFFF)
  end
end

return Bell()
```

Press select while the app is shown, and the clock plays a short melody. `sound.playing()` keeps
a quick double press from playing it twice.

## How it behaves

- **`sound.play()` returns at once**, and the sound plays while your script goes on.
- **`true` means the clock took the sound, not that you hear it.** A name that is not stored plays
  nothing, and the volume may be `0`. `false` means this clock can play none of it.
  [What the answer means](#what-the-answer-means) lists every case.
<!-- only esp32 esp32-s3 -->
- **Your script plays one sound at a time.** Another sound replaces it.
<!-- /only -->
<!-- only tc002 -->
- **Your script plays three kinds of sound at once:** a single sound on top, effects over the
  music, and music underneath everything. See [Music and effects](#music-and-effects).
<!-- /only -->
- **An alert is never cut off by a script.** The sound of a notification plays over your
  script's sounds.
<!-- only esp32-s3 tc002 -->
- **`music` turns sound into numbers you can draw.** Read them in `draw()`: the readings for
  playback match the moment you hear the sound.
<!-- /only -->
- **Start sounds in a button handler or in `loop()`**, never in `draw()`, which runs about 40
  times a second.

## What to play

`sound.play(x)` takes a sound, the same one [`/api/v1/audio/play`](../sounds.md#play-a-sound)
takes. In Berry it is a string, a map or a list:

<!-- only esp32 esp32-s3 -->
```berry
sound.play("doorbell")                           # a stored sound, by name
sound.play({'rtttl': 'beep:d=16,o=6,b=200:c'})   # a melody in the call
sound.play({'track': 3})                         # a DFPlayer track
sound.play({'file': 'siren', 'loop': true})      # repeats until you stop it
```
<!-- /only -->
<!-- only tc002 -->
```berry
sound.play("doorbell")                           # a stored MP3 or melody, by name
sound.play({'rtttl': 'beep:d=16,o=6,b=200:c'})   # a melody in the call
sound.play({'file': 'siren', 'loop': true})      # repeats until you stop it
sound.play([{'speech': 'Door open'}, 'doorbell'])  # speech, else the doorbell
```
<!-- /only -->

A map has exactly one of these keys. The key says what plays:

| Key | Value | Plays |
|---|---|---|
<!-- only esp32 -->
| `'file'` | a name such as `'doorbell'` | a stored melody |
<!-- /only -->
<!-- only esp32-s3 -->
| `'file'` | a name such as `'doorbell'`, or a script's sound such as `'Racer/boost'` | a stored MP3 or melody |
<!-- /only -->
<!-- only tc002 -->
| `'file'` | a name such as `'doorbell'`, a script's sound such as `'Racer/boost'`, or a web address | a stored MP3 or melody, or an MP3 from the internet |
<!-- /only -->
| `'rtttl'` | an [RTTTL](../sounds.md#writing-rtttl) melody | the melody |
<!-- only esp32-s3 tc002 -->
| `'song'` | [song text](../../reference/songs.md) | a song on the synthesizer |
<!-- /only -->
<!-- only tc002 -->
| `'speech'` | text, 1 to 512 bytes | the text, read aloud, when your clock has a voice |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `'track'` | a whole number from 1 to 2999 | a track from the DFPlayer |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `'station'` | a station name, a position in the list, or a stream address | internet radio, see [Internet radio](../radio.md) |
<!-- /only -->

Next to the key, a map may have:

- `'loop': true` to repeat the sound until it is stopped.
<!-- only tc002 -->
- `'nextBar': true`, only with `'song'` and `'loop': true`. See
  [Songs and synth effects](#songs-and-synth-effects).
<!-- /only -->

A plain name is short for `{'file': name}`. A list holds 1 to 4 sounds, and the clock plays the
first one it can play.
<!-- only esp32-s3 tc002 -->

`'station'` starts the radio, as the web UI does. It is not allowed in a list, and `sound.stop()`
does not stop it.
<!-- /only -->

## One sound after another

`sound.playing()` lets you wait for one sound to finish before you start the next. A button that
plays a sound needs this, or a quick double press plays it twice. See
[What you get](#what-you-get).

Play sounds one after another in `loop()`, never in `draw()`:

```berry
  var queue

  def init()
    self.queue = ["chime", "alarm"]     # sounds waiting to play
  end

  def loop()
    if size(self.queue) > 0 && !sound.playing()
      sound.play(self.queue.pop(0))
    end
  end
```

Use `notify()` when the sound belongs to an event that should also interrupt the rotation and show
something. Use `sound` when you only want the sound. A `notify()` sound is an alert, so it plays
over your script's sounds. See [Notifications](device.md#notifications).

## Different clocks

Clocks have different sound hardware. Ask `sound.can()` before you play something only some clocks
have:

<!-- only esp32 -->
```berry
  def on_button(btn)
    if btn == "select" && !sound.playing()
      if sound.can('track') sound.play({'track': 1})
      else sound.play({'rtttl': 'bell:d=4,o=5,b=100:e,c'}) end
    end
  end
```
<!-- /only -->
<!-- only esp32-s3 tc002 -->
```berry
  def on_button(btn)
    if btn == "select" && !sound.playing()
      if sound.can('mp3') sound.play("doorbell")
      else sound.play({'rtttl': 'bell:d=4,o=5,b=100:e,c'}) end
    end
  end
```
<!-- /only -->

A list does the same in one call. The clock plays the first entry it can play:

```berry
sound.play(["doorbell", {'rtttl': 'bell:d=4,o=5,b=100:e,c'}])
```

`sound.can()` answers these keys, among others. They are the `audio` flags of
[`GET /api/v1/capabilities`](../sounds.md#what-your-clock-can-play):

| Key | `true` when the clock can |
|---|---|
<!-- only esp32-s3 tc002 -->
| `'mp3'` | play MP3 files |
<!-- /only -->
| `'rtttl'` | play melodies |
<!-- only esp32-s3 tc002 -->
| `'song'` | play song text on its synthesizer |
<!-- /only -->
<!-- only tc002 -->
| `'speech'` | read text aloud |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `'track'` | play DFPlayer tracks |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `'radio'` | play internet radio |
<!-- /only -->
<!-- only tc002 -->
| `'url'` | play an MP3 from a web address |
| `'effect'` | play effects and music over each other |
| `'clip'` | play a recording sent to `/api/v1/audio/clip` |
<!-- /only -->

A script that cannot work without one of them says so in its header, for example
<!-- only esp32 esp32-s3 -->`# @needs audio.rtttl`<!-- /only --><!-- only tc002 -->`# @needs audio.effect`<!-- /only -->. See [Sharing](sharing.md#what-your-script-asks-of-the-clock).

<!-- only esp32 esp32-s3 -->
## A sound that repeats

Your script plays one sound at a time. A sound with `'loop': true` repeats until it is stopped or
another sound replaces it, an alert too.
<!-- /only -->
<!-- only esp32-s3 -->

## Songs

On a clock with a synthesizer, `sound.can('song')`, your script plays music written as
[song text](../../reference/songs.md): instruments and notes in a string, with no file to upload.
A song is a sound like any other: `sound.play({'song': text})` plays it once, and with
`'loop': true` it repeats as [a sound that repeats](#a-sound-that-repeats) does.

```berry
# @name   Pulse
# @needs  audio.song

class Pulse
  var tune

  def init()
    self.tune = "bpm 100\n"
                "inst pad  wave=saw unison=12 attack=200 sustain=60 release=400 cutoff=1500 volume=65\n"
                "inst kick wave=sine note=c2 pitch=24/25 attack=0 decay=200 sustain=0 volume=200\n"
                "pad:  [a3 c4 e]:16 | [f3 a c4]:16 |\n"
                "kick: (%x...x...x...x...)2"
  end

  def on_show()
    sound.play({'song': self.tune, 'loop': true})   # the music starts with the app
  end
  def on_hide()
    sound.stop()                                    # and stops with it
  end

  def draw()
    clear()
    text(1, 6, "Pulse", 0xFF4000)
  end
end

return Pulse()
```

Berry joins string literals that follow each other, even across line breaks and comments. So you can
write a song one statement per line, as above. Every piece except the last ends in `\n`, the line
break between two statements.

- On a clock without a synthesizer, `sound.play` returns `false`.
- A mistake in the text raises a `value_error` with the reason, line and column, for example
  `song: unexpected 'x' (line 2, column 7)`. Text you build while the script runs belongs in a
  `try`.
- A song plays to the end of its longest track, then starts again from its first bar when it
  repeats. Its `loop` line makes no difference on this clock.
<!-- /only -->
<!-- only tc002 -->
## Music and effects

<a id="effects-over-a-loop"></a>

Your script plays three kinds of sound at once. This is what a game needs:

| Kind | Started by | Plays |
|---|---|---|
| Single sound | `sound.play(x)` | on top. The next one replaces it |
| Effects | `sound.effect(x)` | over the music. Several at once, and they stop nothing |
| Music | `sound.play(x)` with `'loop': true` | underneath everything, until it is stopped |

```berry
  def on_show()
    sound.play({'file': 'theme', 'loop': true})
  end
  def on_hide()
    sound.stop()
  end
  def on_button(btn)
    if btn == "select" sound.effect("jump") end
  end
```

- Stored MP3s and song text play as effects. Any other sound, an address for example, plays like
  `sound.play()`.
- An effect plays once, even with `'loop': true`.
- Up to four MP3 effects and four song effects play together. A fifth cuts off the oldest. An
  effect plays for at most 10 seconds.
- Asking for the music that already plays changes nothing. Other music replaces it.
- While a single sound plays, the music gets quieter. While an alert plays, the effects get
  quieter too.
- Your script's sounds stop when you call `sound.stop()`, and when the script is deleted, saved
  again or stops with an error. Stop them in `on_hide()` when they belong to what your app shows.
- Your script's sounds pause the radio. It comes back once they have ended, the music included.
- A radio station that starts ends the music and the effects. A single sound that plays finishes
  first.
- Home Assistant Voice silences everything while it listens and answers. The music then starts
  again from the beginning. Music you started or stopped meanwhile counts. Effects do not come
  back.
- Everything your script plays is at the **Apps** volume of the [mixer](../sounds.md#volume).

Clocks without effects play one sound at a time there. A script meant for every clock checks
`sound.can('effect')` or `sound.can('song')` before it uses `sound.effect()` or `sound.beat()`.

## MP3s from the internet

`'file'` also takes a web address. The script does not need to bring the file
along.

```berry
  def on_show()
    if sound.can('url')
      sound.play({'file': 'https://example.com/rain.mp3', 'loop': true})
    end
  end
```

- The clock downloads the whole file first, then plays it. That takes a moment.
- A file can be up to 4 MB, and the clock needs enough free memory for it.
- Asking again for the same address as music changes nothing. The music keeps playing.
- `true` only means the request was accepted. If the file cannot be downloaded, nothing plays, and
  `sound.playing()` stays `false`.

## Speech

A clock with a voice reads English text aloud:

```berry
# @needs audio.speech

class TalkingClock
  def on_button(btn)
    if btn == "select"
      sound.play({'speech': "It is " + str(hour()) + ":" + format("%02d", minute())})
    end
  end

  def draw()
    clear()
    text(0, 6, format("%02d:%02d", hour(), minute()), 0xFFFFFF)
  end
end

return TalkingClock()
```

- The text is a string. Build it with `str()` and `format()`, as above.
- The clock reads it as described in [Speech](../sounds.md#speech): up to 512 bytes, numbers and
  times as words, emoji skipped.
- Speech is a single sound like an MP3. The next sound replaces it, `sound.stop()` stops it, and
  `sound.playing()` is `true` while it speaks.
- On a clock without a voice, `sound.play()` returns `false`. Give a list to play something else
  there: `sound.play([{'speech': 'Door open'}, 'doorbell'])`.
- Empty text, or text longer than 512 bytes, raises a `value_error`. On a clock with a voice, so
  does text with nothing to read, for example `'...'`.

## Songs and synth effects

Your clock has a synthesizer, `sound.can('song')`. It plays music written as
[song text](../../reference/songs.md): instruments and notes in a string, with no file to upload.
`sound.play()` with `'loop': true` plays a song as music, `sound.effect()` plays one over it, and
`sound.beat()` tells your picture where the music is. [Song calls](#song-calls) lists every way to
start one.

```berry
# @name   Pulse
# @needs  audio.song

class Pulse
  var tune

  def init()
    self.tune = "bpm 100\n"
                "inst pad  wave=saw unison=12 attack=200 sustain=60 release=400 cutoff=1500 volume=65\n"
                "inst kick wave=sine note=c2 pitch=24/25 attack=0 decay=200 sustain=0 volume=200\n"
                "pad:  [a3 c4 e]:16 | [f3 a c4]:16 |\n"
                "kick: (%x...x...x...x...)2"
  end

  def on_show()
    sound.play({'song': self.tune, 'loop': true})   # the music starts with the app
  end
  def on_hide()
    sound.stop()                                    # and stops with it
  end
  def on_button(btn)
    if btn == "select"
      sound.effect({'song': "inst blip wave=sine attack=0 decay=60 sustain=0 volume=200\nblip: c6:1"})
    end
  end

  def draw()
    clear()
    var beat = sound.beat()
    if beat == nil return end
    var r = int(6 * (1 - beat % 1))     # big on the beat, shrinking until the next
    circle_fill(width() / 2, height() / 2, r, 0xFF4000)
  end
end

return Pulse()
```

Berry joins string literals that follow each other, even across line breaks and comments. So you can
write a song one statement per line, as above. Every piece except the last ends in `\n`, the line
break between two statements.

- On a clock without a synthesizer, `sound.play` returns `false`.
- A mistake in the text raises a `value_error` with the reason, line and column, for example
  `song: unexpected 'x' (line 2, column 7)`. Text you build while the script runs belongs in a
  `try`.
- A beat is a quarter note. `sound.beat() % 1` runs from 0 on each beat to almost 1 just before
  the next, and `int(sound.beat()) % 4` counts the beats of a 4/4 bar. When the song loops, the
  count goes back to the first beat of its `loop` bar. A song started with `'nextBar': true` counts
  from 0 once it takes over.
- `sound.beat()` matches what comes out of the speaker, like the [music](#music) readings. Read it
  in `draw()` and the picture keeps time with the sound. A song with `loop off` in its text plays
  once, and `sound.beat()` returns `nil` once it has ended.
- Song effects count for `sound.playing()`. The music does not.
- Up to four song effects play at once. A fifth cuts off the oldest.
<!-- /only -->

<!-- only esp32-s3 tc002 -->
## Sounds for your script

A script can bring its own sounds: MP3 files in a folder named after the script, next to it.

```
/SCRIPTS/Racer.ax           the script
/SCRIPTS/Racer/boost.mp3    its sounds
/SCRIPTS/Racer/theme.mp3
```

The script plays them by name, like any MP3:

<!-- only esp32-s3 -->
```berry
  def on_show()
    sound.play({'file': 'theme', 'loop': true})   # /SCRIPTS/Racer/theme.mp3
  end
  def on_button(btn)
    if btn == "select" sound.play("boost") end
  end
```
<!-- /only -->
<!-- only tc002 -->
```berry
  def on_show()
    sound.play({'file': 'theme', 'loop': true})   # /SCRIPTS/Racer/theme.mp3
  end
  def on_button(btn)
    if btn == "select" sound.effect("boost") end
  end
```
<!-- /only -->

A plain name is looked up in this order:

1. the script's own folder, `/SCRIPTS/Racer/boost.mp3`;
2. the shared MP3s, `/MP3/boost.mp3`;
3. the melodies, `/MELODIES/boost.txt`.

`sound.play()`<!-- only tc002 -->, `sound.effect()`<!-- /only --> and the `sound` of a [`notify()`](device.md#notifications)
look this way. Anywhere else, in a request or in another script, the sound is `"Racer/boost"`.

A [module](several-apps.md#sharing-code-between-scripts) has no sounds of its own. Its code plays from the folder
of the app that imports it, so that app brings the sounds.

**Add sounds to a script:**

- **From the AWTRIX Hub,** sounds come with the script. This works from its Hub page, and in the
  web UI when it [updates a script](../hub-script-updates.md) or installs one that another script
  [needs](sharing.md#scripts-your-script-needs). A clock that cannot play MP3s gets the script
  without them.
- **By hand** (a shared `.ax` file carries no sounds): open the script on the **Scripts** tab and
  press the note button in the editor bar. It opens the script's group on the **Audio** tab, where
  you upload, play and delete its sounds. See [The web UI → MP3s](../../getting-started/web-ui.md#mp3s).

**Deleting the script asks about its sounds.** **Delete with sounds** removes them. **Keep sounds**
leaves them on the clock, and a script installed again under the same name uses them. Saving a new
version keeps them. Renaming the script in the web UI moves them to the new name.

The files follow the rules for every MP3 on AWTRIX:

- The file name is what the script plays: 1 to 32 characters of `A-Z`, `a-z`, `0-9`, `_` and `-`,
  plus `.mp3`. `boost.mp3` is played as `"boost"`.
- The sound must be an MP3, mono or stereo, at 8 to 48 kHz. A file that is not an MP3 is refused
  on upload or stays silent.
- They share storage with everything else on AWTRIX, so keep them short.
- A script's sound may have the same name as a melody or a shared MP3. Its own sound wins.

Listing, uploading and deleting over the API: [Script sounds](../../reference/http.md#script-sounds).

## Music

`music` turns sound into numbers you can draw.
<!-- only esp32-s3 -->
It analyses what your clock plays: a station or an MP3. This needs a speaker.
<!-- /only -->
<!-- only tc002 -->
It analyses playback or the microphone. Choose under **System → Audio → Music source**. Automatic
uses playback while something plays, otherwise the microphone. Playback and Microphone pick one
source.
<!-- /only -->

The readings for playback match the moment you hear the sound, so picture and sound stay in step.
`music.bands()` answers how loud each range of notes is, from bass to treble, `music.level()` how
loud it is, and `music.beat()` when a beat lands. `music.station()` and `music.title()` name what
the radio plays. [Music calls](#music-calls) lists every answer.

**None of them ever returns `nil`.** When there is no input, they return zeros, `false` and `""`.

The radio does not put the station or the song on the display by itself. A script that shows them
builds the line in `loop()`. `music.title()` changes whenever the station announces a new song:

```berry
def init()
  self.line = ""
end

def loop()
  self.line = music.station() + ": " + music.title()
end

def draw()
  clear()
  scroll_text(self.line, 0xFFFFFF)
end

def should_show()
  return music.title() != ""
end
```

<!-- only tc002 -->
`music.playing()` reports playback only, whatever input is selected. It does not detect voices or
room sound. So for a microphone visualizer, do not hide the app with a `should_show()` based on
`music.playing()`.
<!-- /only -->

A spectrum display takes one line. `bar_chart()` with `autoscale` off draws a fixed 0-to-8 range,
so ask for the bands with `max` 8:

```berry
def draw()
  bar_chart(music.bands(16, 8), "Rainbow", false)
end
```

The classic look adds a peak dot per bar that hangs for a moment and then falls. This one also
skips its turn while nothing plays:

```berry
class Spectrum
  var peaks
  var hold

  def init()
    self.peaks = []
    self.hold = []
    for i: 0..15
      self.peaks.push(0)
      self.hold.push(0)
    end
  end

  def should_show()
    return music.playing()
  end

  def draw()
    var bands = music.bands(16, 8)
    var bar_w = (width() - 15) / 16
    bar_chart(bands, "Rainbow", false)
    for i: 0..15
      if bands[i] >= self.peaks[i]
        self.peaks[i] = bands[i]
        self.hold[i] = 12
      elif self.hold[i] > 0
        self.hold[i] -= 1
      elif self.peaks[i] > 0
        self.peaks[i] -= 1
      end
      if self.peaks[i] > 0
        pixel(i * (bar_w + 1), height() - 1 - self.peaks[i], 0xFFFFFF)
      end
    end
  end
end

return Spectrum()
```

`music.beat()` is for a pulse, a circle that flares on every beat and shrinks again:

```berry
  var glow

  def init()
    self.glow = 0
  end

  def draw()
    if music.beat() self.glow = 4 end
    if self.glow > 0
      circle_fill(width() / 2, height() / 2, self.glow, hsv(300, 100, 100))
      self.glow -= 1
    end
  end
```

- The levels adjust themselves: a quiet track fills the display just like a loud one, and the volume
  setting does not change the picture.
- Read `music.beat()` in `draw()`, never in `loop()`. `loop()` runs once a second and would miss
  most beats.
- `music.playing()` turns `true` as soon as playback starts. The first numbers follow a fraction
  of a second later.
<!-- /only -->

<!-- only tc002 -->
### Hearing a note

`music.pitch()` tells which note is sounding: one that someone sings, hums, whistles or plays
into the microphone, or one that the speaker plays. It answers the note's frequency in Hz, from
70 Hz, a low bass voice, to 1600 Hz, a high whistle.

It listens to the same **Music source** as the other readings. With Automatic, a sound your
script plays is heard instead of the singer, so a singing game stays quiet while someone sings.
Declare `# @needs microphone` for a script that cannot work without the microphone.

It answers `0.0`:

- for silence, noise, several notes at once, or a note outside 70 to 1600 Hz;
- while the voice assistant is listening.

Call it in `draw()`, not in `loop()`. The first reading comes a fraction of a second after the
first call.

A note number follows from the frequency: 69 is A4 at 440 Hz, and each semitone adds one. This app
shows the note name, such as `A4`:

```berry
import math

class Tuner
  var names, note, label

  def init()
    self.names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    self.note = nil
    self.label = ""
  end

  def draw()
    var hz = music.pitch()
    if hz <= 0 return end
    var note = int(69 + 12 * math.log(hz / 440) / math.log(2) + 0.5)
    if note != self.note                # build the name only when the note changes
      self.note = note
      self.label = self.names[note % 12] + str(note / 12 - 1)
    end
    text(0, 6, self.label, 0x00FF00)
  end
end

return Tuner()
```
<!-- /only -->

## Good to know

- **A quick double press plays a sound twice.** Check `!sound.playing()` before you play, as in
  [What you get](#what-you-get).
- **Start sounds in a button handler or in `loop()`**, never in `draw()`. `draw()` runs about 40
  times a second.
- **A sound that repeats keeps playing when your app leaves.** Stop it in `on_hide()` when it
  belongs to what your app shows.
<!-- only esp32-s3 tc002 -->
- **A mistake in song text raises a `value_error`.** Put song text you build while the script runs
  into a `try`.
- **Read `music.beat()` in `draw()`, never in `loop()`.** `loop()` runs once a second and misses
  most beats.
<!-- /only -->

## Details

### Calls

| Call | Does | Returns |
|---|---|---|
<!-- only esp32 esp32-s3 -->
| `sound.play(x)` | plays a sound once. With `'loop': true` it repeats until it is stopped | `true` when the clock took it, `false` when it can play none of it |
| `sound.stop()` | stops every sound your script plays | `nil` |
| `sound.stop('loop')` | stops your script's sound that repeats with `'loop': true` | `nil` |
| `sound.playing()` | whether a sound from your script plays right now | `true` or `false` |
<!-- /only -->
<!-- only tc002 -->
| `sound.play(x)` | plays a sound once. With `'loop': true` it repeats, as music | `true` when the clock took it, `false` when it can play none of it |
| `sound.effect(x)` | plays a sound as an effect over the music | as `sound.play()` |
| `sound.stop()` | stops every sound your script plays: single sounds, effects and music | `nil` |
| `sound.stop('loop')` | stops your script's music only | `nil` |
| `sound.playing()` | whether a single sound or an effect from a script plays right now. Music never counts | `true` or `false` |
| `sound.beat()` | the beat of the song you hear right now, see [Songs and synth effects](#songs-and-synth-effects) | a real, or `nil` |
<!-- /only -->
| `sound.can()` | what this clock can play | a map such as `{'mp3': true, 'rtttl': true, ...}` |
| `sound.can(key)` | one entry of that map, for example <!-- only esp32 -->`sound.can('track')`<!-- /only --><!-- only esp32-s3 -->`sound.can('mp3')`<!-- /only --><!-- only tc002 -->`sound.can('effect')`<!-- /only --> | `true` or `false` |

### What the answer means

`true` means the clock took the request, and the sound starts right away. `true` does not mean
you hear it:

- A name that is not stored plays nothing.
- While an alert plays, for example the sound of a notification, a single sound from your script
  is not played. An alert is never cut off by a script.
- The [volume](../sounds.md#volume) may be `0`.

`false` means this clock cannot play any of it, for example
<!-- only esp32 esp32-s3 -->`{'track': 3}` on a clock without a DFPlayer<!-- /only --><!-- only tc002 -->`{'track': 3}`, because your clock has no DFPlayer<!-- /only -->.
It never stops your script.

A mistake in the sound raises a `value_error` at the call, with the key and the reason:

```berry
sound.play({'rtttl': 'd=4,o=5,b=120:c,e,g'})
# value_error: rtttl: missing ':' (at offset 19)
```

A map with two sound keys, an unknown key, a bad name or a track out of range raise the same way.
The reasons are listed in [When it goes wrong](../sounds.md#when-it-goes-wrong).

<!-- only tc002 -->
### Song calls

| Call | Does |
|---|---|
| `sound.play({'song': text, 'loop': true})` | plays the song as music, underneath everything, until it is stopped. It replaces MP3 music, and MP3 music replaces it. The same text again keeps it playing where it is. Other text starts that song from the beginning |
| `sound.play({'song': text, 'loop': true, 'nextBar': true})` | lets the playing song run to its next bar line and starts this one there, so the music changes in time. With no song playing it starts at once |
| `sound.play({'song': text})` | plays the song once, as a single sound |
| `sound.effect({'song': text})` | plays the song once as an effect, over the music and next to MP3 effects |
| `sound.beat()` | the beat of the music you hear right now, as a real number counted from the start of the song, or `nil` while no song plays as music |
| `sound.stop('loop')` | stops the music |
<!-- /only -->

<!-- only esp32-s3 tc002 -->
### Music calls

| Call | Answer |
|---|---|
| `music.bands(n?, max?)` | a list of `n` numbers, bass on the left, treble on the right. `n` is 1 to 32 (default 32), and each number is scaled from 0 to `max` (default 255) |
| `music.level()` | how loud it is right now, 0 to 255, relative to the quietest and loudest moment of the last few seconds, so a compressed station still moves the needle |
| `music.beat()` | `true` for exactly one frame each time a beat lands |
| `music.playing()` | `true` while a station or an MP3 is playing |
| `music.station()` | the name of the playing station, or its URL when it was started by URL. `""` when no station plays |
| `music.title()` | the song title the station sends right now, or `""` when it sends none or no station plays |
<!-- only tc002 -->
| `music.pitch()` | the pitch of the note in Hz, as a real (`440.0` is concert A), or `0.0`. See [Hearing a note](#hearing-a-note) |
<!-- /only -->
<!-- /only -->

Every sound limit is in [Limits](../../reference/limits.md#sounds-and-radio).

## Related

- [Sound](../sounds.md): stored sounds, melodies, the volume and what each clock can play
<!-- only esp32-s3 tc002 -->
- [Song text](../../reference/songs.md): instruments and notes in a string
- [Internet radio](../radio.md): the stations `music` reads
<!-- /only -->
- [Controlling the device](device.md#notifications): a sound together with a notification
