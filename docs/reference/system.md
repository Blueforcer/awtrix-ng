# System configuration

`GET`/`PUT /api/v1/system` is the **system configuration** of AWTRIX: Wi-Fi, MQTT and Home
Assistant, NTP and timezone, identity, web port and authentication, sensor and battery
calibration, the auto-brightness range, panel layout, and the complete runtime GPIO map.

It is **69 flat fields**, one JSON object, no nesting. This page documents every one of them.

[Settings](settings.md) is a different resource: it controls how the display *behaves* -
brightness, apps, transitions - and is written with `PATCH /api/v1/settings`. How your panel is
built and wired belongs here.

Most system fields are read **once at boot**. Reboot after changing them:
`curl -X POST http://<awtrix-ip>/api/v1/device/reboot`. The "reboot" column in every table
below says which fields are the exception.

## Read the whole configuration

```bash
curl http://<awtrix-ip>/api/v1/system
```

Returns `200` with the full configuration as a flat JSON object, keyed by the field names in
this page:

```json
{
  "wifiSsid": "MyNetwork",
  "netStatic": false,
  "mqttHost": "192.168.1.10",
  "mqttPort": 1883,
  "hostname": "kitchen-clock",
  "webPort": 80,
  "minBrightness": 10,
  "maxBrightness": 220,
  "pinMatrix": 32
}
```

`wifiPass`, `mqttPass` and `authPass` are **secrets** and are omitted from a plain `GET`. Add the
`secrets` query parameter - `GET /api/v1/system?secrets=1` - to get their stored values back as
well; that is how the web UI's backup export writes an archive you can restore from. The parameter
is ignored while AWTRIX is in provisioning AP mode, where the open access point would otherwise
hand the credentials to anyone in range. A `PUT` response never includes them.

## Write a configuration

`PUT` accepts a **partial** object. Send only the keys you want to change; absent keys keep
their stored value, and unknown keys are ignored without error.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttHost":"192.168.1.10","mqttPort":1883,"mqttUser":"awtrix"}'
```

Returns `200` with the resulting configuration (same shape as `GET`, secrets still omitted).
The write is persisted immediately.

A rejected write changes nothing. The statuses this route answers and every validation message it
produces are in [Errors - PUT /api/v1/system](errors.md#put-apiv1system) and
[Errors - GPIO validation](errors.md#gpio-validation-invalidpinconfig).

The `Content-Type` header is
[mandatory on every `PUT`](conventions.md#content-type-is-mandatory).

### What `PUT` checks

Every numeric field is type- and range-checked **before anything is stored**. A bad value is
rejected with `422 validationFailed` naming the offending `field`, exactly like
`PATCH /api/v1/settings`.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"webPort":70000}'
```

```json
{ "error": { "code": "validationFailed", "message": "out of range", "field": "webPort" } }
```

Each field's accepted range is the "Range" column of its own table below. Every `pin*` field takes
`-1` (disabled) or a GPIO within the chip's range - 0–39 on the ESP32, 0–48 (22–25 do not exist) on
the ESP32-S3; see [GPIO & boards](gpio.md).

Integer fields also reject a non-integer (`{"mqttPort":"eighty"}` and `{"tempDecimals":1.5}`
both fail with `422`); the float fields accept any number in range.

### What `PUT` does not check

Beyond the numeric ranges, the two enums (`panelStart`, `panelWiring`), the dotted-quad address
fields and the blanking rules below, strings are not validated, and unknown keys are ignored
without error - the resource is a merge, not a replacement. The **deeper GPIO rules**
(duplicates, input-only pins, the matrix whitelist) are separate and answer
`400 invalidPinConfig`; the rules and their exact messages are in
[GPIO & boards](gpio.md).

### Switches, blank fields and secrets

Each of the two optional subsystems has its own boolean gate: `mqttEnabled` runs the MQTT client,
`authEnabled` requires HTTP Basic auth. The subsystem runs only while its flag is `true`; turning
it off keeps the stored host / username / password, so re-enabling needs no retyping. The switch,
not the emptiness of a field, decides whether the subsystem runs, so `mqttHost` and `authUser` can
be blanked freely.

A gate may only be armed when it has what it needs, checked with `422 validationFailed`:

| Set | Requires | Field named on `422` |
|---|---|---|
| `mqttEnabled: true` | a non-empty `mqttHost` | `mqttHost` |
| `authEnabled: true` | a non-empty `authUser` **and** `authPass` | `authUser` |

One string is load-bearing: `wifiSsid`. A blank SSID *is* the "no network → AP mode" signal, so
`PUT` refuses to blank it (`422`, `field: wifiSsid`) and points at
`POST /api/v1/device/factory-reset`.

The three secret fields ignore a blank value and leave the stored secret intact, so a `PUT` can set
or change a password but never clear one. To disable a subsystem, flip its gate off; to wipe stored
secrets entirely, use `POST /api/v1/device/factory-reset`.

Nothing on the wire tells you a reboot is needed, and the web UI's "reboot required" banner is
unconditional - it appears after any successful save, including changes to fields that apply live.
Use the "reboot" column in the tables below.

## Wi-Fi

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `wifiSsid` | string | - | `""` | Network to join. Empty → AWTRIX reconnects with whatever credentials it already has stored. No format validation. | yes |
| `wifiPass` | string | - | `""` | Passphrase. **Secret**: never returned; blank on write keeps the stored value. | yes |
| `netStatic` | bool | - | `false` | `false` = DHCP. Static addressing is applied only when `netStatic` is true **and** `ip` is non-empty. | yes |
| `ip` | string | dotted quad, optional `/0`–`/32` suffix | `""` | Static IP. May carry the mask as a CIDR suffix - `192.168.1.50/24` is split and stored as `ip` + `subnet` (a non-empty `subnet` in the same request is a `422`). Gates the whole static block - `netStatic: true` with an empty `ip` silently stays on DHCP. Malformed values are rejected with `422`; `""` leaves it unset. | yes |
| `gateway` | string | dotted quad | `""` | Static gateway. Also the **fallback for `dns1`** when `dns1` is empty. | yes |
| `subnet` | string | dotted quad | `""` | Static subnet mask. GET always reports the mask here, even when it was set via the `/24` suffix on `ip`. Required once `netStatic` is true and `ip` is set - the merged config is rejected with `422` when it is empty. | yes |
| `dns1` | string | dotted quad | `""` | Primary DNS. Empty → falls back to `gateway`. | yes |
| `dns2` | string | dotted quad | `""` | Secondary DNS. Empty → none. | yes |
| `wifiConnectTimeout` | long | 5000–120000 ms | `15000` | How long the boot join may take before AWTRIX falls back to the provisioning AP. Raise it for a network that associates slowly. While in the AP, AWTRIX retries the stored credentials every 30 s and restarts once it gets in. | yes |
| `wifiRoamRssi` | int | −90–0 dBm | `0` | Roam away from an access point weaker than this. `0` is off. Requires a link that stays below the threshold for ~30 s, and roams at most once every 5 minutes. **Roaming is a reconnect, not a handover** - the link drops for a second or two and MQTT goes with it. | yes |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"wifiSsid":"MyNetwork","wifiPass":"secret123"}'
```

Static addressing needs `netStatic` and `ip` together:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"netStatic":true,"ip":"192.168.1.50/24","gateway":"192.168.1.1"}'
```

The `/24` suffix names the mask; `"ip":"192.168.1.50","subnet":"255.255.255.0"` is the same
request in longhand.

Wi-Fi station tuning is **not configurable**. Modem power-saving is always off, channels 12 and 13
are usable, and a dropped link is checked every 5 s and reconnected.

## MQTT and Home Assistant

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `mqttEnabled` | bool | - | `false` | Master switch for the MQTT client. `false` → nothing connects, publishes or subscribes; the stored host/user/password are kept. Setting it `true` requires a non-empty `mqttHost`, else `422 validationFailed`. | yes |
| `mqttHost` | string | - | `""` | Broker host. An ordinary string - blank it freely; whether MQTT runs is decided by `mqttEnabled`, not by this field. A `.local` name is resolved over mDNS and needs a responder answering for it on the same network; an IP address needs no lookup at all. | yes |
| `mqttPort` | uint16 | 1–65535 | `1883` | Broker port. Range-checked - outside 1–65535 is `422 validationFailed`. | yes |
| `mqttUser` | string | - | `""` | Broker username. **Not a secret** - it *is* returned by `GET`. When user and password are both empty, AWTRIX connects anonymously. | yes |
| `mqttPass` | string | - | `""` | Broker password. **Secret**: omitted from `GET`, blank on write keeps the stored value. | yes |
| `mqttPrefix` | string | - | `""` | Root of every topic. Empty → the device uid (lowercased MAC, no colons). Drives `<prefix>/cmd/#`, `<prefix>/availability`, `<prefix>/state/*`. | yes |
| `haDiscovery` | bool | - | `false` | Publish the Home Assistant discovery document on `<haPrefix>/device/<uid>/config`. The MQTT topics are unaffected either way. Applied immediately when the broker is connected; switching it off publishes an empty retained payload to the same topic, removing AWTRIX from Home Assistant. | no |
| `haPrefix` | string | - | `"homeassistant"` | Home Assistant discovery prefix. Names the discovery topic whether `haDiscovery` is on or off - with it off, the empty retraction goes to this prefix too. Empty → `homeassistant`. Changing it retracts the document from the old topic before republishing. | no |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,"mqttPrefix":"awtrix","haDiscovery":true}'
```

To turn MQTT off, flip the gate and reboot for it to take effect - the host, port, prefix and
credentials stay stored, so switching it back on later needs no retyping:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":false}'
```

See [MQTT topics](mqtt.md) and [Home Assistant](../guides/home-assistant.md).

## Time

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `ntpServer` | string | - | `"pool.ntp.org"` | NTP host. Applied immediately - the sync is re-armed against the new server on write. | no |
| `tz` | string | POSIX TZ | `"CET-1CEST,M3.5.0,M10.5.0/3"` | POSIX timezone string (Central Europe with DST). This is the setting AWTRIX runs on. Applied immediately. Free text - **not validated**. An invalid string does not error, it just gives you the wrong time. | no |
| `tzName` | string | IANA zone | `"Europe/Berlin"` | The zone `tz` was picked from, e.g. `America/New_York`. Label only - AWTRIX never reads it; the web UI uses it to show the city back. | no |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"tz":"EST5EDT,M3.2.0,M11.1.0","tzName":"America/New_York","ntpServer":"time.cloudflare.com"}'
```

Daylight saving needs no setting of its own: the changeover dates are the `M` rules inside the
POSIX string.

Writing `tz` alone is fine - the web UI falls back to naming the first zone that runs on the rule
it finds. Writing `tzName` alone changes nothing about the clock. The web UI's timezone picker
writes both together.

## Identity, web server and authentication

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `hostname` | string | - | `""` | Station hostname, provisioning AP SSID, mDNS name and UDP discovery name. Empty → `awtrixng-` plus the last 6 hex characters of the Wi-Fi MAC (e.g. `awtrixng-a1b2c3`) - except as the Home Assistant device name, where empty → `AWTRIX NG`. | yes |
| `webPort` | int | 0–65535 | `80` | HTTP port of the API and web UI. `0` is treated as 80 everywhere - HTTP server, mDNS and UDP discovery alike; anything above 65535 is rejected with `422`. In provisioning (AP) mode the server **always** binds port 80 regardless of this value. Announced over mDNS and appended to the boot IP scroll when not 80. | yes |
| `authEnabled` | bool | - | `false` | Master switch for HTTP Basic auth. `true` → Basic auth on *every* route, realm `AWTRIX NG`, `401` in the standard error body. Setting it `true` requires a non-empty `authUser` **and** `authPass`, else `422 validationFailed`. `false` → the API and UI are reachable without a login; the stored credentials are kept. **Applies live.** | no |
| `authUser` | string | - | `""` | HTTP Basic username. An ordinary string - blank it freely; whether a login is required is decided by `authEnabled`, not by this field. | no |
| `authPass` | string | - | `""` | HTTP Basic password. **Secret**: omitted from `GET`, blank on write keeps the stored value. **Applies live.** | no |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"hostname":"kitchen-clock","authEnabled":true,"authUser":"admin","authPass":"hunter2"}'
```

Auth takes effect on the very next request - including the response to this `PUT`. To turn it
back off, flip the gate. The route sits behind the auth check itself, so this needs the
credentials that are currently set; the stored username and password survive:

```bash
curl -u admin:hunter2 -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"authEnabled":false}'
```

!!! warning "The provisioning access point has no password"
    The provisioning AP is **open**, so you can join it to set AWTRIX up. Authentication is off
    by default, so until you turn it on with `authEnabled` anyone in radio range can read the
    configuration and write Wi-Fi credentials. Provision somewhere you trust and get AWTRIX onto
    your Wi-Fi promptly.

    An enabled login applies in the AP too - there is no bypass - and AWTRIX answers far fewer
    routes there: reads, `PUT /api/v1/system` for Wi-Fi setup and `POST /api/v1/device/reboot`.
    Everything else - file and firmware upload included - returns `403`.

## Sensor calibration

| Key | Type | Range | Default | Units | Effect | Reboot |
|---|---|---|---|---|---|---|
| `tempOffset` | float | ±20 | `-9.0` | °C | Added to the I²C sensor's temperature before it is reported. The default is calibrated for a stock Ulanzi TC001. Applied only when a sensor is present. Outside ±20 → `422`. **Applies live** (2 s sensor tick). | no |
| `humOffset` | float | ±50 | `0.0` | % | Added to the sensor's humidity. Applied only when a sensor is present. Outside ±50 → `422`. **Applies live** (2 s tick). | no |
| `batteryDividerRatio` | float | 0.1–10 | `1.79` | V/V | Cell volts per pin volt of the resistor divider. Outside 0.1–10 → `422`. **Applies live** (2 s tick). | no |
| `lowBatteryThreshold` | uint8 | 0–100 (`0` = off) | `0` | % | Battery percentage **below** which `GET /api/v1/device` reports `lowBattery: true` (and the Home Assistant "Low battery" binary sensor trips). `0` disables the check entirely. **Applies live** (2 s tick). | no |

I²C sensors are **auto-detected** at boot (BME280, BMP280, HTU21DF, SHT31) - only the pins are
configurable, not the sensor type.

Calibrating the battery divider: read `batteryPinMillivolts` from `GET /api/v1/device` with a full
cell, then `ratio = 4.2 / (batteryPinMillivolts / 1000)`. The default 1.79 suits a stock Ulanzi TC001.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"tempOffset":-2.5,"batteryDividerRatio":1.79}'
```

Battery percent is read off the cell voltage against a resting Li-Ion discharge curve, not a linear
map. The curve is in [Power & battery](../guides/power.md).

## Auto-brightness

These six fields shape auto-brightness. They are consulted **only** when `autoBrightness` is
enabled in [Settings](settings.md); otherwise the panel uses the `brightness` setting directly.
All six **apply live**, on the 100 ms light-sensor tick - no reboot.

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `minBrightness` | uint8 | 0–255 | `10` | Lower bound of the auto-brightness output range. | no |
| `maxBrightness` | uint8 | 0–255 | `220` | Upper bound of the auto-brightness output range. | no |
| `ldrFactor` | float | 0–10 | `1.0` | Calibrates the **sensor** - what counts as full light, for DIY boards with a different divider. `0` falls back to 1.0; outside 0–10 → `422`. | no |
| `ldrGamma` | float | 0.1–10 | `2.2` | The response curve. `1.0` = linear (curve off). 2.2 keeps dim rooms dim. Outside 0.1–10 → `422`. | no |
| `ldrOnGround` | bool | - | `false` | Inverted wiring: set when the light sensor is wired to ground. | no |
| `brightnessSmoothing` | long | 0–60000 ms | `10000` | How slowly the panel follows a change in ambient light. `0` follows instantly. Applies to **auto-brightness only** - a manual `brightness` takes effect at once. The reported `lightLevel` is never smoothed. | no |

`lightLevel` is a relative 0–100 measure, **not lux**.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"minBrightness":5,"maxBrightness":180,"ldrGamma":2.2}'
```

`minBrightness` and `maxBrightness` are range-checked individually (0–255), and the *relationship*
is checked as well - on the merged configuration, so a partial write that sets only one of the two
is caught too. Equal values are allowed and give a fixed brightness.

```json
{ "error": { "code": "validationFailed",
             "message": "must not be greater than maxBrightness",
             "field": "minBrightness" } }
```

On a board with no light sensor (`pinLdr: -1`) the reading looks exactly like a pitch-dark room, so
with `autoBrightness` on the panel sits at `minBrightness` and never moves. Turn `autoBrightness`
off and set `brightness` yourself instead. `GET /api/v1/device` omits `lightLevel` and `ldrRaw`
entirely on such a board, so there is no reading to check.

See [Brightness & sensors](../guides/brightness.md).

## Panel and orientation

Your matrix is described as **panels**: how wide one panel is, how many of them the data cable
runs through, where that cable enters, and how the strip is laid out inside a panel. The total
width follows from the first two - `panelWidth × panels` - and the height is always 8 pixels.

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `panelWidth` | int | 1–128 | `32` | Width of one panel in pixels. | yes, if the total width changes |
| `panels` | int | 1–128 | `1` | How many identical panels the strip runs through, left to right. `panelWidth × panels` must come to 32–128, or the write is `422 validationFailed` on `panelWidth`. | yes, if the total width changes |
| `panelStart` | enum | `topLeft` `topRight` `bottomLeft` `bottomRight` | `topLeft` | The corner the first LED sits in. Names are case-insensitive; anything else is `422`. | no |
| `panelWiring` | enum | `rows` `columns` | `rows` | Whether the strip runs along the rows or down the columns inside a panel. | no |
| `panelSerpentine` | bool | - | `true` | Every second row (or column) runs backwards - the zigzag most panels are wired in. `false` = every run starts on the same side. | no |
| `panelChainReverse` | bool | - | `false` | The data cable enters the chain at the other end, without changing how a panel is wired inside. | no |
| `panelChainSerpentine` | bool | - | `false` | Every second panel along the cable is mounted rotated 180°, so one panel's output sits beside the next panel's input. | no |
| `mirror` | bool | - | `false` | Flips the picture left to right. | no |
| `rotate` | bool | - | `false` | Turns the picture 180°. Also swaps the left and right button, which is correct for a physically upside-down panel. | no |

`panelStart`, `panelWiring` and `panelSerpentine` describe how a **panel** is wired;
`panelChainReverse` and `panelChainSerpentine` describe how the **panels are chained**;
`mirror` and `rotate` describe how the picture is **drawn** on the result. They compose - the
display transform is applied first, then the chain order, then the wiring map inside a panel.
Only `rotate` moves the buttons; a `panelStart` of `bottomRight` is a statement about the cable,
not about the image. On a single-panel device the two chain keys cannot change anything.

Wiring is re-applied on the next frame, so you can try a setting and look at the panel. The one
exception is the total width, which is fixed at boot: a change to `panelWidth × panels` needs
`POST /api/v1/device/reboot`.

### The wirings people actually have

| Build | Configuration |
|---|---|
| Most 32×8 panels | the defaults: `panelWidth` 32, `panels` 1, `topLeft`, `rows`, serpentine on |
| Four chained 8×8 tiles | `panelWidth` 8, `panels` 4, serpentine off |
| Four 8×8 tiles, each wired from its right edge | `panelWidth` 8, `panels` 4, `panelStart` `topRight`, `panelChainReverse` true |
| Every second tile mounted upside down | `panelChainSerpentine` true |
| A 32×8 panel wired in columns | `panelWiring` `columns` |
| A 64-pixel-wide panel | `panelWidth` 64 |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system   -H "Content-Type: application/json"   -d '{"panelWidth":8,"panels":4,"panelSerpentine":false}'
```

If the picture comes out scrambled, the wiring keys are what to try: `panelSerpentine` first,
then `panelStart`, then `panelWiring`. If those leave each panel correct but the panels in the
wrong order, or every second panel upside down, reach for `panelChainReverse` and
`panelChainSerpentine`. The web UI's **Panel** section shows the resulting size
(`32 × 8 = 256 LEDs`) while you edit.

## Buttons

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `swapButtons` | bool | - | `false` | Swaps left/right: left → next app, right → previous app. XORs with `rotate` - setting **both** cancels out. The middle (select) button is never swapped. **Applies live.** | no |
| `buttonCallback` | string | URL | `""` | HTTP webhook called on every button **state change** (press *and* release). Empty = disabled. **Applies live.** | no |

`buttonCallback` lets the buttons trigger something in your house - a lamp, a scene, a Node-RED
flow. Set it to the URL of your listener and AWTRIX sends it a `POST` with
`Content-Type: application/x-www-form-urlencoded` and this body:

```
button=<left|middle|right>&state=<1|0>&uid=<mac>
```

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"buttonCallback":"http://192.168.1.20:1880/awtrix-button"}'
```

What to expect from it:

* **One press is two calls** - `state=1` when the button goes down, `state=0` when it is released.
  Act on `state=1` and ignore the other, or measure the gap to detect a hold.
* **The buttons keep their normal job.** The webhook runs alongside app switching; use
  [`blockNavigation`](settings.md#buttons) if left/right should only drive your automation, or a
  script that returns `true` from `on_button()` to take single presses on its own screen.
* **`middle`, not `select`** - MQTT and scripts use `select` for the same button. The names follow
  the wiring: `swapButtons` and `rotate` do not rename them.
* **Plain `http://` only**, no TLS and no auth header, so keep the listener on your own network. An
  `https://` URL sends nothing at all.
* **Fire and forget** - the response is ignored, redirects are not followed and nothing is retried.
  Answer immediately: timeouts are 300 ms each and the request runs on the display task, so a dead
  host costs up to about 0.6 s of stutter per edge.
* `uid` is this panel's MAC, so several panels can share one endpoint.

With a broker running you may not need it at all - [`state/buttons/<button>`](mqtt.md#state-topics)
carries the same edges, retained, and Home Assistant discovers them on its own.

The debounce time and the double-press window are fixed and cannot be configured.

## Sound hardware

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `dfplayer` | bool | - | `false` | Talk to a DFPlayer Mini (AWTRIX 2 mainboard conversions) when `dfplayer` is true **and** both DF pins are `>= 0`. The buzzer at `pinBuzzer` keeps working alongside it - the two are separate outputs with separate volumes. The DF pins themselves are validated whenever they're set, regardless of this flag. | yes |

The flag is what keeps two GPIOs free on every board that has no module: the ESP32 defaults name
`pinDfRx` and `pinDfTx` whether or not one is attached.

See [Sound](../guides/sounds.md).

## Art-Net

An optional network listener, **off by default**. With the flag off no extra port is open.

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `artnet` | bool | - | `false` | Enables the Art-Net receiver on UDP 6454, which drives the panel straight from incoming DMX frames. There is no authentication. | no |

The universe and pixel mapping, the five-second hold window and the security implications are in
[Art-Net](../guides/artnet.md).

## Miscellaneous

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `statsInterval` | long | 1000–600000 ms | `10000` | How often device stats are published over MQTT - sets the rate for both the plain-MQTT and the Home Assistant service. Outside 1 s–10 min → `422`. | yes |
| `tempDecimals` | uint8 | 0–2 | `0` | Number of decimal places the Temperature app shows. Outside 0–2 → `422`. **Applies live.** | no |
| `debugMode` | bool | - | `false` | Turns on verbose request/command tracing (`logdbg`) to the serial port and the log console. Off keeps the log quiet. **Applies live.** | no |
| `scriptingEnabled` | bool | - | `true` | Master switch for [Berry scripting](../guides/scripting.md). Off frees the memory the interpreter occupies - watch `freeHeapBytes` in `GET /api/v1/device` to see how much on your board - and no script runs. Stored scripts are not deleted, and they stay **fully editable**: listing, reading, saving and deleting a script all work with this off, so a script that made the device unreachable can be repaired. Only execution stops; saved changes take effect on the next boot once it is switched back on. Holding **left+right** for three seconds while switching the device on sets this to `false` from the panel, for when a script has made it unreachable ([troubleshooting](../troubleshooting/troubleshooting.md#scripts-eat-the-memory-and-awtrix-never-comes-up)). | yes |

Install, read and remove scripts with [`/api/v1/apps/script/{name}`](http.md#scripts); the language
itself is in [Scripting](../guides/scripting.md).

Colour control is not on this route: `colorCorrection` and `colorTint` live on
[Settings](settings.md).

## GPIO map

`PUT /api/v1/system` also carries the sixteen `int` GPIO fields (`pinMatrix`, `pinBtnLeft`,
`pinBtnSelect`, `pinBtnRight`, `pinBattery`, `pinLdr`, `pinBuzzer`, `pinI2cSda`, `pinI2cScl`,
`pinDfRx`, `pinDfTx`, `pinI2sBclk`, `pinI2sLrclk`, `pinI2sDout`, `pinI2sMclk`, `pinAmpEnable`).
`-1` disables a feature (except
`pinMatrix`, which cannot be disabled), and **every change requires a reboot**.

The map is validated against the fully merged configuration, so a partial `PUT` is checked against
the stored rest. A rejection answers `400 invalidPinConfig` and names the offending field before
anything is persisted. An invalid map found at boot falls back to the compiled defaults.

The per-chip field table, defaults, the list of pins the matrix driver accepts, the validation
rules in order with their exact error strings, and the AWTRIX 2 SDA-collision fix are all written
out once in **[GPIO & boards](gpio.md)** - that page is the one to read on GPIO, for both the
ESP32 and the ESP32-S3.

## Wi-Fi scan

`GET /api/v1/system/wifi-scan` - an asynchronous scan, used by the provisioning UI.

```bash
curl http://<awtrix-ip>/api/v1/system/wifi-scan
```

The first call starts a scan and returns `202`; poll until you get `200`.

| Status | Body |
|---|---|
| `202` | `{"scanning":true}` - scan starting or still running |
| `200` | array of results |

```json
[
  { "ssid": "MyNetwork", "rssi": -52, "enc": true },
  { "ssid": "GuestWiFi", "rssi": -78, "enc": false }
]
```

`enc` is a **boolean** (encrypted or open), not an auth-mode number. Results are discarded
after being served, so the next request starts a fresh scan.

## Persistence and resets

Every `PUT` is persisted at once; nothing is batched or deferred. Each field is stored on its own,
and a field that has never been written keeps its built-in default, so a firmware upgrade that adds
a field does not disturb existing configuration.

| Route | Clears | Keeps |
|---|---|---|
| `POST /api/v1/settings/reset` | the stored settings only | **all system configuration** - Wi-Fi, MQTT, GPIO, calibration |
| `POST /api/v1/device/factory-reset` | Settings, **system configuration**, the whole filesystem (icons, melodies, palettes, scripts), and stored Wi-Fi credentials | nothing |

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/factory-reset
```

!!! danger "Factory reset is unrecoverable and drops AWTRIX off your network"
    It erases stored Wi-Fi credentials and clears `wifiSsid`, so AWTRIX necessarily comes back up
    in provisioning AP mode. Everything on the filesystem is formatted away. There is no
    confirmation step and no undo.

Factory reset is available over HTTP only. Reboot, sleep and a settings reset can also be sent
over [MQTT](mqtt.md).

The panel wiring is system configuration, so a *settings* reset leaves it alone; only a factory
reset returns it to the defaults.

### Coming from an AWTRIX 3 device

Nothing is imported. Set AWTRIX up through the web UI or `PUT /api/v1/system` as you would a new
one.

Battery calibration in particular does not carry over: AWTRIX 3's `min_battery`/`max_battery` are
raw-ADC limits, and battery percent here is derived from cell voltage, calibrated with
`batteryDividerRatio` - see [Sensor calibration](#sensor-calibration).
