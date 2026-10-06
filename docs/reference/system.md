# System configuration

<!-- only esp32 esp32-s3 -->
`GET`/`PUT /api/v1/system` holds the **system configuration** of AWTRIX: Wi-Fi, MQTT and Home
Assistant, time server and time zone, device name, web port and login, sensor and battery
calibration, the auto-brightness range, the panel layout and the pin map. This page describes every
field.
<!-- /only -->
<!-- only tc002 -->
`GET`/`PUT /api/v1/system` holds the **system configuration** of AWTRIX: Wi-Fi, MQTT and Home
Assistant, time server and time zone, device name and login, the battery warning, the buttons and
mirroring. This page describes every field.
<!-- /only -->

It is one flat JSON object without nesting.

[Settings](settings.md) are something different: they control how the display *behaves* –
brightness, apps, transitions – and are written with `PATCH /api/v1/settings`.<!-- only esp32 esp32-s3 --> How your clock is
built and wired belongs here.<!-- /only -->

Most system fields take effect only **after a restart**:
`curl -X POST http://<awtrix-ip>/api/v1/device/reboot`. The "Reboot" column in each table shows
which fields apply at once.

## Read the whole configuration

```bash
curl http://<awtrix-ip>/api/v1/system
```

Returns `200` with the full configuration as a flat JSON object, using the field names on this
page:

<!-- only esp32 -->
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
<!-- /only -->
<!-- only esp32-s3 -->
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
  "pinMatrix": 21
}
```
<!-- /only -->
<!-- only tc002 -->
```json
{
  "wifiSsid": "MyNetwork",
  "netStatic": false,
  "mqttHost": "192.168.1.10",
  "mqttPort": 1883,
  "hostname": "kitchen-clock",
  "lowBatteryThreshold": 15
}
```
<!-- /only -->

`wifiPass`, `mqttPass` and `authPass` are **secrets** and are left out of a normal `GET`. Add
`?secrets=1` – `GET /api/v1/system?secrets=1` – to get them too. The web UI's backup export uses
this so that you can restore the backup later. In setup mode any `secrets` query parameter is refused with `403 forbidden`, because
anyone nearby can join the open setup hotspot. A `PUT` answer never contains the secrets.

## Write a configuration

`PUT` takes **only the keys you want to change**. Keys you leave out keep their stored value.
Unknown keys are ignored without an error.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttHost":"192.168.1.10","mqttPort":1883,"mqttUser":"awtrix"}'
```

Returns `200` with the new configuration (same as `GET`, without secrets). The change is saved at
once.

A rejected write changes nothing. All answers and error messages of this route are in
[Errors – PUT /api/v1/system](errors.md#put-apiv1system)<!-- only esp32 esp32-s3 --> and
[Errors – GPIO validation](errors.md#gpio-validation-invalidpinconfig)<!-- /only -->.

Always send `Content-Type: application/json`. Without it, `curl -d` marks the body as a form, and
AWTRIX refuses the `PUT` with `415` ([Content-Type](conventions.md#content-type-is-mandatory)).

### What `PUT` checks

AWTRIX checks the type and range of every number **before it stores anything**. A wrong value is
rejected with `422 validationFailed`, and `field` names the key – the same as with
`PATCH /api/v1/settings`.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"webPort":70000}'
```

```json
{ "error": { "code": "validationFailed", "message": "out of range", "field": "webPort" } }
```

The allowed range of each field is in the "Range" column of its table.
<!-- only esp32 -->
Every `pin*` field takes `-1` (not connected) or a GPIO of the chip, 0–39. See
[GPIO & boards](gpio.md).
<!-- /only -->
<!-- only esp32-s3 -->
Every `pin*` field takes `-1` (not connected) or a GPIO of the chip, 0–48. GPIO 22–25 do not
exist. See [GPIO & boards](gpio.md).
<!-- /only -->

Whole-number fields also reject other values (`{"mqttPort":"eighty"}` and `{"tempDecimals":1.5}`
both fail with `422`). Decimal fields accept any number in range.

### What `PUT` does not check

<!-- only esp32 esp32-s3 -->
Apart from the number ranges, the three name fields (`panelStart`, `panelWiring`,
`panelColorOrder`), the IP address fields and the rules in the next section, text fields are not
checked. Unknown keys are ignored. The **pin rules** (no duplicates, <!-- only esp32 -->input-only pins<!-- /only --><!-- only esp32-s3 -->reserved pins<!-- /only -->, the LED
matrix list) are checked separately and answer `400 invalidPinConfig`; the rules and messages are
in [GPIO & boards](gpio.md).
<!-- /only -->
<!-- only tc002 -->
Apart from the number ranges, the IP address fields, `mqttTlsPin` and the rules in the next
section, text fields are not checked. Unknown keys are ignored.
<!-- /only -->

### Switches, blank fields and secrets

MQTT and the login each have an on/off switch: `mqttEnabled` for MQTT, `authEnabled` for the login.
The feature runs only while its switch is `true`. Switching it off keeps host, user name and
password, so you do not need to type them again later. Because the switch decides, you can leave
`mqttHost` and `authUser` empty whenever you like.

You can switch a feature on only when it has what it needs. Otherwise you get `422 validationFailed`:

| Set | Requires | Field named on `422` |
|---|---|---|
| `mqttEnabled: true` | a non-empty `mqttHost` | `mqttHost` |
| `authEnabled: true` | a non-empty `authUser` **and** `authPass` | `authUser` |

`wifiSsid` cannot be emptied with `PUT` (`422`, `field: wifiSsid`), because an empty network name
sends AWTRIX into setup mode. The error points you to `POST /api/v1/device/factory-reset`.

An empty value for one of the three secrets is ignored and the stored secret stays. So a `PUT` can
set or change a password, but never delete one. To stop using a feature, switch it off. To delete
all stored secrets, use `POST /api/v1/device/factory-reset`.

The API does not tell you when a restart is needed. The web UI shows its "reboot required" note
after every save, even for fields that apply at once. Use the "Reboot" column in the tables below.

<!-- only tc002 -->
## Keys without effect {#device-differences}

`GET` also returns these keys. You can write them, and they are checked and stored, but they
change nothing on this clock:

- `webPort`: the web server always uses port 80.
- `tempOffset`, `humOffset` and `batteryDividerRatio`: the clock has no temperature sensor and
  measures its battery itself.
- `minBrightness`, `maxBrightness`, `ldrFactor`, `ldrGamma`, `ldrOnGround` and
  `brightnessSmoothing`: see [Auto-brightness](#auto-brightness).
- `dfplayer`, `artnet` and `tempDecimals`.

There are no panel or pin keys. A `PUT` or a restored backup ignores such keys like any unknown
key. See [Panel](#panel-and-orientation).
<!-- /only -->

## Wi-Fi

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `wifiSsid` | string | - | `""` | Network name to join. Cannot be emptied with `PUT` (see above). Not checked for format. | yes |
| `wifiPass` | string | - | `""` | Wi-Fi password. **Secret**: never returned; an empty value on write keeps the stored one. | yes |
| `netStatic` | bool | - | `false` | `false` = get the address from the router (DHCP). A fixed address is used only when `netStatic` is `true` **and** `ip` is set. | <!-- only esp32 esp32-s3 -->yes<!-- /only --><!-- only tc002 -->no<!-- /only --> |
| `ip` | string | dotted quad, optional `/0`–`/32` suffix | `""` | Fixed IP address. You can add the mask as a suffix: `192.168.1.50/24` is stored as `ip` + `subnet` (sending a `subnet` in the same request as well is a `422`). With `netStatic: true` but an empty `ip`, AWTRIX stays on DHCP. Invalid values are rejected with `422`; `""` leaves it unset. | <!-- only esp32 esp32-s3 -->yes<!-- /only --><!-- only tc002 -->no<!-- /only --> |
| `gateway` | string | dotted quad | `""` | Gateway (router) address. Also used as DNS server when `dns1` is empty. | <!-- only esp32 esp32-s3 -->yes<!-- /only --><!-- only tc002 -->no<!-- /only --> |
| `subnet` | string | dotted quad | `""` | Subnet mask. `GET` always shows the mask here, even if you set it with the `/24` suffix. Required when `netStatic` is `true` and `ip` is set – otherwise the write is rejected with `422`. | <!-- only esp32 esp32-s3 -->yes<!-- /only --><!-- only tc002 -->no<!-- /only --> |
| `dns1` | string | dotted quad | `""` | First DNS server. Empty → the gateway. | <!-- only esp32 esp32-s3 -->yes<!-- /only --><!-- only tc002 -->no<!-- /only --> |
| `dns2` | string | dotted quad | `""` | Second DNS server. Empty → none. | <!-- only esp32 esp32-s3 -->yes<!-- /only --><!-- only tc002 -->no<!-- /only --> |
| `wifiConnectTimeout` | long | 5000–120000 ms | `15000` | How long AWTRIX tries to join at startup before it opens its setup hotspot. Raise it for a network that connects slowly. While the setup hotspot is open, AWTRIX tries the stored network every <!-- only esp32 esp32-s3 -->30 s. It restarts once it gets in.<!-- /only --><!-- only tc002 -->60 s.<!-- /only --> | yes |
| `wifiRoamRssi` | int | −90–0 dBm | `0` | Switch to a stronger access point when the signal is weaker than this. `0` = off. The signal must stay below the value for about 30 s, and AWTRIX switches at most once every 5 minutes. **Switching means a short disconnect** of a second or two, and MQTT reconnects too. | yes |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"wifiSsid":"MyNetwork","wifiPass":"secret123"}'
```

A fixed address needs `netStatic` and `ip` together:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"netStatic":true,"ip":"192.168.1.50/24","gateway":"192.168.1.1"}'
```

`/24` is the mask; `"ip":"192.168.1.50","subnet":"255.255.255.0"` means the same.

Other Wi-Fi details cannot be changed: power saving is always off, channels 12 and 13 work, and
AWTRIX checks the connection every 5 s and reconnects if it dropped.

## MQTT and Home Assistant

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `mqttEnabled` | bool | - | `false` | Main switch for MQTT. `false` → no connection; host, user and password stay stored. `true` needs a `mqttHost`, otherwise `422 validationFailed`. | yes |
| `mqttHost` | string | - | `""` | Broker host name or IP address. You may leave it empty; `mqttEnabled` decides whether MQTT runs. A `.local` name only works if something on your network answers for it (mDNS); an IP address always works. | yes |
| `mqttPort` | uint16 | 1–65535 | `1883` | Broker port. Outside 1–65535 → `422 validationFailed`. | yes |
| `mqttUser` | string | - | `""` | Broker user name. **Not a secret** – `GET` returns it. If user and password are both empty, AWTRIX connects without login. | yes |
| `mqttPass` | string | - | `""` | Broker password. **Secret**: left out of `GET`; an empty value on write keeps the stored one. | yes |
<!-- only tc002 -->
| `mqttTls` | bool | - | `false` | Connect over TLS, usually on port `8883`. AWTRIX accepts only a broker whose certificate comes from a public certificate authority and names `mqttHost`, or whose certificate you trusted with `mqttTlsPin`. An uploaded broker CA replaces both; see [MQTT → Connect over TLS](../guides/mqtt.md#connect-over-tls-tc002). | yes |
| `mqttTlsPin` | string | 64 hex digits | `""` | SHA-256 of the broker certificate you trust, in lowercase hex, as `GET /api/v1/mqtt/tls` shows it. `""` trusts none. Anything else → `422` `expected 64 lowercase hex digits`. Applies at the next connection attempt. | no |
<!-- /only -->
| `mqttPrefix` | string | - | `""` | Start of every topic. Empty → the device uid (MAC address in lowercase, without colons). Used for `<prefix>/cmd/#`, `<prefix>/availability` and `<prefix>/state/*`. | yes |
| `haDiscovery` | bool | - | `false` | Announce AWTRIX to Home Assistant on `<haPrefix>/device/<uid>/config`. The MQTT topics stay the same either way. Applies at once while connected. Switching it off removes AWTRIX from Home Assistant. | no |
| `haPrefix` | string | - | `"homeassistant"` | Home Assistant discovery prefix. Empty → `homeassistant`. When you change it, AWTRIX removes the announcement from the old topic and publishes it on the new one. | no |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,"mqttPrefix":"awtrix","haDiscovery":true}'
```

To turn MQTT off, set the switch and restart. Host, port, prefix and login stay stored, so you can
switch it on again later without typing them again:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":false}'
```

See [MQTT topics](mqtt.md) and [Home Assistant](../guides/home-assistant.md).

## Time

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `ntpServer` | string | - | `"pool.ntp.org"` | Time server (NTP – gets the exact time from the internet). Applies at once. | no |
| `tz` | string | POSIX TZ | `"CET-1CEST,M3.5.0,M10.5.0/3"` | Time zone rule in POSIX format (default: Central Europe with daylight saving time). This is the value AWTRIX uses. Applies at once. **Not checked** – a wrong string gives no error, just the wrong time. | no |
| `tzName` | string | IANA zone | `"Europe/Berlin"` | The name of the zone, for example `America/New_York`. Only a label for the web UI; it does not change the time. | no |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"tz":"EST5EDT,M3.2.0,M11.1.0","tzName":"America/New_York","ntpServer":"time.cloudflare.com"}'
```

Daylight saving time needs no extra setting: the change dates are the `M` rules in the POSIX
string.

You can write `tz` alone – the web UI then shows the first zone that uses this rule. Writing
`tzName` alone does not change the time. The web UI's time zone picker writes both.

## Identity, web server and authentication

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `hostname` | string | - | `""` | The device name: network name, name of the setup hotspot, mDNS name (`http://<hostname>.local`) and name for discovery tools. Empty → `awtrixng-` plus the last 6 characters of the Wi-Fi MAC address (for example `awtrixng-a1b2c3`). In Home Assistant an empty name shows as `AWTRIX NG`. | yes |
<!-- only esp32 esp32-s3 -->
| `webPort` | int | 0–65535 | `80` | Port of the web UI and API. `0` means 80. Above 65535 is rejected with `422`. In setup mode the port is **always** 80. If it is not 80, the startup screen shows the port with the address. | yes |
<!-- /only -->
| `authEnabled` | bool | - | `false` | Main switch for the login (HTTP Basic auth). `true` → every request needs user name and password (realm `AWTRIX NG`); without them the answer is `401`. `true` needs `authUser` **and** `authPass`, otherwise `422 validationFailed`. `false` → no login needed; the stored login stays. **Applies at once.** | no |
| `authUser` | string | - | `""` | Login user name. You may leave it empty; `authEnabled` decides whether a login is needed. | no |
| `authPass` | string | - | `""` | Login password. **Secret**: left out of `GET`; an empty value on write keeps the stored one. **Applies at once.** | no |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"hostname":"kitchen-clock","authEnabled":true,"authUser":"admin","authPass":"hunter2"}'
```

The login applies from the next request on – including the answer to this `PUT`. To switch it off
again, send the current login with the request. User name and password stay stored:

```bash
curl -u admin:hunter2 -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"authEnabled":false}'
```

!!! warning "The setup hotspot has no password"
    The setup hotspot is **open**, so you can join it to set AWTRIX up. The login is off by
    default, so until you turn on `authEnabled`, anyone nearby can read the configuration and enter
    Wi-Fi details. Set AWTRIX up somewhere you trust and connect it to your Wi-Fi soon.

    A login you switched on also applies in setup mode. In setup mode AWTRIX answers only a few
    requests: the setup page, device/setup status, `PUT /api/v1/system` with only `wifiSsid`,
    `wifiPass` and `hostname`, `POST /api/v1/device/reboot` and restoring a backup
    (`POST /api/v1/restore`). Logs, scripts, file lists, secrets export and other uploads answer
    `403`. See the
    [setup allow-list](errors.md#provisioning-lockdown-403).

<!-- only esp32 esp32-s3 -->
## Sensor calibration

| Key | Type | Range | Default | Units | Effect | Reboot |
|---|---|---|---|---|---|---|
| `tempOffset` | float | ±20 | `-9.0` | °C | Added to the measured temperature.<!-- only esp32 --> The default suits a stock Ulanzi TC001.<!-- /only --> Used only when a sensor is present. Outside ±20 → `422`. **Applies within 2 s.** | no |
| `humOffset` | float | ±50 | `0.0` | % | Added to the measured humidity. Used only when a sensor is present. Outside ±50 → `422`. **Applies within 2 s.** | no |
| `batteryDividerRatio` | float | 0.1–10 | `1.79` | V/V | Battery volts per volt at the pin, set by your resistor divider. Outside 0.1–10 → `422`. **Applies within 2 s.** | no |
| `lowBatteryThreshold` | uint8 | 0–100 (`0` = off) | `0` | % | Below this battery percentage, `GET /api/v1/device` reports `lowBattery: true` (and the Home Assistant "Low battery" sensor turns on). `0` switches the check off. **Applies within 2 s.** | no |

AWTRIX finds the temperature sensor by itself at startup (BME280, BMP280, HTU21DF, SHT31). You only
set the pins, not the sensor type.

To calibrate the battery divider, read `batteryPinMillivolts` from `GET /api/v1/device` with a
fully charged battery. Then set `ratio = 4.2 / (batteryPinMillivolts / 1000)`.<!-- only esp32 --> The default 1.79
suits a stock Ulanzi TC001.<!-- /only -->

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"tempOffset":-2.5,"batteryDividerRatio":1.79}'
```

The battery percentage is estimated from the battery voltage using a typical Li-Ion curve, not a
straight line. The curve is in [Power & battery](../guides/power.md).
<!-- /only -->
<!-- only tc002 -->
## Battery warning {#sensor-calibration}

| Key | Type | Range | Default | Units | Effect | Reboot |
|---|---|---|---|---|---|---|
| `lowBatteryThreshold` | uint8 | 0–100 (`0` = off) | `0` | % | Below this battery percentage, `GET /api/v1/device` reports `lowBattery: true` (and the Home Assistant "Low battery" sensor turns on). `0` switches the check off. | no |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"lowBatteryThreshold":15}'
```

The clock measures its battery by itself. See [Power & battery](../guides/power.md).
<!-- /only -->

<!-- only esp32 esp32-s3 -->
## Auto-brightness

These six fields control auto-brightness. They are used **only** when `autoBrightness` is on in
[Settings](settings.md) and the device has a light sensor. Otherwise the display uses the
`brightness` setting. All six **apply at once** – no restart.

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `minBrightness` | uint8 | 0–255 | `10` | Lowest brightness auto-brightness uses. | no |
| `maxBrightness` | uint8 | 0–255 | `220` | Highest brightness auto-brightness uses. | no |
| `ldrFactor` | float | 0–10 | `1.0` | Calibrates the **sensor** – what counts as full light. For DIY boards with a different divider. `0` means 1.0; outside 0–10 → `422`. | no |
| `ldrGamma` | float | 0.1–10 | `2.2` | How brightness follows the light. `1.0` = in a straight line. 2.2 keeps the display dim in dim rooms. Outside 0.1–10 → `422`. | no |
| `ldrOnGround` | bool | - | `false` | Set this when the light sensor is wired to ground instead of 3.3 V. | no |
| `brightnessSmoothing` | long | 0–60000 ms | `10000` | How slowly the display follows a change in room light. `0` = at once. Only for **auto-brightness** – a manual `brightness` applies at once. The reported `lightLevel` is never smoothed. | no |

`lightLevel` is a relative value from 0 to 100, **not lux**.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"minBrightness":5,"maxBrightness":180,"ldrGamma":2.2}'
```

`minBrightness` must not be greater than `maxBrightness`. This is checked against the stored value
too, so it also catches a write that sets only one of them. Equal values are allowed and give a
fixed brightness.

```json
{ "error": { "code": "validationFailed",
             "message": "must be <= maxBrightness",
             "field": "minBrightness" } }
```

Without a light sensor (`pinLdr: -1`) these six fields have no effect. `autoBrightness` is accepted, but the display uses `brightness`. `GET /api/v1/device` leaves
out `lightLevel` and `ldrRaw`, and the web UI hides these fields.

See [Brightness & sensors](../guides/brightness.md).
<!-- /only -->
<!-- only tc002 -->
## Auto-brightness

The clock has no light sensor. `autoBrightness` is accepted, but the display always uses the
`brightness` setting. `minBrightness`, `maxBrightness`, `ldrFactor`, `ldrGamma`, `ldrOnGround` and
`brightnessSmoothing` are checked and stored, but change nothing. `minBrightness` must still not be
greater than `maxBrightness`.
<!-- /only -->

<!-- only esp32 esp32-s3 -->
## Panel and orientation

Your LED matrix is described as **panels**: how wide one panel is, how many panels the data cable
runs through, where the cable enters, and how the LED strip runs inside a panel. The total width is
`panelWidth × panels`. Every panel is 8 pixels high. All panels form one row, left to right, on one
data pin.

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `panelWidth` | int | 1–128 | `32` | Width of one panel in pixels. | yes, if the total width changes |
| `panels` | int | 1–128 | `1` | How many identical panels the strip runs through, left to right. `panelWidth × panels` must be 32–128, otherwise the write is `422 validationFailed` on `panelWidth`. | yes, if the total width changes |
| `panelStart` | enum | `topLeft` `topRight` `bottomLeft` `bottomRight` | `topLeft` | The corner of the first LED. Upper and lower case do not matter; anything else is `422`. | no |
| `panelWiring` | enum | `rows` `columns` | `rows` | Whether the strip runs along the rows or down the columns inside a panel. | no |
| `panelColorOrder` | enum | `rgb` `rbg` `grb` `gbr` `brg` `bgr` | `grb` | The color order your LEDs expect. Use `rgb` when red and green are swapped. | no |
| `panelSerpentine` | bool | - | `true` | Every second row (or column) runs back the other way – the zigzag most panels use. `false` = every row starts on the same side. | no |
| `panelChainReverse` | bool | - | `false` | The data cable enters the row of panels at the other end. Does not change the wiring inside a panel. | no |
| `panelChainSerpentine` | bool | - | `false` | Every second panel is turned by 180°, so one panel's output sits next to the next panel's input. | no |
| `mirror` | bool | - | `false` | Mirrors the picture left to right. | no |
| `rotate` | bool | - | `false` | Turns the picture by 180°. Also swaps the left and right button, which is right for a panel mounted upside down. | no |

The keys fall into three groups:

- `panelStart`, `panelWiring`, `panelColorOrder` and `panelSerpentine` describe the wiring
  **inside a panel**.
- `panelChainReverse` and `panelChainSerpentine` describe how the **panels are connected**. With a
  single panel they change nothing.
- `mirror` and `rotate` change how the **picture** is shown. Only `rotate` swaps the buttons;
  `panelStart: bottomRight` only describes the cable, not the picture.

Wiring changes show on the next frame. A new total width needs a restart
(`POST /api/v1/device/reboot`).

The panel is 8 pixels high and 32–128 pixels wide, so at most 1024 LEDs. If AWTRIX
cannot use the stored size at startup, it shows 32×8 instead and keeps your stored values. If even
that fails, the panel stays dark, but the web UI and API still work.

`GET /api/v1/capabilities` reports the size in use (`display.width` / `height`), the saved size
(`requestedWidth` / `requestedHeight`), `restartRequired`, `ready`, and the limits `minWidth`,
`maxWidth`, `minHeight`, `maxHeight` and `maxPixels`.

Wider panels update a little less often. Sending the data to an LED chain takes about 30 µs per
pixel: about 8 ms for 256 pixels and about 31 ms for 1024. The capability `estimatedWireTimeUs`
gives this estimate for your panel (it does not include drawing time).

### The wirings people actually have

| Build | Configuration |
|---|---|
| Most 32×8 panels | the defaults: `panelWidth` 32, `panels` 1, `topLeft`, `rows`, serpentine on |
| Four chained 8×8 tiles | `panelWidth` 8, `panels` 4, serpentine off |
| Four 8×8 tiles, each wired from its right edge | `panelWidth` 8, `panels` 4, `panelStart` `topRight`, `panelChainReverse` true |
| Every second tile mounted upside down | `panelChainSerpentine` true |
| A 32×8 panel wired in columns | `panelWiring` `columns` |
| A panel where red and green are swapped | `panelColorOrder` `rgb` |
| A 64-pixel-wide panel | `panelWidth` 64 |
| Four chained 32×8 panels | `panelWidth` 32, `panels` 4 |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system   -H "Content-Type: application/json"   -d '{"panelWidth":8,"panels":4,"panelSerpentine":false}'
```

If the picture is scrambled, try `panelSerpentine` first, then `panelStart`, then `panelWiring`. If
each panel then looks right but the panels are in the wrong order, or every second panel is upside
down, try `panelChainReverse` and `panelChainSerpentine`. The web UI's **Panel** section shows the
resulting size (`32 × 8 = 256 LEDs`) while you edit.

If the picture is right but some colors are wrong, change `panelColorOrder`. Red and green swapped
usually means an RGB panel with the default GRB order.
<!-- /only -->
<!-- only tc002 -->
## Panel {#panel-and-orientation}

The panel is 52 × 16 pixels and cannot be changed. There are no panel or pin keys:
`GET /api/v1/capabilities` reports the size with `configurable: false`.
<!-- /only -->

## Buttons

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `swapButtons` | bool | - | `false` | Swaps left and right: left → next app, right → previous app.<!-- only esp32 esp32-s3 --> Combined with `rotate`, the two cancel each other out.<!-- /only --> The middle (select) button is never swapped. **Applies at once.** | no |
| `buttonCallback` | string | URL | `""` | Web address AWTRIX calls on every press **and** release<!-- only tc002 -->, and when the knob turns<!-- /only -->. Empty = off. **Applies at once.** | no |

With `buttonCallback` the buttons can control something in your home – a lamp, a scene, a Node-RED
flow. Set it to the URL of your listener. AWTRIX sends a `POST` with
`Content-Type: application/json` and this body:

```json
{"button":"left","state":true,"uid":"dcda0c29dcb8"}
```

`button` is `left`, `middle` or `right`. `state` is `true` when the button goes down and `false`
when it is released. `uid` is the device ID from `GET /api/v1/device`.

<!-- only tc002 -->
The [knob](../guides/device-controls.md#the-knob) reports too. Pushing it sends
`"button":"knob"` with `state`, like a button. Turning it sends `turn` instead of `state`: the
number of clicks, positive clockwise, negative counterclockwise.

```json
{"button":"knob","turn":2,"uid":"dcda0c29dcb8"}
```

Clicks that come faster than your listener answers are added up into one call, so a quick turn
sends one call with a larger `turn`, not one per click.
<!-- /only -->

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"buttonCallback":"http://192.168.1.20:1880/awtrix-button"}'
```

Good to know:

* **One press means two calls** – `"state":true` when pressed, `"state":false` when released. React
  to `true` and ignore the other, or measure the time between them to detect a long press.
* **The buttons keep working normally.** They still switch apps<!-- only tc002 -->, and the knob still sets
  brightness and volume<!-- /only -->. Use [`blockNavigation`](settings.md#buttons) if the buttons<!-- only tc002 --> and the knob<!-- /only -->
  should only control your automation, or a script that returns `true` from `on_button()` to
  handle presses on its own screen.
* **`middle`, not `select`** – MQTT and scripts call the same button `select`. The names follow the
  wiring: `swapButtons`<!-- only esp32 esp32-s3 --> and `rotate` do<!-- /only --><!-- only tc002 --> does<!-- /only --> not rename them.
* **Only `http://`**, no `https://` and no login, so keep the listener in your home network. An
  `https://` URL sends nothing.
* **No answer needed** – AWTRIX ignores the answer, does not follow redirects and does not retry.
<!-- only esp32 esp32-s3 -->
* **Answer quickly.** AWTRIX waits at most 300 ms to connect and 300 ms for the answer, and the
  display pauses while it waits. A listener that does not answer can freeze the display for up to
  about 0.6 s per press or release.
<!-- /only -->
<!-- only tc002 -->
* **Answer quickly.** AWTRIX calls in the background, so the display never waits. It gives up
  after 1 s to connect and 2 s for the answer. While your listener is slow, AWTRIX keeps up to 16
  calls waiting and drops further presses.
<!-- /only -->
* `uid` is the clock's MAC address, so several clocks can share one listener.

With MQTT you may not need this at all – [`state/buttons/<button>`](mqtt.md#state-topics) reports
the same presses, and Home Assistant finds them by itself.<!-- only tc002 -->
[`event/knob`](mqtt.md#eventknob) reports the knob turns.<!-- /only -->

The debounce time and the double-press time are fixed and cannot be changed.

<!-- only esp32 esp32-s3 -->
## Sound hardware

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `dfplayer` | bool | - | `false` | Use a DFPlayer Mini (for example on an AWTRIX 2 mainboard) when `dfplayer` is `true` **and** both DF pins are set. The buzzer at `pinBuzzer` keeps working next to it. Both play at the volume of the sound's group, see [Volume](../guides/sounds.md#volume). The DF pins are always checked when set, whatever this switch says. | yes |

The default pins set `pinDfRx` and `pinDfTx` even on boards without a DFPlayer. With
`dfplayer: false` these two pins stay unused.

See [Sound](../guides/sounds.md).
<!-- /only -->

## Mirroring

Shows the display of one clock on other clocks with a panel of the same size. The port for it,
UDP 4212, is open only while `mirrorShare` is on or `mirrorFrom` names a clock. All six keys apply
at once.

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `mirrorShare` | bool | - | `false` | Lets other clocks show this display. Any device on the network can watch; there is no login. | no |
| `mirrorShareApps` | string | app names, comma separated | `"*"` | Which apps are shared. `*` = all, `""` = none. Case does not matter. | no |
| `mirrorShareNotifications` | bool | - | `true` | Whether notifications are shared. | no |
| `mirrorFrom` | string | IP address or host name | `""` | The clock whose display this clock shows. `""` = mirror nothing. | no |
| `mirrorFromApps` | string | app names, comma separated | `"*"` | Which apps of that clock are shown. `*` = all, `""` = none. | no |
| `mirrorFromNotifications` | bool | - | `true` | Whether that clock's notifications are shown. | no |

What a mirroring clock shows, and the state in `GET /api/v1/device`, are described in
[Mirroring](../guides/mirroring.md).

<!-- only esp32 esp32-s3 -->
## Art-Net

Art-Net lets lighting software send pictures to the panel over the network. It is **off by
default**; while it is off, no extra port is open.

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `artnet` | bool | - | `false` | Turns on the Art-Net receiver on UDP port 6454. Incoming DMX frames are shown on the panel directly. There is no login. | no |

Universe and pixel mapping, the five-second hold time and security notes are in
[Art-Net](../guides/artnet.md).
<!-- /only -->

## Miscellaneous

| Key | Type | Range | Default | Effect | Reboot |
|---|---|---|---|---|---|
| `statsInterval` | long | 1000–600000 ms | `10000` | How often device state is published over MQTT – for plain MQTT and Home Assistant. Outside 1 s–10 min → `422`. | yes |
<!-- only esp32 esp32-s3 -->
| `tempDecimals` | uint8 | 0–2 | `0` | Number of decimal places in the Temperature app. Outside 0–2 → `422`. **Applies at once.** | no |
<!-- /only -->
| `debugMode` | bool | - | `false` | Writes detailed messages about requests and commands to the serial port and the log console. Off keeps the log short. **Applies at once.** | no |
| `scriptingEnabled` | bool | - | `true` | Main switch for [scripts](../guides/scripting/index.md). Off<!-- only esp32 esp32-s3 --> frees about 40 KB of memory and<!-- /only --> no script runs. Your scripts are not deleted, and you can still **list, read, edit and delete** them – so you can fix a script that made the clock unreachable. Changes take effect after the next restart with the switch on again.<!-- only esp32 esp32-s3 --> To switch it off on the clock itself, hold **left + right** for three seconds while switching the clock on ([troubleshooting](../troubleshooting/troubleshooting.md#scripts-eat-the-memory-and-awtrix-never-comes-up)).<!-- /only --> | yes |

Install, read and remove scripts with [`/api/v1/apps/script/{name}`](http.md#scripts); the script
language is described in the [Scripting guide](../guides/scripting/index.md).

The color settings `colorCorrection` and `colorTint` are in [Settings](settings.md), not here.

<!-- only esp32 esp32-s3 -->
## GPIO map

`PUT /api/v1/system` also holds the sixteen pin fields (`pinMatrix`, `pinBtnLeft`, `pinBtnSelect`,
`pinBtnRight`, `pinBattery`, `pinLdr`, `pinBuzzer`, `pinI2cSda`, `pinI2cScl`, `pinDfRx`, `pinDfTx`,
`pinI2sBclk`, `pinI2sLrclk`, `pinI2sDout`, `pinI2sMclk`, `pinAmpEnable`). `-1` means "not
connected" (not allowed for `pinMatrix`). **Every pin change needs a restart.**

AWTRIX checks the pins you send together with the stored ones. A problem is answered with
`400 invalidPinConfig`, naming the field, and nothing is saved. If the stored map is not valid at
startup, AWTRIX uses the default pins of its chip.

The pin table, defaults, the allowed LED matrix pins, all rules with their exact error messages and
the AWTRIX 2 pin fix are in **[GPIO & boards](gpio.md)**.
<!-- /only -->

## Wi-Fi scan

`GET /api/v1/system/wifi-scan` searches for Wi-Fi networks.

```bash
curl http://<awtrix-ip>/api/v1/system/wifi-scan
```

The first call starts a search and answers `202`. Ask again until you get `200`.

| Status | Body |
|---|---|
| `202` | `{"scanning":true}` – search starting or still running |
| `200` | list of networks |

```json
[
  { "ssid": "MyNetwork", "rssi": -52, "enc": true },
  { "ssid": "GuestWiFi", "rssi": -78, "enc": false }
]
```

`enc` is `true` for a network with a password and `false` for an open one. After the list has been
returned once, it is gone; the next request starts a new search.

<!-- only tc002 -->
Searching does not work while the setup hotspot is open (`503 scanUnavailable`). Type the
network name by hand. Searching works once the clock is connected to a network.
<!-- /only -->

## Persistence and resets

Every `PUT` is saved at once. A field you have never written keeps its default, so a firmware
update that adds a new field does not change your existing configuration.

| Route | Clears | Keeps |
|---|---|---|
| `POST /api/v1/settings/reset` | the stored settings only | **all system configuration** – Wi-Fi, MQTT<!-- only esp32 esp32-s3 -->, GPIO, calibration<!-- /only --> |
| `POST /api/v1/device/factory-reset` | Settings, **system configuration**, all files (icons, melodies, palettes, scripts), and stored Wi-Fi credentials | nothing |

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/factory-reset
```

!!! danger "A factory reset cannot be undone and takes AWTRIX off your network"
    It deletes the stored Wi-Fi details and `wifiSsid`, so AWTRIX starts in setup mode with its own
    network. All files are deleted. There is no confirmation and no undo.

Factory reset works only over HTTP. Restart, sleep and settings reset also work over
[MQTT](mqtt.md).

<!-- only esp32 esp32-s3 -->
The panel wiring is part of the system configuration, so a *settings* reset keeps it. Only a
factory reset sets it back to the defaults.
<!-- /only -->

<!-- only esp32 esp32-s3 -->
### Coming from an AWTRIX 3 device

Nothing is imported. Set AWTRIX up in the web UI or with `PUT /api/v1/system` like a new device.

Battery calibration does not carry over: AWTRIX 3's `min_battery`/`max_battery` are raw sensor
limits, while AWTRIX NG calculates the battery percentage from the battery voltage, calibrated with
`batteryDividerRatio` – see [Sensor calibration](#sensor-calibration).
<!-- /only -->

## Related

<!-- only esp32 esp32-s3 -->
- [GPIO & boards](gpio.md) – every pin rule
<!-- /only -->
- [Settings](settings.md) – brightness, apps, transitions and colors
- [Device state](device.md) – live values such as <!-- only esp32 esp32-s3 -->`lightLevel` and `batteryPinMillivolts`<!-- /only --><!-- only tc002 -->`batteryPercent` and `lowBattery`<!-- /only -->
- [MQTT topics](mqtt.md)
- [Errors – PUT /api/v1/system](errors.md#put-apiv1system)
