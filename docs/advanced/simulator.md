# Simulator

The `native_sim` build runs the firmware and the real web UI on your own
computer - no ESP32, no flashing. Built-in apps, effects, weather overlays,
scripts and the whole HTTP API behave as they do on AWTRIX, and the live preview
grid in the web UI *is* the simulated panel.

It is the fastest way to develop against the API, try out apps and
notifications, or preview the web UI, without touching hardware.

## Build and run

```bash
pio run -e native_sim
.pio/build/native_sim/program        # Linux/macOS
.pio\build\native_sim\program.exe    # Windows
# then open http://localhost:8080
```

On Windows, if `g++` is not on your `PATH` the build falls back to a portable
w64devkit - see [Host toolchain](building.md#host-toolchain).

Continuous integration builds the unit tests and the firmware, but not
`native_sim` (see [Building from source](building.md#continuous-integration)), so
a green CI run is no promise that the simulator still compiles. Build it locally
to find out.

### Command-line flags

| Flag | Default | Meaning |
|---|---|---|
| `--port <n>` | `8080` | TCP port for the web UI and API |
| `--data <dir>` | `simdata` | Host directory used as the device filesystem |
| `--webui <file>` | `webui/index.html` | The web UI HTML served at `/` |
| `--matrix` | - | Force the terminal matrix on |
| `--no-matrix` | - | Force the terminal matrix off |

On first run the simulator creates the data directory tree - `simdata/` plus
`ICONS/`, `PALETTES/`, `MELODIES/` and `SCRIPTS/` subfolders - mirroring the
filesystem layout AWTRIX keeps in flash.

## What runs, and what does not

The API behaves like the hardware: the same request produces the same response.
It renders at the same 40 frames per second too, so scroll speeds, effects and
GIF timing match what the panel does.

| On AWTRIX | In the simulator |
|---|---|
| Flash filesystem | the `--data` directory (default `simdata/`) |
| Stored settings and device configuration | `simdata/settings.json`, `simdata/device.json` |
| Embedded web UI | `webui/index.html` served from disk - edit it and hit F5 |
| Buttons, light sensor, battery, environment sensor | fake readings you drive over `/sim/` |
| Buzzer and MP3 player | melodies and file plays are printed to stdout, not sounded |
| MQTT | the same client, against a real broker |

The following are **not simulated**. Where the corresponding API route still
exists, it is accepted and then ignored:

- **Wi-Fi and provisioning** - no access point, no `wifi-scan` (it returns `[]`).
- **Art-Net** - no network DMX input.
- **mDNS / UDP discovery** and **OTA** - `/update` responds `501 notSupported`.
- **The button webhook** - `buttonCallback` is stored but never called, however you press the
  simulated buttons.

`/api/v1/system` accepts the network fields so the web UI behaves identically,
but they have no effect here. `reboot`, `sleep`, factory reset and firmware
update are logged and ignored - restart the binary, or delete files under
`simdata/`, instead. The simulator never asks for an HTTP login, whatever
`authEnabled` is set to.

## MQTT

Set `mqttHost` and switch `mqttEnabled` on - on the web UI's MQTT tab, with
`PUT /api/v1/system`, or by editing `simdata/device.json` - then restart the
binary, because the MQTT configuration is read once at startup.

The simulator then connects to that broker exactly as AWTRIX does: availability,
capabilities, periodic state, Home Assistant discovery, the `<prefix>/cmd/#`
command topic and a script's `mqtt.publish()` / `mqtt.subscribe()` all work
against it. With `mqttPrefix` left empty the topics start with `simulator/`.
See [MQTT automation](../guides/mqtt.md) for the setup and
[MQTT topics](../reference/mqtt.md) for the topic list.

## Poking the fake hardware - `/sim/`

Because there are no real buttons or sensors, the simulator exposes an extra
`/sim/` route group to drive them. `GET /sim` lists the routes and their fields.

```bash
# Press a button (left | select | right). Body is optional.
curl -X POST -d '{}' http://localhost:8080/sim/button/left
curl -X POST -d '{"durationMs":400}' http://localhost:8080/sim/button/select

# Set the fake sensors.
curl -X PUT -H "Content-Type: application/json" \
  -d '{"temperature":28.5,"humidity":55,"ldrRaw":300,"batteryPinMillivolts":2100}' \
  http://localhost:8080/sim/sensors
```

**Buttons** - `POST /sim/button/{left|select|right}`. The optional body takes a
single `durationMs` (a whole number of milliseconds, minimum **40** - the press
must clear the 35 ms debounce). Omit the body and the press defaults to 80 ms,
above the debounce and below the 300 ms double-press window. Presses feed the
same debounce and double-press logic as AWTRIX, so app navigation, notification
dismiss and the double-press power toggle all respond. Send a body even when it
is empty (`-d '{}'`): a bare `POST` with no body leaves the connection waiting
for a `Content-Length` that never comes.

**Sensors** - `PUT /sim/sensors` (also `PATCH`) accepts exactly `temperature`,
`humidity`, `ldrRaw` and `batteryPinMillivolts`. Any subset is allowed; the loop
resamples them, so allow about 2 seconds before reading
[`/api/v1/device`](../reference/device.md) back. The default fake readings are
temperature `21.5`, humidity `42`, `ldrRaw` `1200` (mid-daylight on the 0–4095
ADC scale) and `batteryPinMillivolts` `2290`.

`batteryPinMillivolts` is the raw voltage at the ADC pin, not a percentage: the
resistor divider and the discharge curve sit between it and the `batteryPercent`
and `batteryVoltage` you read back. The default `2290` corresponds to roughly
4.10 V and about 88 % at the default divider.

Both `/sim` routes reject what they don't understand with a `400` rather than
silently ignoring it - an unknown field is `400 unknownField`, a body that is not
a JSON object is `400 invalidJson`, and a `durationMs` that is not a whole number
or is below 40 is `400 invalidField`. Nothing is applied unless the whole body
validates, so a rejected request never half-lands.

## Host files: `device.json` and `settings.json`

Two files in the data directory hold what AWTRIX keeps in flash, in the **same
JSON schema as the API**:

| File | Device equivalent | Written by |
|---|---|---|
| `simdata/settings.json` | the stored settings | the settings API / web UI |
| `simdata/device.json` | device (GPIO / network) config | the system API / web UI |

The app-loop order lives in `simdata/apploop.json`, radio stations in
`simdata/radio.json`, Berry sources and their stores in `simdata/SCRIPTS/`, and
uploaded icons, palettes and melodies in the matching subfolders. Pushed apps are
held in memory, as they are on AWTRIX, so nothing on disk corresponds to them and
restarting the binary clears them.

!!! warning "Hand-editing these files bypasses validation"
    The API and the web UI validate before they write; editing `settings.json` or
    `device.json` by hand skips all of that. Values are applied on load with no
    checks, and a file that is not valid JSON is discarded entirely - the
    simulator silently falls back to defaults and your configuration is gone. If
    it behaves strangely after a manual edit, delete the file and let it be
    recreated.

## Terminal matrix

Run the simulator in a terminal and it draws the matrix right there, in ANSI
truecolor blocks, redrawn on every rendered frame. Unlike the web UI preview,
which polls, the terminal shows every frame with the **effective** brightness
applied, so auto-brightness and moodlight dimming are visible. Log lines scroll
below a pinned matrix, and a status line reports the measured fps, the brightness
and the current app.

The panel fills the window: each LED is blown up to the largest square block the
width and the free rows allow, separated by a thin grid like the one in the web
UI live preview. Resize the window and the matrix rescales within half a second;
`COLUMNS` / `LINES` override the detected size.

It needs a VT-capable terminal (Windows Terminal, Windows 10+ conhost, or any
POSIX terminal), and is on by default unless stdout is redirected. The default
32-wide panel fits from about 34 columns, and wider panels need more. Force it
with `--matrix` or turn it off with `--no-matrix`.
