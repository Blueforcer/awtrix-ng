# Internet radio

AWTRIX can play an MP3 internet radio stream and show what is on air. It needs
an ESP32-S3 with PSRAM and an external I²S DAC. Without all three the **Radio**
tab is hidden. `POST /api/v1/audio/play` and `POST /api/v1/audio/stop` answer
`503 unavailable`; `GET /api/v1/audio` still works and reports
`"available": false`, and editing the station list (`PUT /api/v1/audio/stations`)
works on every build.

There is one image per PSRAM type. A quad-PSRAM board (`N8R2`, `N16R2`) needs
`firmware-awtrix-ng-s3-quad.bin`; with the `-s3-octal-` image it shows no PSRAM
on the device page and the Radio section stays hidden.

## What you need

A **MAX98357A** breakout, or any I²S DAC that takes a standard 16-bit stereo
frame - a UDA1334A or a PCM5102A work the same way.

| DAC pin | Clock pin | Default GPIO |
|---|---|---|
| BCLK | bit clock | `5` |
| LRC / WS | word select | `6` |
| DIN | data | `4` |

Plus power and ground. The MAX98357A drives a speaker directly; it needs no
amplifier of its own.

Any free output pin works, but keep clear of GPIO 13–18, 21, 38–42 and 47: the
matrix panel is wired to one of those. The rest of the
[GPIO rules](../reference/gpio.md) apply as usual.

Change the pins under **System → GPIO**, or with a `PUT /api/v1/system`:

```json
{ "pinI2sBclk": 5, "pinI2sLrclk": 6, "pinI2sDout": 4 }
```

The three are one bus and are validated together: send all three, or `-1` for
all three to turn the output off. A half-configured set is rejected with a `422`.

## Adding stations

The [**Audio** tab's Radio section](../getting-started/web-ui.md#radio) in the web UI is the
quickest way in: give each station a name and its stream URL, then save. Up to 32
stations, names up to 24 characters, URLs up to 255.

The same list over the API:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/audio/stations \
  -H 'Content-Type: application/json' \
  -d '{"stations":[{"name":"SWR3","url":"https://liveradio.swr.de/sw282p3/swr3/"}]}'
```

The whole list is replaced on every write. A rejected list leaves the stored one
untouched, and the error names the row that was wrong:

```json
{"error":{"code":"validationFailed","message":"must not be empty","field":"stations[1].name"}}
```

Stations survive a reboot and ride along in a backup.

### Playlist URLs

A URL from a station directory often points at a `.m3u` or `.pls` playlist
rather than at the stream. Those are resolved automatically to their first
playable entry. Only one level is followed - a playlist pointing at another
playlist is reported as an error.

## Playing

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H 'Content-Type: application/json' -d '{"station":"SWR3"}'
```

`{"index":0}` picks by position, and `{"url":"http://..."}` plays something that
is not in the list at all. Stop with `POST /api/v1/audio/stop`.

Over MQTT the same three commands are `cmd/audio/play`, `cmd/audio/stop` and
`cmd/audio/stations`.

`GET /api/v1/audio` reports what is happening and returns the station list in
the same read:

```json
{
  "available": true,
  "playing": true,
  "station": "SWR3",
  "title": "Kraftwerk - Das Model",
  "error": "",
  "underruns": 0,
  "decodeUs": 4180,
  "starvedMs": 0,
  "bufferBytes": 12288,
  "stations": [{"name":"SWR3","url":"https://liveradio.swr.de/sw282p3/swr3/"}]
}
```

The same document is published retained on `<prefix>/state/audio`.

`underruns`, `starvedMs`, `decodeUs` and `bufferBytes` report playback health rather
than settings - `underruns` counts audible dropouts and `starvedMs` the time spent
waiting for data, so a rising pair of those means the stream is not keeping up.
Field-by-field: [HTTP API - GET /api/v1/audio](../reference/http.md#get-apiv1radio).

## On the matrix

With `radioMeta` on - the default - each new track title appears as it arrives,
for about seven seconds, then the normal rotation continues. Turn it off to have
the radio play without ever taking over the display.

Titles come from the stream's ICY metadata. Stations resend the same title every
few seconds; only actual changes are shown.

Volume is `radioVolume`, `0`–`100`. It is separate from `volume`, which drives
the buzzer or a DFPlayer - different hardware, and different scales.

## Limits

**MPEG-1 Layer III only.** That is what internet radio overwhelmingly is, but a
station that only offers AAC will not play - the API reports the stream as
unusable. 44.1, 48 and 32 kHz, mono or stereo, any bitrate including variable.

**One stream at a time.** Starting a new station stops the current one.

A dropped connection is reconnected after a two-second pause. If that fails the
wait grows - two seconds, then five, then fifteen - and holds at fifteen for as
long as the station stays unreachable. Stop the radio or pick another station to
end the retries.

Each failed attempt sets `error` to `could not connect to the station` and
`playing` to `false`. Nothing about it appears on the matrix.

HTTPS streams work, and their certificates are not verified.

## When it does not play

| Symptom | Cause |
|---|---|
| Radio section missing, play/stop answer `503` | Not an ESP32-S3 image, no PSRAM, the octal image on a quad-PSRAM board, or the I²S pins are `-1` (station editing and `GET /api/v1/audio` still work) |
| `422` on the pins | One of the three is set and the others are not |
| "this stream is not MPEG-1 Layer III audio" | An AAC or otherwise unsupported mount |
| "could not connect to the station" | Wrong URL, station offline, or DNS is not resolving |
| Plays but no title ever appears | The station sends no ICY metadata; nothing to show |
| `503` with "not enough free memory for a TLS connection right now" | An HTTPS stream asked for at a moment with too little free heap; retry |

## Related

- [Web UI tour - Radio](../getting-started/web-ui.md#radio) - the Audio tab's Radio section, and what each control does
- [HTTP API - Radio](../reference/http.md#radio) - every route, field and status code
- [GPIO & boards](../reference/gpio.md) - the I²S pins and what else can sit on them
- [Sounds & melodies](sounds.md) - the buzzer, for alerts rather than music
