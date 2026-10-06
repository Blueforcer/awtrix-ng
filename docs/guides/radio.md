---
only: [esp32-s3, tc002]
---

# Internet radio

This page shows how to add internet radio stations to your clock and play them.

<!-- only tc002 -->
The radio plays on the built-in speaker. There is nothing to wire.
<!-- /only -->
<!-- only esp32-s3 -->
Radio needs a board with usable PSRAM, the matching firmware image and an external I²S DAC, which
is a small audio amplifier board. See [What you need](#what-you-need) and
[Which of the two images](../getting-started/flashing.md#which-of-the-two-s3-images).
<!-- /only -->

`capabilities.audio.radio` in [`GET /api/v1/capabilities`](../reference/http.md#get-apiv1capabilities)
tells you if your clock can play radio. When it is `false`, playing a station answers
`503 unavailable`. You can still edit the station list, read `GET /api/v1/audio`, and send stop.

## How it behaves

The radio plays in the background: the apps keep taking turns on the display while it plays. One
station plays at a time, until you stop it or start another one. An alert, for example a
notification sound, pauses the radio, and the radio comes back by itself afterwards
([What plays over what](sounds.md#what-plays-over-what)). When the connection drops, the clock
connects again by itself. Your stations stay on the clock after a restart and are part of a
backup.

## Add stations {#adding-stations}

1. Open the web UI and go to the [**Audio** tab, Radio section](../getting-started/web-ui.md#radio).
2. Press **+ Add station**.
3. Enter a name and the stream URL, then press **Save**.

You can store up to 32 stations. Names are up to 24 characters, URLs up to 255.

Or send the whole list with a request:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/audio/stations \
  -H 'Content-Type: application/json' \
  -d '{"stations":[{"name":"SWR3","url":"https://liveradio.swr.de/sw282p3/swr3/"}]}'
```

If the list has a mistake, the stored list stays as it was, and the error names the wrong row:

```json
{"error":{"code":"validationFailed","message":"must not be empty","field":"stations[1].name"}}
```

### Playlist URLs

Station directories often give you a `.m3u` or `.pls` playlist instead of the stream itself. You
can use those URLs: AWTRIX plays the first stream in the playlist. A playlist that points to another
playlist does not work and is reported as an error. Every reconnect reads the playlist again, so a
stream link that expires after a while keeps working.

The server may also identify a playlist with `Content-Type: audio/x-mpegurl`, `audio/mpegurl`
or `audio/x-scpls`, even when the URL has no playlist extension.

## Play a station {#playing}

In the web UI, press **▶** next to the station on the **Audio** tab. On the clock, hold select for
half a second and choose **Radio** in the [menu](device-controls.md#the-menu). Or send:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' -d '{"station":"SWR3"}'
```

`station` takes three kinds of value:

| Body | Plays |
|---|---|
| `{"station":"SWR3"}` | the station of that name in your list |
| `{"station":0}` | the station at that position in your list, counted from 0 |
| `{"station":"https://example.com/live.mp3"}` | a stream that is not in your list |

To stop the radio and nothing else:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/stop \
  -H 'Content-Type: application/json' -d '{"group":"radio"}'
```

Over MQTT the same commands are `cmd/audio/play`, `cmd/audio/stop` and `cmd/audio/stations`.

When a station does not play, the answer says why:

| Status | `field` | `message` | Why |
|---|---|---|---|
| `404` | `station` | `unknown station` | no station in your list has that name |
| `404` | `station` | `no station at that position` | your list is shorter |
| `422` | `station` | `invalid URL` | the stream address cannot be used |
| `503` | | `no audio output` | your clock cannot play radio |

### See what is playing

`GET /api/v1/audio` shows the radio state and the station list:

```json
{
  "radio": {
    "playing": true,
    "station": "SWR3",
    "title": "Kraftwerk - Das Model",
    "error": "",
    "underruns": 0,
    "decodeUs": 4180,
    "starvedMs": 0,
    "bufferBytes": 12288
  },
  "app": {"playing": false, "name": "", "error": ""},
  "alert": {"playing": false, "name": "", "error": ""},
  "stations": [{"name":"SWR3","url":"https://liveradio.swr.de/sw282p3/swr3/"}]
}
```

The same document is published (retained) on MQTT at `<prefix>/state/audio`.

`underruns` counts the dropouts you can hear. `starvedMs` is the time spent waiting for the station.
If these two numbers grow while music plays, the stream does not arrive fast enough. Usually
this is weak Wi-Fi, sometimes the station. Every field:
[HTTP API → GET /api/v1/audio](../reference/http.md#get-apiv1audio).

## Station and song title on the display {#on-the-matrix}

The radio never puts anything on the display by itself. To show the station or the current song,
use a script. `music.station()` and `music.title()` return both. This script shows the song while
the station sends a title, and lets the rotation skip it otherwise:

```berry
class NowPlaying
  def should_show()
    return music.title() != ""
  end

  def draw()
    scroll_text(music.title(), 0xFFFFFF)
  end
end

return NowPlaying()
```

The title comes from the station. Stations that send no title leave `music.title()` empty.

A script can also draw the music itself with the [`music` module](scripting/sound.md#music), for
example a spectrum, a level meter or a pulse on the beat.

## Volume

1. Open the **Audio** tab.
2. Move the **Radio** slider in the **Mixer** section.

The radio volume is `radioVolume`, 0 to 100. It is the radio's share of the master volume
`volume`. With `volume` at 80 and `radioVolume` at 50, the radio plays at 40. So you can turn a
station down without making your doorbell quieter. See [Volume](sounds.md#volume).

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' -d '{"radioVolume":50}'
```

<!-- only esp32-s3 -->
## What you need {#what-you-need}

You need a **MAX98357A** board, or another I²S DAC that takes a standard 16-bit stereo signal. A
UDA1334A or a PCM5102A work the same way. The MAX98357A drives a speaker directly and needs no
extra amplifier.

| DAC pin | Setting | Default GPIO |
|---|---|---|
| BCLK | `pinI2sBclk` | `5` |
| LRC / WS | `pinI2sLrclk` | `6` |
| DIN | `pinI2sDout` | `4` |

Connect power and ground as well.

Some DAC boards have two more inputs. Both settings are off by default and only work together with
the three pins above:

| Setting | Connect it to |
|---|---|
| `pinI2sMclk` | the DAC's **MCLK** pin, if it has one |
| `pinAmpEnable` | the amplifier's enable pin, for example **CTRL** on an NS4168 or **SD** on a MAX98357A. AWTRIX sets it high at start so the amplifier plays |

Any free output pin works, but do not use GPIO 13–18, 21, 38–42 and 47: the display uses one of
those. The other [GPIO rules](../reference/gpio.md) apply as usual.

Set the pins in the web UI under **System → GPIO**, or with `PUT /api/v1/system`:

```json
{ "pinI2sBclk": 5, "pinI2sLrclk": 6, "pinI2sDout": 4 }
```

Always send the three pins together: either all three set, or all three `-1` to turn the output
off. If only some are set, the request is refused with `422`.
<!-- /only -->

## Limits

- **MP3 streams only.** Most internet radio is MP3. A station that sends AAC does not play, and the
  error says so. Supported: MP3 at 8 to 48 kHz, mono or stereo, any bitrate, variable bitrate too.
- **One station at a time.** Starting a new station stops the one that is playing.
- **Lost connection.** AWTRIX reconnects after two seconds. If that fails, it waits longer each
  time: two seconds, then five, then fifteen. It keeps trying every fifteen seconds while the
  station is unreachable. Stop the radio or pick another station to end the retries.
- Each failed try sets `error` to `connect failed` and `playing` to `false`.<!-- only tc002 --> A
  successful reconnect clears the error and sets `playing` back to `true`.<!-- /only -->
<!-- only tc002 -->
- **HTTPS streams** work. The clock checks the station's certificate and host name. A
  certificate it does not trust stops the connection.
<!-- /only -->
<!-- only esp32-s3 -->
- **HTTPS streams** work. The clock encrypts the connection but does not check the certificate.
<!-- /only -->

## When it does not play

| What you see | What to check |
|---|---|
<!-- only esp32-s3 -->
| No Radio section, playing answers `503` | `capabilities.audio.radio`, the PSRAM image, the wiring and the I²S pins |
| `422` when setting the pins | only some of the three I²S pins are set |
<!-- /only -->
<!-- only tc002 -->
| No Radio section, playing answers `503` | the speaker may not be available. Check the log |
<!-- /only -->
| "not playable MP3" | the station sends AAC or another format that is not MP3 |
| "connect failed" | wrong URL, station offline, or name lookup failed<!-- only tc002 -->, or a certificate the clock does not trust<!-- /only --> |
| Plays, but no title appears | the station sends no titles |
<!-- only esp32-s3 -->
| `503` with "not enough memory for TLS" | the clock is busy at that moment. Try again |
<!-- /only -->

## Good to know

- **Each `PUT /api/v1/audio/stations` replaces the whole list.** Send every station you want to
  keep, not only the new one.
- **`station` works only on its own.** In a list of sounds or in a notification's `sound` it is
  refused with `422`, `not here`. Start the radio with a request of its own.

## Details

- [HTTP API → Audio](../reference/http.md#audio): every route, field and status code
- [Audio playback errors](../reference/errors.md#audio-playback): every answer a play request can
  get
<!-- only esp32-s3 -->
- [GPIO & boards](../reference/gpio.md): the I²S pins and what else can use them
<!-- /only -->

## Related

- [The web UI → Radio](../getting-started/web-ui.md#radio): the Radio section and its controls
- [Sound](sounds.md): melodies, MP3 files and the other sound outputs
