# Device state

`GET /api/v1/device` tells you how AWTRIX is doing right now: firmware version, network, memory,
display, sensors, battery and the indicators. The web UI dashboard reads it, and you can read it
from your own automations.

All values are live. They are not stored and they reset when AWTRIX restarts.

## Endpoint

| | |
| --- | --- |
| Method | `GET` only |
| Path | `/api/v1/device` |
| Request body | none |
| Response | `200 application/json` – a single object |
| Auth | HTTP Basic, whenever `authEnabled` is set – in setup mode too. See [Authentication](http.md#authentication) |

```bash
curl http://<awtrix-ip>/api/v1/device
```

With a password set:

```bash
curl -u admin:secret http://<awtrix-ip>/api/v1/device
```

Any method other than `GET` gets `405 methodNotAllowed`:

```bash
curl -i -X POST http://<awtrix-ip>/api/v1/device
# HTTP/1.1 405 Method Not Allowed
# {"error":{"code":"methodNotAllowed","message":"allowed: GET"}}
```

## Response shape

An example. The tables below explain every key.

<!-- only esp32 -->
```json
{
  "version": "1.0.12",
  "uid": "a4cf12ab34cd",
  "boardType": "awtrixng",
  "soc": "esp32",
  "updateImage": "firmware-awtrix-ng.bin",
  "ipAddress": "192.168.1.42",
  "hostname": "awtrixng-ab34cd",
  "wifiRssi": -58,
  "uptimeSeconds": 4213,
  "freeHeapBytes": 118234,
  "minFreeHeapBytes": 91560,
  "largestFreeBlockBytes": 63488,
  "scriptingRunning": true,
  "scriptHeapPool": "internal",
  "scriptHeapBudgetBytes": 98304,
  "resetReason": "software",
  "fps": 41,
  "brightness": 120,
  "lightLevel": 29.3,
  "ldrRaw": 1200,
  "batteryPercent": 88,
  "batteryVoltage": 4.1,
  "batteryPinMillivolts": 2290,
  "lowBattery": false,
  "temperature": 21.5,
  "humidity": 42,
  "pressureHpa": 1013.2,
  "matrixPower": true,
  "currentApp": "Time",
  "indicators": [
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0},
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0},
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0}
  ],
  "messageCount": 0,
  "wifi": {
    "enabled": true,
    "state": "connected",
    "host": "MyNetwork",
    "endpoint": "192.168.1.31",
    "attempts": 0,
    "retryInMs": 0,
    "connects": 1,
    "error": null,
    "lastError": null
  },
  "mqtt": {
    "enabled": true,
    "state": "connected",
    "host": "broker.local",
    "endpoint": "192.168.1.42:1883",
    "attempts": 0,
    "retryInMs": 0,
    "connects": 1,
    "error": null,
    "lastError": null
  },
  "mirror": {
    "sharing": false,
    "viewers": 0,
    "source": "kitchen.local",
    "state": "showing",
    "sourceWidth": 32,
    "sourceHeight": 8
  }
}
```
<!-- /only -->
<!-- only esp32-s3 -->
```json
{
  "version": "1.0.12",
  "uid": "a4cf12ab34cd",
  "boardType": "awtrixng",
  "soc": "esp32s3",
  "updateImage": "firmware-awtrix-ng-s3-octal.bin",
  "ipAddress": "192.168.1.42",
  "hostname": "awtrixng-ab34cd",
  "wifiRssi": -58,
  "uptimeSeconds": 4213,
  "freeHeapBytes": 118234,
  "minFreeHeapBytes": 91560,
  "largestFreeBlockBytes": 63488,
  "psramTotalBytes": 8388608,
  "psramFreeBytes": 8012345,
  "scriptingRunning": true,
  "scriptHeapPool": "psram",
  "scriptHeapBudgetBytes": 4006172,
  "resetReason": "software",
  "fps": 41,
  "brightness": 120,
  "lightLevel": 29.3,
  "ldrRaw": 1200,
  "batteryPercent": 88,
  "batteryVoltage": 4.1,
  "batteryPinMillivolts": 2290,
  "lowBattery": false,
  "temperature": 21.5,
  "humidity": 42,
  "pressureHpa": 1013.2,
  "matrixPower": true,
  "currentApp": "Time",
  "indicators": [
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0},
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0},
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0}
  ],
  "messageCount": 0,
  "wifi": {
    "enabled": true,
    "state": "connected",
    "host": "MyNetwork",
    "endpoint": "192.168.1.31",
    "attempts": 0,
    "retryInMs": 0,
    "connects": 1,
    "error": null,
    "lastError": null
  },
  "mqtt": {
    "enabled": true,
    "state": "connected",
    "host": "broker.local",
    "endpoint": "192.168.1.42:1883",
    "attempts": 0,
    "retryInMs": 0,
    "connects": 1,
    "error": null,
    "lastError": null
  },
  "mirror": {
    "sharing": false,
    "viewers": 0,
    "source": "kitchen.local",
    "state": "showing",
    "sourceWidth": 32,
    "sourceHeight": 8
  }
}
```
<!-- /only -->
<!-- only tc002 -->
```json
{
  "version": "1.0.12",
  "uid": "a4cf12ab34cd",
  "boardType": "tc002",
  "soc": "armv7l",
  "updateImage": "awtrix-ng-tc002.awup",
  "ipAddress": "192.168.1.42",
  "hostname": "awtrixng-ab34cd",
  "wifiRssi": -58,
  "uptimeSeconds": 4213,
  "freeHeapBytes": 61423616,
  "minFreeHeapBytes": 60817408,
  "scriptingRunning": true,
  "scriptHeapPool": "system",
  "scriptHeapBudgetBytes": 4194304,
  "resetReason": "software",
  "fps": 41,
  "brightness": 120,
  "batteryPercent": 88,
  "batteryVoltage": 4.1,
  "lowBattery": false,
  "matrixPower": true,
  "currentApp": "Time",
  "indicators": [
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0},
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0},
    {"on": false, "color": "#000000", "blinkMs": 0, "fadeMs": 0}
  ],
  "messageCount": 0,
  "wifi": {
    "enabled": true,
    "state": "connected",
    "host": "MyNetwork",
    "endpoint": "192.168.1.31",
    "attempts": 0,
    "retryInMs": 0,
    "connects": 1,
    "error": null,
    "lastError": null
  },
  "mqtt": {
    "enabled": true,
    "state": "connected",
    "host": "broker.local",
    "endpoint": "192.168.1.42:1883",
    "attempts": 0,
    "retryInMs": 0,
    "connects": 1,
    "error": null,
    "lastError": null
  },
  "mirror": {
    "sharing": false,
    "viewers": 0,
    "source": "kitchen.local",
    "state": "showing",
    "sourceWidth": 52,
    "sourceHeight": 16
  },
  "usbPower": true,
  "update": {"state": "idle", "release": "", "error": ""}
}
```
<!-- /only -->

<!-- only esp32 esp32-s3 -->
!!! warning "Check that a key exists before you read it"
    These keys are **left out completely** – not `null`, not `0` – when the hardware is missing:
    `psramTotalBytes`, `psramFreeBytes`, `lightLevel`, `ldrRaw`, `batteryPercent`,
    `batteryVoltage`, `batteryPinMillivolts`, `lowBattery`, `temperature`, `humidity`
    and `pressureHpa`.
<!-- /only -->
<!-- only tc002 -->
!!! warning "Check that a key exists before you read it"
    `batteryPercent`, `batteryVoltage` and `lowBattery` are **left out completely** – not `null`,
    not `0` – until the clock has reported its battery.
<!-- /only -->

## Always-present fields

These keys are in every response.

| Key | Type | Range / format | Units | Meaning |
| --- | --- | --- | --- | --- |
| `version` | string | - | - | Firmware version. Same value as `GET /api/v1/version`. |
| `uid` | string | 12 lowercase hex chars | - | Device ID: the Wi-Fi MAC address in lowercase without colons. It never changes, not even after a new firmware install. Also the default MQTT topic prefix and MQTT client ID. |
<!-- only esp32 esp32-s3 -->
| `boardType` | string | `awtrixng` | - | Which firmware this is. `awtrixng` on every board, whatever your pin map. |
<!-- /only -->
<!-- only tc002 -->
| `boardType` | string | `tc002` | - | Which firmware this is. |
<!-- /only -->
<!-- only esp32 -->
| `soc` | string | `esp32` | - | The chip the firmware is made for. Use it only to tell the firmware types apart; for pin rules read `gpio` in `GET /api/v1/capabilities`. |
<!-- /only -->
<!-- only esp32-s3 -->
| `soc` | string | `esp32s3` | - | The chip the firmware is made for. Use it only to tell the firmware types apart; for pin rules read `gpio` in `GET /api/v1/capabilities`. |
<!-- /only -->
<!-- only tc002 -->
| `soc` | string | `armv7l` | - | The processor the firmware is made for. |
<!-- /only -->
<!-- only esp32 -->
| `updateImage` | string | `firmware-awtrix-ng.bin` | - | The update file this device needs – the one `POST /update` accepts. The web UI uses it to offer the right download. |
<!-- /only -->
<!-- only esp32-s3 -->
| `updateImage` | string | `firmware-awtrix-ng-s3-octal.bin`, `firmware-awtrix-ng-s3-quad.bin` | - | The update file this device needs – the one `POST /update` accepts. It depends on the kind of PSRAM on your board. The web UI uses it to offer the right download. |
<!-- /only -->
<!-- only tc002 -->
| `updateImage` | string | `awtrix-ng-tc002.awup` | - | The update file this device needs – the one `POST /update` accepts. The web UI uses it to offer the right download. |
<!-- /only -->
| `ipAddress` | string | dotted quad | - | The IP address in your home network. In setup mode this is not the address you used to reach AWTRIX. |
| `hostname` | string | 1 … 32 chars | - | The name AWTRIX uses on the network and announces over mDNS (lets you open `http://<hostname>.local`). If you never set a name, it is `awtrixng-` plus the last six characters of `uid`. In that case `hostname` in `GET /api/v1/system` is empty, because it holds only a name you set yourself. |
| `wifiRssi` | integer | about −30 (excellent) to −90 (unusable) | dBm | Wi-Fi signal strength. |
| `uptimeSeconds` | integer | 0 … | seconds | Seconds since the last start. <!-- only esp32 esp32-s3 -->Starts at 0 after every restart, including after an update or a settings reset.<!-- /only --><!-- only tc002 -->It counts from the moment the clock was switched on, so a restart of AWTRIX alone keeps counting.<!-- /only --> |
<!-- only esp32 esp32-s3 -->
| `resetReason` | string | `poweron`, `external`, `software`, `panic`, `interruptWatchdog`, `taskWatchdog`, `watchdog`, `deepSleep`, `brownout`, `sdio`, `unknown` | - | Why AWTRIX last started. `poweron`: switched on. `software`: AWTRIX restarted itself (update, restart from the web UI). `panic` or one of the `watchdog` values: AWTRIX crashed or hung and was restarted. `brownout`: the power supply voltage dropped too low. `deepSleep`: woke from sleep. |
<!-- /only -->
<!-- only tc002 -->
| `resetReason` | string | `poweron`, `software`, `panic`, `watchdog` | - | Why AWTRIX last started. `poweron`: switched on. `software`: AWTRIX restarted itself (update, restart from the web UI). `panic` or `watchdog`: AWTRIX crashed or hung and was restarted. |
<!-- /only -->
| `freeHeapBytes` | integer | 0 … | bytes | Free memory right now. It changes all the time.<!-- only tc002 --> It is the memory the system can still give out.<!-- /only --> |
| `minFreeHeapBytes` | integer | 0 … | bytes | The lowest free memory seen since the last start. It only goes down. A value that keeps dropping towards zero over days points to a problem; a stable value is fine.<!-- only tc002 --> It is checked about once a second, so a very short dip can be missed.<!-- /only --> |
<!-- only esp32 esp32-s3 -->
| `largestFreeBlockBytes` | integer | 0 … | bytes | The largest piece of free memory in one block. Installing a script or playing an HTTPS stream needs one large block. If AWTRIX says "not enough memory" although `freeHeapBytes` looks high, this value is the one that is too small. |
<!-- /only -->
| `scriptingRunning` | boolean | - | - | Whether scripts run. `false` when [`scriptingEnabled`](system.md#miscellaneous) is off – installed scripts stay listed and editable, but none of them runs. |
<!-- only esp32 esp32-s3 -->
| `scriptHeapPool` | string | `internal`, `psram` | - | Which memory scripts use: `internal` (main memory) or `psram` (extra memory on boards that have it – installing a script then hardly changes `freeHeapBytes`). |
| `scriptHeapBudgetBytes` | integer | 0 … | bytes | How much memory all scripts together may use. When it is full, installing another script is refused. `98304` with `internal`; with `psram` about half the free PSRAM, so about 4 MB on an 8 MB board. Read the value instead of assuming it. |
<!-- /only -->
<!-- only tc002 -->
| `scriptHeapPool` | string | `system` | - | Which memory scripts use. |
| `scriptHeapBudgetBytes` | integer | 0 … | bytes | How much memory all scripts together may use. When it is full, installing another script is refused. A quarter of the memory free at start, at most 4 MiB. Read the value instead of assuming it. |
<!-- /only -->
| `fps` | integer | 0 … | frames/second | Frames actually shown per second, measured once per second. It moves around. |
| `brightness` | integer | 0 … 255 | - | The brightness the display uses **right now**. With `autoBrightness` off, or without a light sensor, it equals `settings.brightness`. With `autoBrightness` on and a light sensor, AWTRIX calculates it from `lightLevel` within `minBrightness`…`maxBrightness`, and `settings.brightness` is ignored. While the mood light is on, it is the mood light's `brightness`. Also shown as `brightness` in `GET /api/v1/display`. The value you set is in `GET /api/v1/settings`. |
| `matrixPower` | boolean | - | - | `true` when the display is on. Same as `power` in `GET /api/v1/display`. Change it with `PATCH /api/v1/display`. |
| `currentApp` | string | app name, or `""` | - | The app currently selected in the rotation, for example `"Time"`, `"Date"` or the name of one of your apps. Empty when no app is in the rotation. A notification shown does not change it. During a transition it still names the app that is leaving. |
| `indicators` | array | exactly 3 objects | - | The three indicators, in order 1, 2, 3. See [Indicators](#indicators). |
| `messageCount` | integer | 0 … | count | MQTT command messages received since the last start – everything that arrives under your AWTRIX's topic prefix, including a script's own subscription below that prefix. Messages AWTRIX sends (such as `/result`) and HTTP requests are not counted. |
| `wifi` | object | - | - | Whether AWTRIX is on your network, and if not, why. See [Connection status](#connection-status). |
| `mqtt` | object | - | - | Whether AWTRIX is connected to your MQTT broker, and if not, why. See [Connection status](#connection-status). |

<!-- only esp32 esp32-s3 -->
## PSRAM fields (conditional)

**Only on boards with PSRAM** (extra memory). Both keys appear together or not at all.

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `psramTotalBytes` | integer | 0 … | bytes | Total PSRAM on the board. |
| `psramFreeBytes` | integer | 0 … | bytes | Free PSRAM. Do not add it to `freeHeapBytes`: PSRAM cannot replace main memory, and main memory runs out first. |
<!-- /only -->

<!-- only esp32 esp32-s3 -->
## Light sensor fields (conditional)

**Only when the board has a light sensor pin** – `pinLdr` in [the system configuration](system.md)
is `0` or higher. The default is GPIO <!-- only esp32 -->35<!-- /only --><!-- only esp32-s3 -->2<!-- /only -->, so a stock clock has both keys. With `pinLdr: -1`
both keys disappear.

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `lightLevel` | number | 0.0 … 100.0, one decimal | percent (relative) | Ambient light as a **relative** percentage, not lux. |
| `ldrRaw` | integer | 0 … 4095 | - | The raw sensor reading: `0` in the dark, `4095` in bright light. Use this value to calibrate `ldrFactor`. |

`lightLevel` follows `ldrRaw` in a straight line. `ldrFactor` sets what counts as full light, and
`ldrOnGround` handles a sensor wired the other way round. `ldrGamma` changes only how `lightLevel`
turns into `brightness`, never `lightLevel` itself. `lightLevel` is reported whether
`autoBrightness` is on or off.
<!-- /only -->

## Battery fields (conditional)

<!-- only esp32 esp32-s3 -->
**Only when the board has a battery pin** – `pinBattery` in [the system configuration](system.md)
is `0` or higher. The default is GPIO <!-- only esp32 -->34<!-- /only --><!-- only esp32-s3 -->1<!-- /only -->, so a stock clock has all four keys. With
`pinBattery: -1` all four disappear.
<!-- /only -->
<!-- only tc002 -->
The clock measures the battery itself. `batteryPercent`, `batteryVoltage` and `lowBattery` appear
once it reports a value.
<!-- /only -->

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
<!-- only esp32 esp32-s3 -->
| `batteryPinMillivolts` | integer | 0 … 65535 | mV | Voltage **at the pin, after the resistor divider** – *not* the battery voltage. Median of the last 5 readings. Use it to calibrate `batteryDividerRatio`. |
| `batteryVoltage` | number | 0.0 … , two decimals | V | The **battery** voltage: `batteryPinMillivolts / 1000 × batteryDividerRatio`. A ratio of `0` or less uses the default `1.79`. Useful to watch a battery age, independent of the percentage. |
| `batteryPercent` | integer | 0 … 100 | percent | Charge level, estimated from `batteryVoltage` with a typical Li-Ion curve. 100 % at 4.20 V or more, 0 % at 3.27 V or less. |
<!-- /only -->
<!-- only tc002 -->
| `batteryVoltage` | number | 0.0 … , two decimals | V | The **battery** voltage as the clock measures it. `0` while it has not reported one. Useful to watch a battery age, independent of the percentage. |
| `batteryPercent` | integer | 0 … 100 | percent | Charge level as the clock reports it. |
<!-- /only -->
| `lowBattery` | boolean | - | - | `true` when `batteryPercent` is below `lowBatteryThreshold` in [the system configuration](system.md). Always `false` while `lowBatteryThreshold` is `0` (the default). Also shown as a Home Assistant "Low battery" binary sensor. |
<!-- only tc002 -->
| `usbPower` | boolean | - | - | `true` while the clock is on USB power and charges its battery. Appears once the clock has reported its power supply. Also shown as a Home Assistant "Charging" binary sensor. |
<!-- /only -->

<!-- only esp32 esp32-s3 -->
Calibrate the divider, not the percentage.<!-- only esp32 --> On a stock Ulanzi TC001 a full battery reads about
2347 mV at the pin. The default ratio of 1.79 turns this into about 4.20 V.<!-- /only --> If `batteryVoltage`
looks wrong, read `batteryPinMillivolts` with a fully charged battery and set
`batteryDividerRatio = 4.2 / (batteryPinMillivolts / 1000)`. See [Power & battery](../guides/power.md).
<!-- /only -->

`batteryPercent` is only an estimate from the voltage. It cannot tell you the remaining runtime,
and it drops a little under load.

<!-- only esp32 esp32-s3 -->
## Environment fields (conditional)

**Only when a temperature sensor was found at startup.** AWTRIX looks for BME280, then BMP280,
then HTU21DF, then SHT31. If none answers – or if `pinI2cSda` / `pinI2cScl` is `-1` – all three
keys are left out. A sensor you connect later is found only after a restart.

Only the values the sensor can measure appear:

| Sensor | `temperature` | `humidity` | `pressureHpa` |
| --- | :---: | :---: | :---: |
| BME280 | ✓ | ✓ | ✓ |
| BMP280 | ✓ | - | ✓ |
| HTU21DF | ✓ | ✓ | - |
| SHT31 | ✓ | ✓ | - |

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `temperature` | number | sensor-dependent, one decimal | °C | Temperature plus `tempOffset`. **Always Celsius**, whatever `useCelsius` says – that setting changes only the display. |
| `humidity` | number | sensor-dependent, one decimal | percent RH | Relative humidity plus `humOffset`. Left out on temperature-only sensors. |
| `pressureHpa` | number | sensor-dependent, one decimal | hPa | Air pressure. Only with a BME280 or BMP280. |
<!-- /only -->

<!-- only tc002 -->
## Update fields (conditional)

The clock installs `.awup` update packages. See [Updating](../guides/updating.md#tc002-updates).

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `update` | object | - | - | The web update: `state`, `release` and `error`. |

`update.state` is one of:

- `idle` – no update is running.
- `applying` – a package is being installed and the clock restarts.
- `boot-pending` – the new release has started and is being checked.
- `confirmed` – the new release works.
- `failed` – the new release did not start properly, or the installation was interrupted.

`update.release` names the release, and `update.error` says what went wrong.
<!-- /only -->

## Indicators

`indicators` always holds 3 objects: index `0` is indicator 1, index `2` is indicator 3. It shows
what was last set with `PUT /api/v1/indicators/{id}` (or over MQTT / Home Assistant), which is also
what you see on the display: dots on the right edge, indicator 1 at the top, indicator 3 at the
bottom.

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `on` | boolean | - | - | Whether the indicator is on. |
| `color` | string | `"#RRGGBB"` | - | Color, uppercase hex. Default `#000000`. |
| `blinkMs` | integer | 0 … 65535 | milliseconds | Blink interval. `0` = no blinking. |
| `fadeMs` | integer | 0 … 65535 | milliseconds | Fade interval. `0` = no fading. |

## Connection dots

The right edge of the display is for your indicators. The **left** edge is for AWTRIX: it shows a
single pulsing dot there while a connection is missing.

| Where | Color | Meaning |
| --- | --- | --- |
| Top-left corner | red | AWTRIX is not on your Wi-Fi network. |
| Bottom-left corner | yellow | AWTRIX is on the network but not connected to your MQTT broker. |

The dots appear by themselves, cannot be switched off, and go away as soon as the connection is
back.

Only one dot shows at a time. Without Wi-Fi there is no MQTT either, so a network outage shows only
the red dot. If no broker is set up, the yellow dot never shows.

You do not see the dots while the display is off, on the setup screen<!-- only esp32 esp32-s3 -->, in mood light, or while an
Art-Net stream is running<!-- /only --><!-- only tc002 --> or in mood light<!-- /only -->. To find out why a connection is down, read `wifi` and `mqtt` below.

## Connection status

`wifi` and `mqtt` tell you whether AWTRIX reached your network and your broker, and if not, why.
Both objects have the same keys. `mqtt` also feeds the **Connection** line on the web UI's MQTT
tab. For the dots on the display see [Connection dots](#connection-dots).

```json
"mqtt": {
  "enabled": true,
  "state": "offline",
  "host": "broker.local",
  "endpoint": "",
  "attempts": 4,
  "retryInMs": 40000,
  "connects": 0,
  "error": "hostNotFound",
  "lastError": "hostNotFound"
}
```

| Key | Type | Meaning for `wifi` | Meaning for `mqtt` |
| --- | --- | --- | --- |
| `enabled` | boolean | A network name is stored. | Same as `mqttEnabled`. |
| `state` | string | `disabled`, `offline`, `connecting` or `connected`. | Same four values. |
| `host` | string | The network name (SSID). | The broker host as you entered it. |
| `endpoint` | string | AWTRIX's IP address on that network. Empty while not connected. | The broker address and port in use, once the name is resolved. Empty before that – an empty `endpoint` with a broker name means the name was not found yet. |
| `attempts` | integer | Failed attempts in a row. `0` while connected. | Same. |
| `retryInMs` | integer | Milliseconds until the next attempt. `0` while connected and during an attempt. | Same. |
| `connects` | integer | Successful connections since the last start. A number that keeps rising means the connection keeps dropping – for `wifi`, usually the router or the range. | Same. |
| `error` | string / null | Why it is not connected **right now**. `null` when connected. | Same. |
| `lastError` | string / null | Why the connection last went down. **Kept after it comes back.** `null` until something goes wrong. | Same. |

!!! note "Why `wifi` has a `lastError`"
    While Wi-Fi is down, you cannot reach this API – only the red dot tells you. `lastError` stays
    after the connection is back. So `"lastError": "lost"` together with `"state": "connected"`
    means the connection dropped and came back. `connects` tells you how often.

    You can read `wifi.error` live in one case: when AWTRIX could not join your network at
    startup. It then opens its own setup hotspot, and the API is reachable there.

### What each error means

| `error` | What it means | What to do |
| --- | --- | --- |
| `noWifi` | *(`mqtt` only)* AWTRIX is not on the network. | Fix Wi-Fi first; MQTT needs it. You see this only if you reach AWTRIX some other way. |
| `hostNotFound` | For `wifi`: your network was not found. For `mqtt`: the broker name could not be resolved. | For Wi-Fi, check the network name and that the router is on. For MQTT, check the spelling. A `.local` name only works if something on your network answers for it – if in doubt, enter the broker's IP address. |
| `refused` | *(`mqtt` only)* Nothing accepted a connection at that address and port. | Check the port, and that the broker runs and is reachable from AWTRIX's network. |
| `badCredentials` | The password was rejected – by the router for `wifi`, by the broker for `mqtt`. | Enter the Wi-Fi password again, or `mqttUser` and `mqttPass`. |
| `rejected` | *(`mqtt` only)* The broker refused AWTRIX for another reason. | Check the broker's log. |
| `timeout` | For `wifi`: the network did not answer in time while AWTRIX was starting. For `mqtt`: something answered at that address but it did not speak MQTT. | For Wi-Fi, usually range or a router that was still starting; AWTRIX keeps trying. For MQTT, usually the wrong port. |
| `lost` | The connection was up and dropped. | For Wi-Fi, usually range or a router restart. For MQTT, the same or a broker restart. AWTRIX reconnects by itself. |

### Retries back off

After a failed attempt AWTRIX waits 5 seconds, then 10, 20, 40, and then 60 seconds between
attempts. Each wait is up to 20 % shorter at random, so many clocks do not all retry at the same
moment. After a successful connection the waits start again at 5 seconds. `retryInMs` counts down
to the next attempt.

AWTRIX looks up the broker's address once and keeps it while it works. It looks it up again after
three failed attempts in a row, and after it rejoins Wi-Fi.

## Mirroring status

`mirror` tells you what [mirroring](../guides/mirroring.md) is doing. It feeds the **State** line in
the web UI's *Mirroring* section.

| Key | Type | Meaning |
| --- | --- | --- |
| `sharing` | boolean | This clock shares its display and is on the network. |
| `viewers` | integer | How many clocks watch this display right now. |
| `source` | string | The clock this clock mirrors, as entered in `mirrorFrom`. Empty when it mirrors none. |
| `state` | string | What mirroring the `source` clock does right now; see below. |
| `sourceWidth`, `sourceHeight` | integer | The display size the `source` clock reported. Left out until it has answered. |

| `state` | What it means |
| --- | --- |
| `off` | `mirrorFrom` is empty. |
| `offline` | This clock is not on the network. |
| `resolving` | The host name is being looked up. |
| `notFound` | The host name could not be found. It is looked up again every 10 seconds. |
| `waiting` | No answer from that clock for more than 3 seconds, or none yet. |
| `idle` | That clock answers but shows nothing it shares. |
| `filtered` | That clock shows something `mirrorFromApps` or `mirrorFromNotifications` leaves out. |
| `sizeMismatch` | The two displays differ in size. |
| `noMemory` | There was no memory for the mirrored picture. |
| `showing` | The mirrored picture is on the display. |

## Also published over MQTT

The same JSON is published, **retained**, to `<prefix>/state/device`. A subscriber gets the latest
state as soon as it connects, so you do not need to poll over HTTP. See [MQTT topics](mqtt.md) for
how often it is sent and for the other state topics.

## How often the values update

The values are updated on their own schedule, not when you ask. Asking more often than this returns
the same values.

| Field(s) | Updated |
| --- | --- |
<!-- only esp32 esp32-s3 -->
| `ldrRaw`, `lightLevel` | every 100 ms, median of 5 readings |
<!-- /only -->
| `brightness` | every frame |
<!-- only esp32 esp32-s3 -->
| `batteryPinMillivolts`, `batteryVoltage`, `batteryPercent`, `lowBattery` | every 2 s, median of 5 readings |
| `temperature`, `humidity`, `pressureHpa` | every 2 s |
<!-- /only -->
<!-- only tc002 -->
| `batteryVoltage`, `batteryPercent`, `lowBattery`, `usbPower` | whenever the clock reports its battery |
<!-- /only -->
| `fps` | once per second |
| `uptimeSeconds`, `freeHeapBytes`, `minFreeHeapBytes`, <!-- only esp32 esp32-s3 -->`largestFreeBlockBytes`, <!-- /only -->`ipAddress`, `wifiRssi` | when you ask |
| `resetReason`, `hostname` | at startup |

## Platform capabilities

Tools that support several device types can read `GET /api/v1/capabilities` to find out what the
device offers. Part of the answer describes the platform:

<!-- only esp32 -->
```json
{
  "platform": { "id": "esp32" },
  "sensors": { "light": true },
  "display": { "width": 32, "height": 8, "configurable": true }
}
```
<!-- /only -->
<!-- only esp32-s3 -->
```json
{
  "platform": { "id": "esp32s3" },
  "sensors": { "light": true },
  "display": { "width": 32, "height": 8, "configurable": true }
}
```
<!-- /only -->
<!-- only tc002 -->
```json
{
  "platform": { "id": "tc002" },
  "sensors": { "light": false },
  "display": { "width": 52, "height": 16, "configurable": false },
  "gpio": null
}
```
<!-- /only -->

| Key | Meaning |
| --- | --- |
<!-- only esp32 -->
| `platform.id` | `esp32`. |
| `sensors.light` | `true` when there is a light sensor, that is when `pinLdr` is set (a pin change shows after a restart). Without a light sensor `autoBrightness` has no effect and the web UI hides the auto-brightness controls. |
| `display.width` / `display.height` | Display size in pixels. The top-left pixel is `(0, 0)`, so `(31, 7)` is the bottom-right pixel of a 32×8 display. Scripts, draw commands and `/api/v1/display/screen` use the same coordinates. |
| `display.configurable` | `true`: the display size and wiring can be changed. |
| `gpio` | The pin rules of the chip – see [GPIO & boards](gpio.md). |
<!-- /only -->
<!-- only esp32-s3 -->
| `platform.id` | `esp32s3`. |
| `sensors.light` | `true` when there is a light sensor, that is when `pinLdr` is set (a pin change shows after a restart). Without a light sensor `autoBrightness` has no effect and the web UI hides the auto-brightness controls. |
| `display.width` / `display.height` | Display size in pixels. The top-left pixel is `(0, 0)`, so `(31, 7)` is the bottom-right pixel of a 32×8 display. Scripts, draw commands and `/api/v1/display/screen` use the same coordinates. |
| `display.configurable` | `true`: the display size and wiring can be changed. |
| `gpio` | The pin rules of the chip – see [GPIO & boards](gpio.md). |
<!-- /only -->
<!-- only tc002 -->
| `platform.id` | `tc002`. |
| `sensors.light` | `false`: the clock has no light sensor, so `autoBrightness` has no effect and the web UI hides the auto-brightness controls. |
| `display.width` / `display.height` | Display size in pixels. The top-left pixel is `(0, 0)`, so `(51, 15)` is the bottom-right pixel of the 52×16 display. Scripts, layouts and `/api/v1/display/screen` use these coordinates. Draw commands in pushed apps and notifications use a 26×8 grid at double size while [`enlargeApps`](settings.md#global-text) is on. |
| `display.configurable` | `false`: the display size is fixed. |
| `gpio` | `null`: the pins cannot be changed. |
<!-- /only -->

## Related

- [HTTP API](http.md) – conventions, auth, and the full route list
- [System configuration](system.md) – <!-- only esp32 esp32-s3 -->`pinBattery`, `pinLdr`, `batteryDividerRatio`, `lowBatteryThreshold`, `ldrFactor`, `ldrGamma`, `minBrightness`, `maxBrightness`, `tempOffset`, `humOffset`<!-- /only --><!-- only tc002 -->`lowBatteryThreshold`<!-- /only -->
- [Settings](settings.md) – `brightness`, `autoBrightness` and the rest of the user settings
<!-- only esp32 esp32-s3 -->
- [Brightness & sensors](../guides/brightness.md) – calibrating the light sensor
- [Power & battery](../guides/power.md) – calibrating the divider ratio
<!-- /only -->
<!-- only tc002 -->
- [Power & battery](../guides/power.md) – the battery and the low-battery warning
<!-- /only -->
- [Errors](errors.md) – what a failing request answers
