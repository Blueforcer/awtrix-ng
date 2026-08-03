# Device state

`GET /api/v1/device` is the read-only status snapshot of AWTRIX: firmware version, identity,
network, memory, display state, sensors, battery and the indicator slots. It is the endpoint the
web UI dashboard polls, and the one to poll from an automation.

Every value is live: nothing here is persisted, there is no caching and no `ETag`, and everything
resets on reboot.

## Endpoint

| | |
| --- | --- |
| Method | `GET` only |
| Path | `/api/v1/device` |
| Request body | none |
| Response | `200 application/json` - a single object |
| Auth | HTTP Basic, whenever `authEnabled` is set - in AP mode too. See [Authentication](http.md#authentication) |

```bash
curl http://<awtrix-ip>/api/v1/device
```

With auth enabled:

```bash
curl -u admin:secret http://<awtrix-ip>/api/v1/device
```

Any method other than `GET` on this path is rejected with `405 methodNotAllowed`:

```bash
curl -i -X POST http://<awtrix-ip>/api/v1/device
# HTTP/1.1 405 Method Not Allowed
# {"error":{"code":"methodNotAllowed","message":"allowed method(s): GET"}}
```

## Response shape

Keys are emitted in a fixed order. 21 fields are always present; up to 11 more appear only when the
hardware supports them.

```json
{
  "version": "1.0.12",
  "uid": "a4cf12ab34cd",
  "boardType": "awtrixng",
  "soc": "esp32",
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
  }
}
```

!!! warning "Do not assume a field exists"
    The conditional fields - `psramTotalBytes`, `psramFreeBytes`, `lightLevel`, `ldrRaw`,
    `batteryPercent`, `batteryVoltage`, `batteryPinMillivolts`, `lowBattery`, `temperature`,
    `humidity` and `pressureHpa` - are **omitted entirely**, not null and not zero, when the
    hardware is not there. Check for the key before you read it.

## Always-present fields

These 22 keys are in every response, on every board, in every state.

| Key | Type | Range / format | Units | Meaning |
| --- | --- | --- | --- | --- |
| `version` | string | - | - | Running firmware version. Same value as `GET /api/v1/version`. |
| `uid` | string | 12 lowercase hex chars | - | Device identity: the WiFi MAC address, lowercased, colons stripped. Stable across reboots and reflashes. Also the default MQTT topic prefix and MQTT client id. |
| `boardType` | string | constant `"awtrixng"` | - | A fixed constant in the device firmware - it does **not** vary with your GPIO configuration. The simulator reports `"simulator"` instead. |
| `soc` | string | `esp32`, `esp32s3` | - | The chip this image was built for. Branch on this only to tell the two firmware images apart; for pin rules read `gpio` in `GET /api/v1/capabilities`. |
| `ipAddress` | string | dotted quad | - | The station-mode IP address. In AP (provisioning) mode this is not the address you reached AWTRIX on. |
| `hostname` | string | 1 … 32 chars | - | The name AWTRIX answers to on the network and publishes over mDNS. Read the configured value from `GET /api/v1/system`; that one is empty when the name is derived from the MAC (`awtrixng-` plus the last six hex digits of `uid`), which is why the two fields disagree on a device that was never named by hand. |
| `wifiRssi` | integer | typically −30 (excellent) to −90 (unusable) | dBm | Current signal strength of the station connection. |
| `uptimeSeconds` | integer | 0 … | seconds | Whole seconds since boot. Resets on every reboot, including the reboot after an OTA update or a settings reset. |
| `resetReason` | string | `poweron`, `external`, `software`, `panic`, `interruptWatchdog`, `taskWatchdog`, `watchdog`, `deepSleep`, `brownout`, `sdio`, `unknown` | - | Why AWTRIX last came up. Constant for the whole session. |
| `freeHeapBytes` | integer | 0 … | bytes | Free internal heap right now. Useful as a leak canary; it fluctuates constantly with rendering and networking. |
| `minFreeHeapBytes` | integer | 0 … | bytes | The **low-water mark**: the least free heap seen since boot. Unlike `freeHeapBytes` it only ever falls, so it survives the spike you were not polling during. A value creeping towards zero is a leak; a stable one is not. Resets on reboot. |
| `largestFreeBlockBytes` | integer | 0 … | bytes | The largest single **contiguous** block free right now, in the same internal pool as `freeHeapBytes`. Always ≤ `freeHeapBytes`, and the gap between them is fragmentation. Installing a script or opening an HTTPS stream needs its memory in one piece, so plenty of total free spread over small blocks is still refused. Watch this, not `freeHeapBytes`, to understand a "not enough memory" refusal. |
| `scriptingRunning` | boolean | - | - | Whether scripts are running at all. `false` when [`scriptingEnabled`](system.md#miscellaneous) is off - installed scripts stay listed and editable, but none of them executes. |
| `scriptHeapPool` | string | `internal`, `psram` | - | Which pool the Berry VM allocates from. Where PSRAM is usable the script heap lives there, so installing a script barely moves `freeHeapBytes`. |
| `scriptHeapBudgetBytes` | integer | 0 … | bytes | How large the shared Berry heap may grow before further installs are refused. `98304` on internal RAM; on PSRAM about half the free pool, so roughly 4 MB on an 8 MB module. Read it back rather than assuming. |
| `fps` | integer | 0 … | frames/second | **Measured** render rate, not a target: the frames actually shown, recounted once per second. Expect it to move around. |
| `brightness` | integer | 0 … 255 | - | The **effective** panel brightness currently driving the LEDs, not an echo of `settings.brightness`. With `autoBrightness` off it is `settings.brightness` clamped to 0…255 and the two agree. With `autoBrightness` on it is derived from `lightLevel` along the gamma curve and mapped into the `minBrightness`…`maxBrightness` window, and `settings.brightness` is ignored. Mirrored as `brightness` in `GET /api/v1/display`; read `GET /api/v1/settings` for the configured value. |
| `matrixPower` | boolean | - | - | `true` when the panel is on. Mirrors `power` in `GET /api/v1/display`. Set it via `PATCH /api/v1/display`. |
| `currentApp` | string | app name, or `""` | - | The app currently selected in the rotation, e.g. `"Time"`, `"Date"`, or one of your own apps' names. Empty string when the rotation holds no apps. A notification covering the panel does not change it, and during a transition it names the app being left until the transition commits. |
| `indicators` | array | exactly 3 objects | - | State of indicator slots 1, 2 and 3, in that order. See [Indicators](#indicators). |
| `messageCount` | integer | 0 … | count | Inbound **MQTT** command messages since boot - anything arriving under the topic prefix of your AWTRIX, including a script's own subscription if that topic sits under the prefix. The `/result` messages AWTRIX publishes back are not counted, and HTTP requests are never counted. Resets to 0 on reboot. |
| `wifi` | object | - | - | Whether AWTRIX is on your network, and if not, why. See [Connection status](#connection-status). |
| `mqtt` | object | - | - | Whether AWTRIX is talking to your broker, and if not, why. See [Connection status](#connection-status). |

## PSRAM fields (conditional)

**Present only when the board has external PSRAM.** Both fields appear together, or not at all.

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `psramTotalBytes` | integer | 0 … | bytes | External PSRAM on the module. |
| `psramFreeBytes` | integer | 0 … | bytes | Free PSRAM. Do not add this to `freeHeapBytes`: the two heaps are not interchangeable, and it is the internal one that runs out first. |

## Light sensor fields (conditional)

**Present only when the board has a light sensor pin** - that is, when `pinLdr` in
[the system configuration](system.md) is `>= 0`. The default (ESP32 wiring) is GPIO 35, so on
a stock device both fields are present. Set `pinLdr` to `-1` and both disappear from the response.

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `lightLevel` | number | 0.0 … 100.0, one decimal | percent (relative) | Ambient light as a **relative** percentage, not lux. |
| `ldrRaw` | integer | 0 … 4095 | - | The raw light-sensor reading behind `lightLevel`: `0` in the dark, `4095` in bright light. This is the number to use when calibrating `ldrFactor`. |

`lightLevel` rises linearly with `ldrRaw`, so calibrate it there: `ldrFactor` sets what counts as
full light, and `ldrOnGround` accounts for a sensor wired the other way round. `ldrGamma` shapes
only the step from `lightLevel` to `brightness`; it never changes `lightLevel`, which is reported
whether or not `autoBrightness` is switched on.

## Battery fields (conditional)

**Present only when the board has a battery pin** - that is, when `pinBattery` in
[the system configuration](system.md) is `>= 0`. The default (ESP32 wiring) is GPIO 34, so
on a stock device these four fields are present. Set `pinBattery` to `-1` and all four disappear
from the response.

The first three come from the same reading: pin millivolts → cell volts → percent; `lowBattery` is
a flag derived from that percent.

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `batteryPinMillivolts` | integer | 0 … 65535 | mV | Voltage measured **at the ESP32 pin, before the resistor divider** - *not* the battery voltage. Median-filtered over the last 5 samples. This is the raw input to the divider maths, and the number you calibrate `batteryDividerRatio` against. |
| `batteryVoltage` | number | 0.0 … , two decimals | V | The reconstructed **cell** voltage: `batteryPinMillivolts / 1000 × batteryDividerRatio`. A ratio `<= 0` falls back to the built-in default of `1.79`. This value lets you track a cell's ageing directly, independent of the percentage. |
| `batteryPercent` | integer | 0 … 100 | percent | State of charge, read off a resting Li-Ion discharge curve using `batteryVoltage`. Voltages between two points on the curve are blended; anything past either end stops at 100 % or 0 %. 100 % at ≥ 4.20 V, 0 % at ≤ 3.27 V. |
| `lowBattery` | boolean | - | - | `true` when `batteryPercent` has fallen below the `lowBatteryThreshold` in [the system configuration](system.md). Always `false` when `lowBatteryThreshold` is `0` (the default), which disables the check. Also shown as a Home Assistant "Low battery" binary sensor. |

Calibrate the divider, not the percentage. On a stock Ulanzi TC001 a full cell reads roughly
2347 mV at the pin, which the default ratio of 1.79 turns into ≈ 4.20 V at the cell. If your
`batteryVoltage` looks wrong, read `batteryPinMillivolts` on a known-full cell and set
`batteryDividerRatio = 4.2 / (batteryPinMillivolts / 1000)`. See [Power & battery](../guides/power.md).

`batteryPercent` is a charge estimate from voltage alone. There is no fuel gauge in the hardware,
so it cannot give you a runtime estimate, and it will move under load as the cell sags.

## Environment fields (conditional)

**Present only when an I²C environment sensor was detected at boot.** Detection happens once during
startup, in probe order: BME280, then BMP280, then HTU21DF, then SHT31. If none answers - or if
`pinI2cSda` / `pinI2cScl` is `-1`, which disables I²C entirely - all three keys are omitted, and
plugging a sensor in later does nothing until you reboot.

Which keys appear is gated by what the detected sensor can measure. A field is never emitted with a
phantom `0`.

| Sensor | `temperature` | `humidity` | `pressureHpa` |
| --- | :---: | :---: | :---: |
| BME280 | ✓ | ✓ | ✓ |
| BMP280 | ✓ | - | ✓ |
| HTU21DF | ✓ | ✓ | - |
| SHT31 | ✓ | ✓ | - |

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `temperature` | number | sensor-dependent, one decimal | °C | Sensor temperature plus the configured `tempOffset`. **Always Celsius here**, regardless of the `useCelsius` setting - that setting only affects what the panel draws. |
| `humidity` | number | sensor-dependent, one decimal | percent RH | Sensor relative humidity plus the configured `humOffset`. Omitted on temperature-only sensors. |
| `pressureHpa` | number | sensor-dependent, one decimal | hPa | Barometric pressure. Present only on a barometer (BME280 / BMP280). |

## Indicators

`indicators` is always an array of exactly 3 objects, one per slot, index `0` → indicator 1,
index `2` → indicator 3. It reflects whatever was last written via `PUT /api/v1/indicators/{id}`
(or over MQTT / Home Assistant), and it matches what is on screen: the slots paint as pixels down
the panel's right edge, slot 1 at the top and slot 3 at the bottom.

| Key | Type | Range | Units | Meaning |
| --- | --- | --- | --- | --- |
| `on` | boolean | - | - | Whether the slot is switched on. |
| `color` | string | `"#RRGGBB"` | - | The slot's colour, uppercase hex. Defaults to `#000000`. |
| `blinkMs` | integer | 0 … 65535 | milliseconds | Blink interval. `0` = solid. |
| `fadeMs` | integer | 0 … 65535 | milliseconds | Fade interval. `0` = no fade. |

## Connection dots

The right edge belongs to your indicators. The **left** edge belongs to AWTRIX, which lights a
single pulsing pixel there while a connection it should have is missing:

| Where | Colour | Meaning |
| --- | --- | --- |
| Top-left corner | red | AWTRIX is not on your WiFi network. |
| Bottom-left corner | yellow | AWTRIX is on the network but not talking to your MQTT broker. |

The dots appear on their own, cannot be switched off, and disappear the moment the connection is
back.

Only one lights at a time. Without WiFi there is no MQTT either, so a network outage shows the red
dot alone - a lit dot always points at one thing to fix. A device with no broker configured never
shows the yellow dot at all.

You will not see the dots while the panel is off, on the setup screen, in mood light, or while an
Art-Net stream is running. For the reason behind an outage, read `wifi` and `mqtt` below.

## Connection status

`wifi` and `mqtt` report whether AWTRIX reached your network and your broker, and if not, why.
Both objects have the same shape, and `mqtt` drives the **Connection** line on the web UI's MQTT
tab. AWTRIX also shows an outage on the panel itself - see [Connection dots](#connection-dots).

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
| `enabled` | boolean | A network name is stored. | Mirrors `mqttEnabled`. |
| `state` | string | `disabled`, `offline`, `connecting` or `connected`. | Same four values. |
| `host` | string | The network name (SSID) you joined. | The broker host you configured, as entered. |
| `endpoint` | string | The IP address AWTRIX holds on that network. Empty while not joined. | The address and port actually in use, once the host has been resolved. Empty before that - so an empty `endpoint` on a named broker means the name has not resolved yet. |
| `attempts` | integer | Consecutive failed attempts. `0` while connected. | Same. |
| `retryInMs` | integer | Milliseconds until the next attempt. `0` while connected, and while an attempt is running. | Same. |
| `connects` | integer | Successful connections since boot. A number that keeps climbing is a link that keeps dropping - for `wifi`, usually a router or a range problem. | Same. |
| `error` | string / null | Why it is not up **right now**. `null` when it is. | Same. |
| `lastError` | string / null | The last reason this link went down, **kept after it recovers**. `null` until something goes wrong. | Same. |

!!! note "Why `wifi` has a `lastError`"
    While WiFi is down nothing can reach this API to ask why - the panel's red dot is the only
    live signal you get. `lastError` is what you read afterwards: it survives the reconnect, so
    `"lastError": "lost"` on a device reporting `"state": "connected"` means the link dropped and
    came back while you were not looking. Together with `connects`, that is the whole picture of
    an outage you missed.

    The one case where `wifi.error` is readable live is a device that never got onto your network
    at boot: it falls back to its own setup network, and the API is reachable there.

### What each error means

| `error` | What it means | What to do |
| --- | --- | --- |
| `noWifi` | *(`mqtt` only)* AWTRIX is not on the network. | Fix the WiFi first; MQTT cannot be reached without it. You will only ever see this value on a device you reached some other way - if the WiFi is down, so is this API. |
| `hostNotFound` | For `wifi`: the network you configured was not on the air. For `mqtt`: the broker name did not resolve. | For WiFi, check the network name and that the router is up. For MQTT, check the spelling - a `.local` name needs a responder answering for it on the same network, so if in doubt enter the broker's IP address instead. |
| `refused` | *(`mqtt` only)* Nothing accepted a connection at that address and port. | Check the port, and that the broker is running and reachable from the network AWTRIX is on. |
| `badCredentials` | The password was rejected - by the router for `wifi`, by the broker for `mqtt`. | Re-enter the WiFi password, or `mqttUser` and `mqttPass`. |
| `rejected` | *(`mqtt` only)* The broker refused the client for some other reason. | Check the broker's own log. |
| `timeout` | For `wifi`: the network did not answer in time while AWTRIX was starting up. For `mqtt`: something answered at that address but never completed an MQTT handshake. | For WiFi, usually range or a router that was still booting; AWTRIX keeps trying. For MQTT, usually the wrong port - something that is not a broker is listening there. |
| `lost` | The connection was up and dropped. | For WiFi, usually range or a router restart. For MQTT, often the same, or a broker restart. AWTRIX reconnects on its own. |

### Retries back off

A failed connection is retried after 5 seconds, then 10, 20, 40, and at most every 60 seconds, each
delay shortened by up to 20 % so a rebooting broker does not get every device knocking in lockstep.
A successful connection resets the schedule. `retryInMs` counts down to the next attempt.

The broker's address is looked up once and kept for as long as it works. It is looked up again after
three consecutive failures against the same address, and after AWTRIX rejoins WiFi.

## Also published over MQTT

The same JSON document is published, **retained**, to `<prefix>/state/device`. Because it is
retained, a subscriber gets the last snapshot immediately on connect, which is cheaper than HTTP
polling. See [MQTT topics](mqtt.md) for the publish cadence and the rest of the state topics.

## How often the values update

The values in this response are refreshed on their own schedules, not at request time. Polling
faster than these intervals returns repeated values.

| Field(s) | Refresh interval |
| --- | --- |
| `ldrRaw`, `lightLevel`, `brightness` | every 100 ms, median-filtered over 5 samples |
| `batteryPinMillivolts`, `batteryVoltage`, `batteryPercent`, `lowBattery` | every 2 s, median-filtered over 5 samples |
| `temperature`, `humidity`, `pressureHpa` | every 2 s |
| `fps` | recomputed once per second |
| `uptimeSeconds`, `freeHeapBytes`, `minFreeHeapBytes`, `largestFreeBlockBytes`, `ipAddress`, `wifiRssi` | read at request time |
| `resetReason`, `hostname` | fixed at boot |

## In the simulator

The native simulator serves `GET /api/v1/device` with the same key set, with a few differences worth
knowing if you develop against it: `boardType` is `"simulator"`, and the battery fields (including
`lowBattery`) plus `temperature` and `humidity` are **always** present, because the simulated board
always reports a battery and a temperature+humidity sensor. The simulated sensor is not a barometer,
so `pressureHpa` is **absent** in the simulator - the one environment field you cannot exercise there.
Do not use the simulator to test how your client handles missing fields - point it at a device with
`pinBattery` set to `-1` instead. See [Simulator](../advanced/simulator.md).

## Related

- [HTTP API v1](http.md) - conventions, auth, and the full route list
- [System configuration](system.md) - `pinBattery`, `pinLdr`, `batteryDividerRatio`, `lowBatteryThreshold`, `ldrFactor`, `ldrGamma`, `minBrightness`, `maxBrightness`, `tempOffset`, `humOffset`
- [Settings](settings.md) - `brightness`, `autoBrightness` and the rest of the user settings
- [Brightness & sensors](../guides/brightness.md) - calibrating the LDR
- [Power & battery](../guides/power.md) - calibrating the divider ratio
- [Errors](errors.md) - what a failing request answers
