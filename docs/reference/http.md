# HTTP API

This page lists every HTTP route AWTRIX answers, with its fields, answers and errors.

The rules that apply to every route (`camelCase` keys, `...Ms` durations, the color forms, the
`Content-Type` header and the error body) are on [Conventions](conventions.md) and are not
repeated here.

<swagger-ui src="../api/openapi.yaml"/>

## How to read a route entry

Each route below follows the same order:

1. one sentence on what it does,
2. **Request**: body, path or query values,
3. **Response**: what a successful call returns,
4. **Errors**: every status the route can answer besides success,
5. a `curl` example.

A wrong HTTP method answers `405 methodNotAllowed` with the message `allowed: <list>`. The
Errors table of each route names that list. Which check comes first, and the few paths that check
their name before the method: [Errors → Order of the checks](errors.md#order-of-the-checks).

## Base URL

All routes live under `http://<awtrix-ip>`.

<!-- only esp32 esp32-s3 -->
The web port is the `webPort` setting (default `80`, with `0` also meaning `80`). After a change,
use `http://<awtrix-ip>:<webPort>`. In access-point (setup) mode the port is always `80`.
<!-- /only -->
<!-- only tc002 -->
The web port is always `80`.
<!-- /only -->

AWTRIX also answers on its mDNS hostname (for example `http://awtrix.local`). See
[Find your clock](../getting-started/discovery.md).

### Captive-portal redirect (setup mode only)

While AWTRIX is in setup mode, a `GET` or `HEAD` for a foreign or missing `Host` answers
**302** with `Location: http://<apIp>/`. This sends captive-portal probes to the setup page.
Writes to a foreign or missing host answer `403 forbidden`. The hotspot's address, with its
HTTP port when needed, is the accepted host. A foreign `Origin` or `Sec-Fetch-Site: cross-site`
is refused. On the accepted host, `GET` probes outside `/api/` whose paths contain no file
extension are also redirected. In normal mode this setup policy does not apply.

## Authentication

AWTRIX uses HTTP Basic authentication. It is **off by default**: every request is allowed until
you set `authEnabled` to `true` in the [system configuration](system.md). The username and
password are `authUser` and `authPass`.

In the web UI this is under System › Web server. Type the new password twice: the page saves only
when both entries match. The eye button next to a password field shows what you typed.

Once it is on, it applies:

* in **every** mode, including setup mode, so the setup page can be password protected,
* to **everything** AWTRIX serves: the API, the web UI at `/` and the static files.

A request without valid credentials gets:

```http
HTTP/1.1 401 Unauthorized
WWW-Authenticate: Basic realm="AWTRIX NG"
Content-Type: application/json

{"error":{"code":"unauthorized","message":"authentication required"}}
```

The `WWW-Authenticate` header makes a browser show its login prompt. The body is the normal JSON
error body, never HTML.

For the uploads (`/update`, `/api/v1/files`, `/api/v1/audio/mp3`, `/api/v1/restore`) the
credentials are checked before any file is stored. The answer, including a `401`, arrives after
the whole upload was sent. When a restore changes the username or password, the next request must
use the restored credentials.

In setup mode AWTRIX permits only the setup page, device/setup status reads, Wi-Fi setup,
[reboot](#post-apiv1devicereboot) and [backup restore](#post-apiv1restore). Logs, scripts, file
routes, secrets export and firmware upload answer `403 forbidden`. See the exact [setup allow-list](errors.md#provisioning-lockdown-403).

```bash
curl -u admin:secret http://<awtrix-ip>/api/v1/device
```

## Requests from other web pages {#cross-origin}

A web page on another site, such as the [AWTRIX Hub](https://awtrix.de) or your own dashboard, can
read and control AWTRIX from your browser. Answers carry `Access-Control-Allow-Origin: *`, and a
preflight `OPTIONS` request answers `204` with the allowed methods and headers and
`Access-Control-Allow-Private-Network: true`.

Five requests read, replace or erase the passwords or the firmware. Only AWTRIX's own web UI and
programs that are not browsers, such as `curl` or Home Assistant, can use them:

* `GET /api/v1/system?secrets=1`
* `PUT /api/v1/system`
* `POST /api/v1/restore`
* `POST /update`
* `POST /api/v1/device/factory-reset`

Their answers carry no `Access-Control-Allow-Origin`. When a page of another site sends one of them
(its `Origin` header names another host, or `Sec-Fetch-Site` is neither `same-origin` nor `none`),
AWTRIX answers `403 forbiddenOrigin` and changes nothing:

```json
{"error":{"code":"forbiddenOrigin","message":"only from this device's web page"}}
```

When the web login is on, it is checked first.

## Content-Type

Send `Content-Type: application/json` with every JSON body. See
[Conventions](conventions.md#content-type-is-mandatory). A `PUT` or `PATCH` with a different
`Content-Type` answers **`415 unsupportedMediaType`**. Script source uploads and the uploads
listed above use their own formats.

An empty body or `{}` never clears anything. These three routes answer **`422 validationFailed`**
to it:

| Route | Empty / `{}` body |
|---|---|
| `PUT /api/v1/apps/pushed/{name}` | `422`: `body required` |
| `PUT /api/v1/display/moodlight` | `422`: `body required` |
| `PUT /api/v1/indicators/{id}` | `422`: `body required` |

To remove an app, turn the mood light off or clear an indicator, use the `DELETE` route (or the
empty-payload message over MQTT). For a pushed app that is
[`DELETE /api/v1/apps/{name}`](#delete-apiv1appsname). The `pushed` path itself answers `405` to
anything but `PUT`. See [Errors](errors.md#content-type-the-empty-body-trap).

## Method override

Some clients can only send `GET` and `POST`: for example the FRITZ!Box HTTP action and some
home-automation gateways. For them, AWTRIX accepts this header on a `POST`:

```http
X-HTTP-Method-Override: PATCH
```

| | |
|---|---|
| Carrier method | `POST` only. The header on any other method is `400 invalidMethodOverride`. |
| Accepted values | `PUT`, `PATCH`, `DELETE`, in any upper/lower case, spaces around it ignored. Anything else (including `GET` and `POST`) is `400 invalidMethodOverride`. |
| Effect | The request is handled exactly like one sent with that method: same route, same `405` lists, same `Content-Type` rule, same errors. |
| Exception | `PUT /api/v1/apps/script/{name}` (script source upload) cannot be reached this way and answers `400 invalidMethodOverride`. Use a real `PUT`. |

A missing or empty header changes nothing.

An overridden `PATCH` or `PUT` needs `Content-Type: application/json` like a real one, or it
answers `415`.

Turn the display off from a client without `PATCH`:

```bash
curl -X POST http://<awtrix-ip>/api/v1/display \
  -H "X-HTTP-Method-Override: PATCH" \
  -H "Content-Type: application/json" \
  -d '{"power":false}'
```

The uploads `/update`, `/api/v1/files`, `/api/v1/audio/mp3` and `/api/v1/restore` do not take the
header. A valid override there answers `405 methodNotAllowed`, a malformed one
`400 invalidMethodOverride`. So **`DELETE /api/v1/files?path=...` needs a real `DELETE`**.

## Errors

Every failed request answers with the error body described in
[Conventions](conventions.md#errors). All codes, statuses and messages are listed in
**[Errors](errors.md#error-codes)**. Some availability and update errors differ by device.

### Status mapping for command routes

Routes that change something answer when the change is done:

| Outcome | Status | Body |
|---|---|---|
| Ok | 200 | `{"ok":true}` |
| Parse error | 400 | `invalidJson`, message `invalid JSON` |
| Validation error | 422 | `validationFailed`, a message and the `field` at fault |
| Not found | 404 | `notFound`: `app not found` / `nothing called "<name>"` / `not found` |
| Capacity | 507 | `insufficientStorage`, message `storage full` |
| Unavailable | 503 | `unavailable`, message `not available` |
| Busy | 503 | `serviceBusy`, message `busy, try again` (with `Retry-After: 2`) |
| Failed / unknown | 500 | `internalError`, message `command failed` |

The one exception is `PATCH /api/v1/settings`: on success it returns **all settings** instead of
`{"ok":true}`.

---

## Device

### GET /api/v1/device

Reads the device state and statistics.

**Request:** no body.

**Response:** `200` with these keys:

| Key | Type | Units / range | Meaning |
|---|---|---|---|
| `version` | string | - | firmware version, for example `1.0.12` |
| `uid` | string | - | unique device id |
| `boardType` | string | - | <!-- only esp32 esp32-s3 -->`awtrixng`<!-- /only --><!-- only tc002 -->`tc002`<!-- /only -->. See [Device state](device.md) |
| `soc` | string | - | <!-- only esp32 -->`esp32`<!-- /only --><!-- only esp32-s3 -->`esp32s3`<!-- /only --><!-- only tc002 -->the processor type, for example `armv7l`<!-- /only --> |
| `updateImage` | string | - | the update file `POST /update` accepts on this device: <!-- only esp32 -->`firmware-awtrix-ng.bin`<!-- /only --><!-- only esp32-s3 -->`firmware-awtrix-ng-s3-octal.bin` or `firmware-awtrix-ng-s3-quad.bin`<!-- /only --><!-- only tc002 -->`awtrix-ng-tc002.awup`<!-- /only --> |
| `ipAddress` | string | - | current IP address in your network |
| `hostname` | string | - | the name AWTRIX answers to, also over mDNS. Made from the device id when not set in `/api/v1/system`. See [Device state](device.md) |
| `wifiRssi` | integer | dBm | Wi-Fi signal strength |
| `uptimeSeconds` | integer | s | time since start |
| `resetReason` | string | - | why AWTRIX last started. See [Device state](device.md) |
| `freeHeapBytes` | integer | bytes | free memory |
| `minFreeHeapBytes` | integer | bytes | lowest free memory since start. See [Device state](device.md) |
<!-- only esp32 esp32-s3 -->
| `largestFreeBlockBytes` | integer | bytes | largest free memory block. See [Device state](device.md) |
<!-- /only -->
<!-- only esp32-s3 -->
| `psramTotalBytes` | integer | bytes | extra memory (PSRAM). Not sent on boards without it |
| `psramFreeBytes` | integer | bytes | free PSRAM. Do not add it to `freeHeapBytes`. See [Device state](device.md) |
<!-- /only -->
| `scriptingRunning` | boolean | - | whether scripts run. `false` while `scriptingEnabled` is off |
| `scriptHeapPool` | string | - | memory scripts use: <!-- only esp32 -->`internal`<!-- /only --><!-- only esp32-s3 -->`internal`, or `psram` on a board with PSRAM<!-- /only --><!-- only tc002 -->`system`<!-- /only --> |
| `scriptHeapBudgetBytes` | integer | bytes | memory all scripts may use together before new installs are refused |
| `fps` | integer | frames/s | measured display refresh rate |
| `brightness` | integer | 0–255 | **actual** brightness after auto-brightness, not the setting |
| `matrixPower` | boolean | - | `false` while the display is switched off |
| `currentApp` | string | - | name of the app shown |
| `indicators` | array | 3 entries | `{on: bool, color: "#RRGGBB", blinkMs: int, fadeMs: int}`: what is on the display. See [Indicators](#indicators) |
| `messageCount` | integer | - | **MQTT** commands received since start. HTTP requests are not counted |
| `wifi` | object | - | connection to your network. See [Device state → Connection status](device.md#connection-status) |
| `mqtt` | object | - | connection to the MQTT broker. See [Device state → MQTT connection](device.md#connection-status) |
| `mirror` | object | - | what [mirroring](../guides/mirroring.md) is doing: `source` and `state`. See [Device state → Mirroring status](device.md#mirroring-status) |
<!-- only tc002 -->
| `update` | object | - | the web update, with `state`, `release` and `error`. See [Device state → Update fields](device.md#update-fields-conditional) |
<!-- /only -->

These keys appear only when the hardware is there:

| Key | Type | Units | Present when |
|---|---|---|---|
<!-- only esp32 esp32-s3 -->
| `lightLevel` | number | 0–100 % | `pinLdr >= 0`: relative ambient light, 1 decimal. Not lux |
| `ldrRaw` | integer | 0–4095 | `pinLdr >= 0`: raw light-sensor reading behind `lightLevel` |
| `batteryPercent` | integer | 0–100 % | `pinBattery >= 0` |
| `batteryVoltage` | number | V, 2 decimals | `pinBattery >= 0`: voltage of the battery |
| `batteryPinMillivolts` | integer | mV | `pinBattery >= 0`: voltage at the **pin**, before the divider. Use it to calibrate `batteryDividerRatio` |
| `lowBattery` | boolean | - | `pinBattery >= 0`: `true` while `batteryPercent` is below `lowBatteryThreshold`. Always `false` when the threshold is `0` |
| `temperature` | number | °C | an I²C sensor was found: every supported sensor measures temperature |
| `humidity` | number | % | the sensor measures humidity: not sent for temperature-only sensors such as the BMP280 |
| `pressureHpa` | number | hPa, 1 decimal | the sensor measures air pressure (BMP280/BME280) |
<!-- /only -->
<!-- only tc002 -->
| `batteryPercent` | integer | 0–100 % | the battery has reported its charge |
| `batteryVoltage` | number | V, 2 decimals | the battery has reported its charge. The voltage of the battery |
| `lowBattery` | boolean | - | the battery has reported its charge. `true` while `batteryPercent` is below `lowBatteryThreshold`. Always `false` when the threshold is `0` |
| `usbPower` | boolean | - | the clock has reported its power supply. `true` while it is on USB power and charges |
<!-- /only -->

<!-- only esp32 esp32-s3 -->
`pinBattery: -1` removes all four battery keys, and `pinLdr: -1` removes both light keys. To know
in advance which sensor keys to expect, read
[`GET /api/v1/capabilities`](#get-apiv1capabilities).
<!-- /only -->

**Errors:**

| Status | Condition |
|---|---|
| 401 | authentication is on and the credentials are missing or wrong |

```bash
curl http://<awtrix-ip>/api/v1/device
```

### GET /api/v1/version

Reads the firmware version as JSON.

**Request:** no body.

**Response:** `200`:

```json
{"version":"1.0.12"}
```

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl http://<awtrix-ip>/api/v1/version
```

### GET /version

Reads the firmware version as plain text, handy for shell scripts and health checks.

**Request:** no body.

**Response:** `200` `text/plain`. The body is only the version string, for example `1.0.12`.

**Errors:** only `GET` is answered.

```bash
curl http://<awtrix-ip>/version
```

### POST /api/v1/device/reboot

Restarts AWTRIX.

**Request:** no body.

**Response:** `200` `{"ok":true}`, sent before the restart. To know when AWTRIX is back, poll
[`GET /version`](#get-version) until it answers.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: POST` |

This route also works in setup mode, like [`PUT /api/v1/system`](#put-apiv1system). The
reboot applies the Wi-Fi credentials you wrote there. See [Connect to Wi-Fi](../getting-started/first-boot.md#step-3-reboot).

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

<!-- only esp32 esp32-s3 -->
### POST /api/v1/device/sleep

Puts AWTRIX into deep sleep for a set time. It then wakes up and starts normally.

**Request:**

| Key | Type | Range | Default | Units | Required |
|---|---|---|---|---|---|
| `durationMs` | integer | `> 0` | - | ms | yes |

**Response:** `200` `{"ok":true}`, sent before AWTRIX sleeps. The display goes dark.

The select button ends the sleep early only if `pinBtnSelect` is one of the chip's `rtc` pins. See [`gpio` in capabilities](#gpio-what-the-chip-can-do). On any other pin AWTRIX wakes when
`durationMs` has passed.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 422 | `durationMs` missing, not an integer, or `<= 0`: `field: "durationMs"`, message `must be an integer > 0` |
| 405 | wrong method: `allowed: POST` |

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/sleep \
  -H "Content-Type: application/json" \
  -d '{"durationMs":60000}'
```
<!-- /only -->

### POST /api/v1/device/factory-reset

Erases everything (settings, device configuration, all files and the Wi-Fi credentials) and
restarts in setup mode.

**Request:** no body. This command exists over HTTP only, not over MQTT.

**Response:** `200` `{"ok":true}`, sent before the reset.

**Errors:**

| Status | Condition |
|---|---|
| 403 | `forbiddenOrigin`: the request came from another website ([details](#cross-origin)) |
| 405 | wrong method: `allowed: POST` |

!!! warning "This erases everything, and it cannot be undone"
    There is no confirmation step. Your icons, melodies, palettes and scripts are deleted along
    with the settings and the Wi-Fi credentials. Take a backup first if you want to keep any of it.

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/factory-reset
```

---

## Settings

### GET /api/v1/settings

Reads all settings.

**Request:** no body.

**Response:** `200` with every setting of this device, always present. Choice fields come back as their name,
colors as `"#RRGGBB"`, and an unset nullable color as `null`. What each key means:
[Settings](settings.md).

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET, PATCH` |

```bash
curl http://<awtrix-ip>/api/v1/settings
```

### PATCH /api/v1/settings

Changes one or more settings.

**Request:** any subset of the keys from [Settings](settings.md). All keys are checked first. If
one is wrong, nothing is changed.

**Response:** `200` with **all settings**, as they are after the change: not `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 415 | body sent with a non-JSON `Content-Type` (`unsupportedMediaType`) |
| 422 | a field is not valid: `field` names it |
| 405 | wrong method: `allowed: GET, PATCH` |

An unknown key is **rejected**, not ignored: `field: "<key>"`, message `unknown field`.

See [Settings validation](errors.md#patch-apiv1settings) for the message returned for each kind of field.

#### Fields

Type, range, default and meaning of every key: [Settings](settings.md). Points that matter when you
send them here:

* `null` on the five per-app `*Color` fields means **use `textColor`**. On `colorCorrection` and
  `colorTint` it means **off**. Both are the default.
<!-- only esp32 esp32-s3 -->
* Panel size and wiring are **not** settings: they are
  [system configuration](system.md#panel-and-orientation).
<!-- /only -->
* `volume` is the master volume, and <!-- only esp32-s3 tc002 -->`radioVolume`, <!-- /only -->`appVolume` and `alertVolume` are shares of
  it. `0` is silence. See [Sound](settings.md#sound).
* `transitionEffect` accepts any upper/lower case: `"slide"`, `"Slide"` and `"SLIDE"` are the
  same. The names are in [`GET /api/v1/capabilities`](#get-apiv1capabilities). The other name
  fields (`transitionDirection`, `timeSeparatorMode`, `dateOrder`, `dateSeparator`,
  `dateYearMode`) accept any case too. Answers always use one fixed spelling.
  `transitionDirection: "reverse"` only reverses the direction of the animation. It never changes
  the app order.
* `scroll` sets how text moves on all apps: `mode` (`static` · `wrap` · `loop` · `bounce`),
  `direction` (`left` · `right`), `entry` (`inline` · `offscreen`), `whenFits`
  (`static` · `scroll`), `speed` in percent of 21 px/s, `gap` in pixels and `holdMs` in
  milliseconds. Only the fields you send change, so `{"scroll":{"mode":"loop"}}` keeps the speed.
  `{"scroll":"loop"}` is short for the same. A negative number, an unknown value or an unknown
  field is `422 validationFailed` with `field` naming the key, for example `scroll.speed`. An app's own
  `scroll` overrides this field by field. See [Payload → Scrolling](payload.md#scrolling).
* `weekdayBar` sets the weekday bar: `show`, `startOnMonday`, `weekendDays` (lowercase English
  day names, any subset, `[]` for no weekend) and four colors: `activeColor` / `inactiveColor`
  for a workday, `weekendActiveColor` / `weekendInactiveColor` for a weekend day: "active" is
  today, "inactive" the other days. The weekend colors start equal to the workday colors. Only the
  fields you send change, so `{"weekdayBar":{"weekendDays":["friday","saturday"]}}` leaves the
  other six alone. An unknown field, a wrong type or an unknown day name is
  `422 validationFailed` with `field` naming the key, for example `weekdayBar.weekendDays`.
  `startOnMonday` only changes the display order. Which days are weekend days does not change.
  Do not confuse it with `dateShowWeekday`, which puts a weekday name in front of the date.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"brightness":80,"autoBrightness":false,"timeColor":"#00FF00"}'
```

### POST /api/v1/settings/reset

Resets all settings to their defaults and restarts. The system configuration, such as Wi-Fi and
MQTT, stays as it is.

**Request:** no body.

**Response:** `200` `{"ok":true}`, sent before the reset.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: POST` |

```bash
curl -X POST http://<awtrix-ip>/api/v1/settings/reset
```

---

## Display

### GET /api/v1/display

Reads the display state: power, brightness, overlay and mood light.

**Request:** no body.

**Response:** `200`:

| Key | Type | Meaning |
|---|---|---|
| `power` | boolean | `false` while the display is switched off |
| `brightness` | integer 0–255 | actual brightness after auto-brightness |
| `overlay` | string \| null | name of the weather overlay on all apps, `null` when none |
| `overlaySettings` | object | always present: `{speed, palette, blend}` for that overlay. `palette` is `null` when unset. See [Weather overlays](visuals.md#weather-overlays) |
| `moodlight` | object \| null | `{color: "#RRGGBB", brightness: 0–255}`, or `null` when off |

```bash
curl http://<awtrix-ip>/api/v1/display
```

### PATCH /api/v1/display

Switches the display on or off and sets a weather overlay for all apps.

**Request:** all fields optional. If one field is wrong, nothing changes.

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `power` | boolean | - | unchanged | `false` switches the display off |
| `overlay` | string \| null | an overlay name, `""`, or `null` | unchanged | weather overlay over **all** apps |
| `overlaySettings` | object | `{speed, palette, blend}` | unchanged | fine-tunes that overlay. See [Weather overlays](visuals.md#weather-overlays) |

`overlay` accepts any upper/lower case. `null` or `""` removes it. The names (also listed in
[`GET /api/v1/capabilities`](#get-apiv1capabilities)) are `drizzle`, `frost`, `rain`, `snow`,
`storm`, `thunder`.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 415 | body sent with a non-JSON `Content-Type` (`unsupportedMediaType`) |
| 422 | `field: "power"`, `must be a boolean` |
| 422 | `field: "overlay"`, `must be a string or null` |
| 422 | `field: "overlay"`, `unknown overlay` |
| 422 | `field: "overlaySettings"`, `must be an object` |
| 422 | `field: "overlaySettings.palette"`, `unknown palette` |
| 405 | wrong method: `allowed: GET, PATCH` |

The per-app `overlay` field of [`PUT /api/v1/apps/pushed/{name}`](#put-apiv1appspushedname) accepts
the same names.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H "Content-Type: application/json" \
  -d '{"power":true,"overlay":"snow"}'
```

### PUT /api/v1/display/moodlight

Fills the whole display with one color.

**Request:** all fields optional, but the body must not be empty.

| Key | Type | Range | Default | Units |
|---|---|---|---|---|
| `kelvin` | integer | limited to 1000–40000 | - | K |
| `color` | color | any color form | unchanged | - |
| `brightness` | integer | 0–255, **not checked**. See below | unchanged | - |

* If you send both `kelvin` and `color`, **`kelvin` wins and `color` is ignored.**
* **Both values are kept.** A field you leave out keeps its value: `{"brightness":30}` dims
  without changing the color, `{"color":"#FF0000"}` changes the color without changing the
  brightness.
* The first time, the mood light is white at brightness `120`.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 415 | body sent with a non-JSON `Content-Type` (`unsupportedMediaType`) |
| 422 | `field: "color"`: the value is not a valid color. Nothing changes |
| 422 | empty or `{}` body: `body required` |
| 405 | wrong method: `allowed: PUT, DELETE` |

!!! warning "`brightness` is not range-checked"
    A value above 255 is not refused. It wraps around: `300` becomes `44`, `256` becomes `0`.
    Keep it inside 0–255 yourself.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/display/moodlight \
  -H "Content-Type: application/json" \
  -d '{"kelvin":2700,"brightness":90}'
```

### DELETE /api/v1/display/moodlight

Turns the mood light off.

**Request:** no body.

**Response:** always `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: PUT, DELETE` |

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/display/moodlight
```

### GET /api/v1/display/screen

Reads the picture currently on the display, pixel by pixel.

**Request:** no body.

**Response:** `200`:

| Key | Type | Meaning |
|---|---|---|
<!-- only esp32 esp32-s3 -->
| `width` | integer | display width, `32` by default |
| `height` | integer | display height, `8` |
| `pixels` | array of integers | `width * height` entries (256 by default), row by row from the top left |
<!-- /only -->
<!-- only tc002 -->
| `width` | integer | display width, `52` |
| `height` | integer | display height, `16` |
| `pixels` | array of integers | `width * height` entries (832), row by row from the top left |
<!-- /only -->

Each pixel is the color `0xRRGGBB` written as a **decimal number**, not hex.

These are the colors the apps drew. Brightness and the display color settings (`saturation`,
`gamma`, `colorCorrection`, `colorTint`) change what the LEDs show, not what this route returns.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl http://<awtrix-ip>/api/v1/display/screen
```

---

## Apps

All apps are in one list. Each app has a **name** and an `origin` that says where it comes from:

| `origin` | Content comes from | Stored in | Kept after a restart |
|---|---|---|---|
| `builtin` | AWTRIX itself | - | - |
| `pushed` | JSON you send from outside | memory only | no |
| `script` | a Berry script stored on AWTRIX | `/SCRIPTS` | yes |
| `module` | Berry code other scripts import. Not an app of its own | `/SCRIPTS` | yes |

Pushed apps and scripts are written through their own routes (`/api/v1/apps/pushed/{name}` and `/api/v1/apps/script/{name}`) because they carry different
content. Listing, ordering and removing work the same for all apps.

<!-- only esp32 esp32-s3 -->
Built-in apps are `Time`, `Date`, `Temperature`, `Humidity` and `Battery`. No setting switches them
on or off, but three need hardware: `Temperature` needs an I²C sensor, `Humidity` a sensor that
measures humidity, and `Battery` a configured battery pin.
<!-- /only -->
<!-- only tc002 -->
Built-in apps are `Time` and `Status`. No setting switches them on or off.
<!-- /only -->

### Names

Every app name, pushed or script, must match `[A-Za-z0-9_-]{1,32}`: 1 to 32 letters, digits,
`_` or `-`. `active`, `next`, `previous` and `order` are taken by the routes of the same name and
cannot be app names. A wrong name answers **`400 invalidName`** (`invalid name`,
`field: "name"`) before the body is read, and nothing is stored.

The method is checked before the name: `PATCH /api/v1/apps/pushed/bad.name` answers `405`. Only
`DELETE /api/v1/apps/{name}` checks the name first. The paths without a name,
`/api/v1/apps/pushed/` and `/api/v1/apps/script/`, are `400 invalidName` (not `405`) with any
method. A `PUT` to them with a `Content-Type` other than `application/json` answers `415` first,
because without a name the path is no script upload. All checks in order:
[Errors → Order of the checks](errors.md#order-of-the-checks).

A script cannot use the name of a built-in app such as `Time`: that answers
**`422 validationFailed`** with the reason in `message`, and nothing is stored.

`active`, `next`, `previous` and `order` are reserved: `DELETE /api/v1/apps/next` is a `405`, not
the deletion of an app called `next`.

### GET /api/v1/apps

Lists all apps: first the ones you arranged, in that order, then all others.

**Request:** no body.

**Response:** `200` with an array. Three keys describe the state of each app: `enabled` (it runs),
`inLoop` (it is shown in the rotation) and `present` (it exists on AWTRIX right now). For most apps
all three are the same. Two exceptions: a
[background script](../guides/scripting/several-apps.md#running-without-ever-being-shown) (`@headless true`)
runs but is never shown, and a [pushed app](../guides/pushed-apps.md) that was deleted keeps its
place until the next push.

| Key | Type | Meaning |
|---|---|---|
| `name` | string | app name |
| `enabled` | boolean | whether it runs |
| `inLoop` | boolean | whether it is in the rotation |
| `present` | boolean | whether the app exists on AWTRIX right now. `false` for a name that only keeps a place for an app that is not there |
| `slot` | integer \| null | position in your order, counting from 0. `null` when the app has no position of its own |
| `origin` | string \| null | `"builtin"`, `"pushed"`, `"script"`, `"module"`. `null` while `present` is `false` |
| `import` | string | **module only**: the name scripts write in their `import` line |
| `icon` | string | **pushed only**, and only when the app has an `icon` |
| `skipped` | boolean | **script only**: the app's own [`should_show()`](../guides/scripting/several-apps.md#sitting-a-round-out) last said no, so the rotation skips it |
| `headless` | boolean | **script only**: the script has `@headless true` and never draws |
| `ondemand` | boolean | **script only**: the script has [`@ondemand`](../guides/scripting/several-apps.md#start-from-the-device-menu): it is not in the rotation and runs only when started. `inLoop` is `true` while it runs |
| `config` | boolean | **built-ins, scripts and modules**: it has settings you can change: a built-in app under [`/builtin/{name}/config`](#get-apiv1appsbuiltinnameconfig), a script or module under [`/config`](#get-apiv1appsnameconfig). On a [module](../guides/scripting/several-apps.md#settings-several-apps-share) these settings are shared by every app that imports it |
| `error` | object \| null | **script only**: `null` while it works, otherwise the error it stopped on (see [below](#the-error-object)) |
| `meta` | object | **script only**: `{name, desc, author, version, icons, requires, needs, display}` from the `@` headers. Each text is `""` when its header is missing. `icons` lists the icon IDs from [`@icons`](../guides/scripting/drawing.md#the-icons-your-script-needs), `[]` when missing. `requires` lists the [`@requires`](../guides/scripting/sharing.md#scripts-your-script-needs) lines as `{name, hub?, missing}`. `needs` lists the [`@needs`](../guides/scripting/sharing.md#what-your-script-asks-of-the-clock) names as `{name, missing}`. `missing` is `true` while this display does not have it. `display` is `{width, height, fits}` from `@display`, or `null` when the script runs on any display. The script is installed and runs either way |

`icon`, `import`, `skipped`, `config`, `error` and `meta` are **left out**, not sent empty, where
they do not apply. On a firmware without scripting, `skipped`, `error` and `meta` are never sent,
and `config` only on built-in apps.

A [module](../guides/scripting/several-apps.md#sharing-code-between-scripts) is listed because it shares the
script routes, but it is not an app. Besides `origin` it has only `name`, `import`, `error` and
`meta`: no `enabled`, `inLoop` or `slot`, because it is never shown. Read, write and delete it
under `/api/v1/apps/script/{name}` like a script. An import name that cannot be used (or that
another module or a built-in module already has) answers **`422 validationFailed`** with the
reason in `message`, and nothing is stored.

`skipped` is not the opposite of `inLoop`. `inLoop: false` means the app is not in the rotation at
all. `skipped: true` means the app is in the rotation but declined its last turn. It can change
that by itself at any moment.

```json
[
  {"name":"Time","enabled":true,"inLoop":true,"slot":0,"present":true,"origin":"builtin","config":true},
  {"name":"weather","enabled":true,"inLoop":true,"slot":1,"present":true,"origin":"pushed","icon":"1"},
  {"name":"doorbell","enabled":true,"inLoop":false,"slot":2,"present":true,"origin":"script",
   "skipped":false,"headless":true,"config":false,"error":null,
   "meta":{"name":"Doorbell","desc":"","author":"me","version":"1.0","icons":[],"requires":[],
     "needs":[{"name":"audio.rtttl","missing":false}],"display":null}},
  {"name":"co2","enabled":true,"inLoop":false,"slot":3,"present":false,"origin":null},
  {"name":"clock","enabled":false,"inLoop":false,"slot":null,"present":true,"origin":"script",
   "skipped":false,"headless":false,"config":true,"error":null,
   "meta":{"name":"Wall Clock","desc":"","author":"me","version":"1.2","icons":["sun","moon"],
     "requires":[],"needs":[],"display":{"width":64,"height":32,"fits":false}}}
]
```

Here `doorbell` runs but is never shown, `co2` keeps a place for an app that is not there, and
`clock` is switched off.

#### The error object

This is how a script error looks: here and in the answer to
[`PUT /api/v1/apps/script/{name}`](#put-apiv1appsscriptname).

| Key | Type | Meaning |
|---|---|---|
| `message` | string | the error text, without the line number |
| `line` | integer | **optional**: line in the script source, counting from 1 |
| `hook` | string | **optional**: which function failed: `setup`, `loop`, `draw`, `on_show`, `on_hide`, `on_button`, `should_show` |

`line` and `hook` are left out when unknown, and often both are: `"no draw() method"` has
neither. A syntax error usually has a `line`. An error with a `hook` usually has no `line`.

```json
{"message":"syntax_error: unexpected token ')'","line":12}
{"message":"runtime_error: operand must be number","hook":"setup"}
```

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl http://<awtrix-ip>/api/v1/apps
```

### PUT /api/v1/apps/active

Shows a specific app now.

**Request:**

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | - | name of the app to show |
| `fast` | boolean | `false` | `true` switches at once, `false` plays the transition |

A body that does **not** start with `{` is read as the app name itself. A **broken** JSON body is
also read as a name, so it answers **404 `app not found`**, not 400.

The name of an [`@ondemand`](../guides/scripting/several-apps.md#start-from-the-device-menu) script starts it,
as picking it in the device menu does. Showing any other app, `next` or `previous` ends it again.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 404 | `notFound`, `app not found` |
| 405 | wrong method: `allowed: PUT` |
| 503 | `serviceBusy`: an on-demand script could not start while a download runs. Try again |
| 507 | `insufficientStorage`: not enough free memory to start an on-demand script |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H "Content-Type: application/json" \
  -d '{"name":"Time","fast":true}'
```

### POST /api/v1/apps/next

Switches to the next app in the rotation.

**Request:** no body.

**Response:** always `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: POST` |

```bash
curl -X POST http://<awtrix-ip>/api/v1/apps/next
```

### POST /api/v1/apps/previous

Switches back to the previous app.

**Request:** no body.

**Response:** always `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: POST` |

```bash
curl -X POST http://<awtrix-ip>/api/v1/apps/previous
```

### PUT /api/v1/apps/order

Sets which apps are switched on, and the order in which they are shown.

**Request:** an object with two lists of app names:

| Key | Meaning |
|---|---|
| `order` | the apps that run, in the order they are shown |
| `disabled` | the apps that are switched off |

**`disabled` is always required. `order` is optional, but only together with `disabled`.**

| Body | Effect |
|---|---|
| `{"order":[…],"disabled":[…]}` | sets both |
| `{"disabled":[…]}` | sets which apps are off, keeps the order |
| `{"order":[…]}` | **400** |
| `["Time","weather"]` | **400**: the body must be an object |

**`disabled` is the complete list of switched-off apps.** Every app it does not name is switched
on, also one that was off before. To switch one app on or off and leave the others as they are, use
[PUT /api/v1/apps/{name}/enabled](#put-apiv1appsnameenabled).

* **Duplicates count** in `order`: list an app twice and it is shown twice per round, each time
  with its own `slot`.
* A switched-off **script** does nothing at all: no `loop()`, no HTTP or MQTT callbacks. To keep
  a [background script](../guides/scripting/several-apps.md#running-without-ever-being-shown) running, put it in
  `order` like any other app. The rotation skips it because it never draws.
* A name in `order` **keeps its place** even if the app does not exist yet. When the app appears,
  it takes that place. Any other new app is added after the ordered ones, switched on.
* A name in `disabled` stays switched off while the app is not there.
* Such names are listed in `GET /api/v1/apps` with `present: false`. Deleting a **script** is the
  exception: it removes the name from both lists.
* Both lists are kept after a restart.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | not valid JSON, a body that is not an object, or one without `disabled` (`invalidJson`) |
| 405 | wrong method: `allowed: PUT` |
| 507 | `insufficientStorage`, `applied, not saved yet`: active until restart, but saving failed |

Free storage and repeat the request before rebooting after a 507.<!-- only tc002 --> AWTRIX also
retries saving by itself.<!-- /only -->

<!-- only esp32 esp32-s3 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","weather","Time","Date"],"disabled":["Battery"]}'
```

Keep the order, switch Battery off and every other app on:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"disabled":["Battery"]}'
```
<!-- /only -->
<!-- only tc002 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","weather","Time","Status"],"disabled":["doorbell"]}'
```

Keep the order, switch Status off and every other app on:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"disabled":["Status"]}'
```
<!-- /only -->

### PUT /api/v1/apps/{name}/enabled

Switches one app on or off. Every other app stays as it is.

**Request:** `{name}` is the name of the app. The body is `true` to switch it on or `false` to switch
it off.

* A switched-off app keeps its place in the order. Switched on again, it returns to that place.
* The name may belong to an app that is not there yet. When the app arrives, it is on or off as set.
* The switch is kept after a restart.

**Response:** `200` `{"ok":true}`, also when the app already was on or off.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: the name is not a valid app name |
| 405 | wrong method: `allowed: PUT` |
| 422 | `validationFailed`, `must be true or false`: the body is anything else |
| 507 | `insufficientStorage`, `applied, not saved yet`: active until restart, but saving failed |

<!-- only esp32 esp32-s3 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/Battery/enabled \
  -H "Content-Type: application/json" -d 'false'
```
<!-- /only -->
<!-- only tc002 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/Status/enabled \
  -H "Content-Type: application/json" -d 'false'
```
<!-- /only -->

### PUT /api/v1/apps/pushed/{name}

Creates a pushed app, or replaces the one with that name.

**Request:** `{name}` is everything after `/api/v1/apps/pushed/`. The body is an app payload:
`text`, `icon`, `color`, `draw`, `effect`, charts and more: all fields are in
[App & notification payload](payload.md).

| Body | Effect |
|---|---|
| object | stores one app named `{name}` |
| array of objects | stores each element as `{name}0`, `{name}1`, … (elements that are not objects are skipped) |
| empty or `{}` | `422`. Use `DELETE /api/v1/apps/{name}` to remove the app |
| anything else | `400 invalidJson` |

The body is accepted whole or not at all. An unknown `effect` or `overlay` name (matched in any
upper/lower case against [`GET /api/v1/capabilities`](#get-apiv1capabilities)) rejects the whole
request, and for an array the whole batch.

A pushed app is kept in memory only. It stays until you replace or delete it, its `lifetimeMs`
runs out, or AWTRIX restarts. For content that must come back after a restart, write a
[script](#scripts).

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | not valid JSON, or a single value instead of an object or array (`invalidJson`) |
| 400 | `invalidName`: `{name}` is not `[A-Za-z0-9_-]{1,32}`, or missing (`/api/v1/apps/pushed/`) |
| 415 | body sent with a non-JSON `Content-Type` (`unsupportedMediaType`) |
| 422 | empty or `{}` body: `body required` |
| 422 | `field: "<key>"`: an unknown key, an invalid color, an unknown mode word, a wrong `scroll`, a wrong `draw` command, or an `effect`/`overlay` name AWTRIX does not know |
| 507 | a new app would go past the [pushed-app limit](limits.md#apps-and-notifications) of 50 (`insufficientStorage`). An array is stored whole or not at all |
| 405 | wrong method: `allowed: PUT` |

A `415` or `422` never deletes or changes the existing app.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"21.5C","icon":"2422","textColor":"#00AAFF"}'
```

### DELETE /api/v1/apps/{name}

Removes an app of any kind.

**Request:** no body.

* **Pushed app:** removes that name **and the numbered apps an array push to `{name}` created**
  (`{name}0`, `{name}1`, …). An app you pushed to `temp1` yourself is a separate app and stays.
  The name keeps its place in `order` or `disabled`, listed with `present: false`. The next push
  puts the app back there.
* **Script:** removes the source and its saved data (the only way to reset it). Its
  [sounds](#script-sounds) stay. Delete them with
  [`DELETE /api/v1/apps/script/{name}/sounds`](#delete-apiv1appsscriptnamesounds). The name is
  removed from both lists. Installed again, the script joins at the end of the rotation, switched on,
  and uses the sounds that stayed.
* **Built-in app:** nothing happens. It stays in the rotation.

**Response:** always `200` `{"ok":true}` for a valid name: also when no such app exists. Deleting
twice is safe.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: `{name}` is not `[A-Za-z0-9_-]{1,32}` |
| 403 | `forbidden`: not allowed in setup mode |
| 405 | any other method on a valid name: `allowed: DELETE` |

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/apps/weather
```

---

## Scripts

A script is a small program in the Berry language. An installed script becomes an app in the
rotation, and its source is stored on AWTRIX, so it comes back after a restart. The language and
worked examples are in the [Scripting guide](../guides/scripting/index.md). This section lists the routes.

A script is addressed as `/api/v1/apps/script/{name}`. `{name}` is the app name and the file name
under `/SCRIPTS`, and follows the [name rule](#names). To remove a script, use
[`DELETE /api/v1/apps/{name}`](#delete-apiv1appsname), the route that removes any app. The MP3s a
script brings along are under [`/api/v1/apps/script/{name}/sounds`](#script-sounds).

`/api/v1/apps/script/{name}` answers other methods than `GET` and `PUT` with `405`,
`allowed: GET, PUT`.

### GET /api/v1/apps/script/{name}

Downloads a script's source.

**Request:** no body.

**Response:** `200` `text/plain`: the source exactly as it was installed, so you can send it
straight back with `PUT`.

Which scripts exist, whether they work, and the details from their `@` headers are in
[`GET /api/v1/apps`](#get-apiv1apps) (`origin`, `error`, `meta`), without the source.

This route also works while [`scriptingEnabled`](system.md#miscellaneous) is off, so you can read
and fix a script that made the device unreachable. See
[Scripts eat the memory](../troubleshooting/troubleshooting.md#scripts-eat-the-memory-and-awtrix-never-comes-up).

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: the name is not `[A-Za-z0-9_-]{1,32}` |
| 404 | `notFound`, `no such script` |
| 503 | `unavailable`, `no scripting`: this firmware has no scripting at all |

```bash
curl "http://<awtrix-ip>/api/v1/apps/script/clock" -o clock.ax
```

### Script updates

`PUT /api/v1/apps/script-update/{name}` replaces a script only if its stored source is still
exactly `expected_source`. [Hub script updates](../guides/hub-script-updates.md) use it. It is
available when `capabilities.scriptUpdates` is `true`.

**Request:** a JSON body:

```json
{"expected_source":"exact current source", "source":"replacement source"}
```

* `expected_source: null` creates the script only if no script of that name exists yet.
* `{name}` is 1–32 letters, digits, `_` or `-`.
* An existing script keeps the settings that still fit.
* The whole request must fit the script size limit. See [Limits](limits.md#scripting).

**Response:** `200` `{"ok":true}`, the new source is installed. If the new script fails to compile
or its `setup()` fails, the previous script is restored. Errors that happen later are reported in
the script log.

**Errors:**

| Status | Meaning |
|---|---|
| 400 | `invalidJson`: malformed JSON. `invalidName`: the name is not `[A-Za-z0-9_-]{1,32}` |
| 404 | `notFound`: no script of that name to update |
| 409 | `scriptChanged`: the source has changed, or the name for a new script already exists |
| 422 | invalid source, or the script failed to compile or set up |
| 503 | scripting not available, or busy |
| 507 | not enough room for the script |

The request schema is in the [OpenAPI description](../api/openapi.yaml).

```bash
curl -X PUT "http://<awtrix-ip>/api/v1/apps/script-update/clock" \
  -H "Content-Type: application/json" \
  -d '{"expected_source":null,"source":"class Hello\n  def draw()\n    clear()\n    text(1, 6, \"hi\", 0x00FF00)\n  end\nend\nreturn Hello()"}'
```

### PUT /api/v1/apps/script/{name}

Installs a script, or replaces the script with that name.

**Request:** the Berry source as it is: **not JSON**. Any `Content-Type` is accepted, or none.

**Response:**

| Status | Body |
|---|---|
| 200 | `{"ok":true,"name":"X","error":null}`: installed and working |
| 200 | `{"ok":true,"name":"X","error":{...}}`: installed, but it has an error. See [the error object](#the-error-object) |

!!! note "A script with an `error` is still installed"
    A script with an error **is still installed**. The source is stored and kept after a restart,
    the app joins the rotation and shows `ERR:<name>`. Always check `error`: only `null` means it
    works. The script is tried again when you send a new version, save its settings or data, or
    restart the device. If it fails again, the new error is shown.

* Replacing a script starts it **fresh**: its subscriptions, running requests and values in memory
  are dropped. Its saved data is kept, and it keeps its place in the rotation.
* While [`scriptingEnabled`](system.md#miscellaneous) is off, the script is saved and the answer is
  `{"ok":true,"name":"X","error":null}`. It is not run or checked until scripting is on again after
  a restart. This is how
  [the rescue](../troubleshooting/troubleshooting.md#scripts-eat-the-memory-and-awtrix-never-comes-up)
  works.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: the name is wrong, or missing (`/api/v1/apps/script/`) |
| 403 | `forbidden`: not allowed in setup mode |
| 422 | `validationFailed`, `body must be the script`, `field: "source"`: empty body |
| 507 | `insufficientStorage`: not enough memory. See below |

A `507` can mean several different memory limits. See [Script installation errors](errors.md#script-installation)
for the message, its meaning and the action to take.

```bash
curl -X PUT "http://<awtrix-ip>/api/v1/apps/script/clock" \
  -H "Content-Type: text/plain" --data-binary @clock.ax
```

### GET /api/v1/apps/{name}/config

Reads the [settings a script offers](../guides/scripting/storage.md#settings-the-user-can-change) and
their current values.

**Request:** no body.

**Response:** `200`. A script without settings answers with an empty `fields` list, not `404`.
[`GET /api/v1/apps`](#get-apiv1apps) shows which scripts have settings, under `config`.

```json
{
  "name": "Weather",
  "fields": [
    {"key": "lat", "type": "text", "label": "Latitude",
     "maxlen": 16, "default": "52.52", "value": "48.14"},
    {"key": "metric", "type": "bool", "label": "Celsius", "default": true, "value": true},
    {"key": "every", "type": "number", "label": "Refresh", "unit": "min",
     "min": 1, "max": 60, "default": 15, "value": 30},
    {"key": "mode", "type": "select", "label": "Show",
     "options": ["now", "today", "week"], "default": "now", "value": "today"},
    {"key": "tint", "type": "color", "label": "Color",
     "default": 16746496, "value": 65280}
  ],
  "warnings": []
}
```

| Field | Meaning |
|---|---|
| `key` | the name the script reads with `store.get(key)` |
| `type` | `bool`, `text`, `number`, `slider`, `select` or `color` |
| `label` | the label, as the script author wrote it |
| `default` | the script's default value, so an app can offer "reset" |
| `value` | the current value: the default until somebody changes it |
| `help`, `unit`, `group`, `min`, `max`, `step`, `maxlen`, `options` | only present when they apply |
| `warnings` | `@config` lines AWTRIX could not read, with their line numbers |

A `color` is a **number** from `0` to `16777215`. Every other type has the JSON type its name
suggests.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: the name is not `[A-Za-z0-9_-]{1,32}` |
| 404 | `notFound`, `no such script` |
| 503 | `unavailable`, `no scripting` |
| 507 | `insufficientStorage`, `not enough memory`, `field: "name"` |

```bash
curl "http://<awtrix-ip>/api/v1/apps/Weather/config"
```

### PATCH /api/v1/apps/{name}/config

Changes a script's settings.

**Request:** a JSON object with only the settings you want to change. Everything else keeps its
value, including other data the script stored.

* A `color` accepts the number or `"#RRGGBB"`.
* A `number` outside its `min`/`max` is **set to the nearest limit**, not refused.
* If one field is wrong, nothing is changed.

**Response:** saving restarts the script, like sending its source again: `init()` and `setup()`
run again with the new values, values in memory and subscriptions are dropped, and the app keeps
its place in the rotation.

| Status | Body |
|---|---|
| 200 | `{"ok":true,"name":"X","error":null}`: saved, and the script restarted without error |
| 200 | `{"ok":true,"name":"X","error":{...}}`: saved, but the restarted script has an error. See [the error object](#the-error-object) |

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidJson`, or `invalidName` |
| 403 | `forbidden`: not allowed in setup mode |
| 404 | `notFound`, `no such script` |
| 415 | `unsupportedMediaType`: the body must be `application/json` (unlike the source upload) |
| 422 | `validationFailed`, `unknown setting` with `field`: the script declares no such setting |
| 422 | `validationFailed` with `field`: wrong type, a `select` value not in the list, or text too long |
| 422 | `validationFailed`, `no settings`: the script declares no settings |
| 422 | `validationFailed`, `body required`: empty body |
| 503 | `unavailable`: [`scriptingEnabled`](system.md#miscellaneous) is off |
| 503 | `serviceBusy`: a script is waiting for a web request. Try again, `Retry-After: 2` |
| 507 | `insufficientStorage`: not enough free memory for the change or the restart. Nothing changed |

```bash
curl -X PATCH "http://<awtrix-ip>/api/v1/apps/Weather/config" \
  -H "Content-Type: application/json" -d '{"lat":"48.14","tint":"#00FF00"}'
```

### GET /api/v1/apps/builtin/{name}/config

Reads the settings of a built-in app, for example <!-- only esp32 esp32-s3 -->`Time`, `Date`,
`Temperature`, `Humidity` or `Battery`<!-- /only --><!-- only tc002 -->`Time`<!-- /only -->, and their current values. The web UI shows them under **Apps**.

**Request:** no body.

**Response:** `200`. [`GET /api/v1/apps`](#get-apiv1apps) shows which built-in apps have settings,
under `config`. A built-in app without settings<!-- only tc002 -->, for example `Status`,<!-- /only --> answers with
an empty `fields` list.

```json
{
  "name": "Time",
  "fields": [
    {"key": "time24h", "type": "bool", "group": "time", "path": ["time24h"],
     "default": true, "value": false},
    {"key": "timeColor", "type": "color", "group": "time", "path": ["timeColor"],
     "nullable": true, "default": null, "value": 16711680},
    {"key": "weekdayBar.weekendDays", "type": "days", "group": "weekday",
     "path": ["weekdayBar", "weekendDays"], "default": ["sunday", "saturday"],
     "value": ["saturday"]}
  ],
  "warnings": []
}
```

| Field | Meaning |
|---|---|
| `key` | the name of the setting, for example `time24h` or `weekdayBar.show` |
| `type` | `bool`, `select`, `color` or `days` (a list of weekday names) |
| `path` | where the value goes in a `PATCH`: `["weekdayBar","show"]` means `{"weekdayBar":{"show":false}}` |
| `group` | the part of the form the setting belongs to: `time`, <!-- only tc002 -->`calendar`, <!-- /only -->`date` or `weekday` |
| `options` | for `select`, the values you can choose from |
| `nullable` | `true` when the color may be `null`, which means "use the global text color" |
| `default` | the factory value |
| `value` | the current value |

A `color` is a **number** from `0` to `16777215`. `0` is black.
<!-- only esp32 esp32-s3 -->
`Time` and `Date` each have their own weekday bar under `weekdayBar`. The one of `Date` is
`dateWeekdayBar` in the [settings](settings.md#weekday-bar).
<!-- /only -->
<!-- only tc002 -->
`Time` offers `clockFace` and the date format. Its weekday bar is under `weekdayBar`.
<!-- /only -->

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: the name is not `[A-Za-z0-9_-]{1,32}` |
| 404 | `notFound`, `no such app`: this device has no such built-in app<!-- only esp32 esp32-s3 -->, its sensor is missing<!-- /only -->, or a pushed app has its name |

```bash
curl "http://<awtrix-ip>/api/v1/apps/builtin/Time/config"
```

### PATCH /api/v1/apps/builtin/{name}/config

Changes the settings of a built-in app. The change shows at once and is saved like every other
setting.

**Request:** a JSON object with only the settings you want to change, each at its `path`.
Everything else keeps its value.

* A `color` accepts the number, `"#RRGGBB"` and the other [color forms](conventions.md#colors).
  `null` is allowed only where `nullable` is `true`.
* A `days` value is a list of lowercase English day names, for example `["saturday","sunday"]`.
* If one setting is wrong, nothing is changed.

**Response:** `200` `{"ok":true,"name":"Time","error":null}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidJson`, or `invalidName` |
| 403 | `forbidden`: not allowed in setup mode |
| 404 | `notFound`, `no such app` |
| 415 | `unsupportedMediaType`: the body must be `application/json` |
| 422 | `validationFailed`, `unknown setting` with `field`: the app has no such setting on this device |
| 422 | `validationFailed` with `field`: a value of the wrong type or outside its range |
| 422 | `validationFailed`, `no settings`: the app has no settings |
| 422 | `validationFailed`, `body required`: empty body |

```bash
curl -X PATCH "http://<awtrix-ip>/api/v1/apps/builtin/Time/config" \
  -H "Content-Type: application/json" -d '{"time24h":false,"weekdayBar":{"show":false}}'
```

### GET /api/v1/apps/{name}/data

Reads what a script saved with `store.set()`: high scores, save games, last known values.

**Request:** no body.

**Response:** `200` with one JSON object, key by key. A script that saved nothing answers `{}`.

```json
{"unl": 7, "best": [12, 40, 9, 0]}
```

* The values of its [settings](#get-apiv1appsnameconfig) are not in it. They are under `/config`.
* A key the script cleared with `store.set(key, nil)` is not in it.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: the name is not `[A-Za-z0-9_-]{1,32}` |
| 404 | `notFound`, `no such script` |
| 503 | `unavailable`, `no scripting` |
| 507 | `insufficientStorage`, `not enough memory`, `field: "name"` |

```bash
curl "http://<awtrix-ip>/api/v1/apps/lemmix/data"
```

### PATCH /api/v1/apps/{name}/data

Changes what a script saved.

**Request:** a JSON object with only the keys you want to change.

* A value replaces the stored one, or adds the key.
* `null` removes the key.
* Every key you leave out keeps its value.
* A key that is one of the script's settings is refused. Change it with
  [`PATCH /config`](#patch-apiv1appsnameconfig).

**Response:** saving restarts the script, like changing its settings: `init()` and `setup()` run
again and read the new values.

| Status | Body |
|---|---|
| 200 | `{"ok":true,"name":"X","error":null}`: saved, and the script restarted without error |
| 200 | `{"ok":true,"name":"X","error":{...}}`: saved, but the restarted script has an error. See [the error object](#the-error-object) |

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidJson`, or `invalidName` |
| 403 | `forbidden`: not allowed in setup mode |
| 404 | `notFound`, `no such script` |
| 415 | `unsupportedMediaType`: the body must be `application/json` |
| 422 | `validationFailed` with `field`: the key is a setting of the script |
| 422 | `validationFailed`: the body is not a JSON object, or it is empty |
| 503 | `unavailable`: [`scriptingEnabled`](system.md#miscellaneous) is off |
| 503 | `serviceBusy`: a script is waiting for a web request. Try again, `Retry-After: 2` |
| 507 | `insufficientStorage`: not enough free memory for the change or the restart. Nothing changed |

```bash
curl -X PATCH "http://<awtrix-ip>/api/v1/apps/lemmix/data" \
  -H "Content-Type: application/json" -d '{"unl":9,"best":null}'
```

<!-- only tc002 -->
### Script sign-in

Some scripts sign in to a service with your account, see [Signing in to services](../guides/sign-in.md).
These routes hold the client ID and client secret and run the sign-in. The web UI uses them in
the app settings. They exist only while [capabilities](#get-apiv1capabilities) say
`"oauth": true`, which needs scripting switched on.

Every change needs the header `X-Awtrix-OAuth: 1` and an `Origin` header naming the device, as the
device's own web UI sends them. This keeps other web pages from changing a sign-in. Answers are
never cached.

#### GET /api/v1/oauth

Lists every script that signs in.

**Response:** `200`:

```json
{"redirectUri": "https://awtrix.de/oauth/callback",
 "apps": [{"name": "Spotify", "provider": "accounts.spotify.com", "scope": "user-read-currently-playing",
           "pkce": true, "clientId": "4f1c...", "clientSecretSet": true, "state": "signedIn"}]}
```

`redirectUri` is the address to enter in the developer app at the service.

#### GET /api/v1/oauth/{name}

One script's sign-in, as one entry of the list above.

| Key | Meaning |
|---|---|
| `provider` | the service, as the host of its sign-in page |
| `scope` | what the script asks for |
| `pkce` | `true`: the service may not need a client secret |
| `clientId` | the saved client ID, `""` when none is set |
| `clientSecretSet` | a client secret is saved. The secret itself is never returned. |
| `state` | `signedOut`, `pending` (signing in), `signedIn` or `error` |
| `error` | why the last sign-in failed, such as `invalid_grant`. Only present after a failure. |
| `invalid` | why the script's sign-in line is unusable. Only present then. |

`404` `notFound` when the script does not sign in.

#### POST /api/v1/oauth/{name}

Saves client ID and client secret. **Request:** any of `clientId`, `clientSecret`, or
`"clearSecret": true`. Fields you leave out keep their value. An empty `clientSecret` keeps the
saved one. A new client ID ends the sign-in. **Response:** `200` `{"ok":true}`.

#### POST /api/v1/oauth/{name}/start

Starts a sign-in. **Request:** `{}`. **Response:** `200` `{"url": "..."}`, the service's sign-in
page. Open it in the same browser. After you allow access, the service sends the browser back to
the clock's web UI, which finishes the sign-in. A started sign-in expires after 10 minutes.

#### POST /api/v1/oauth/{name}/code

Finishes a sign-in with what the service sent back. **Request:** `{"code": "...", "state": "..."}`.
**Response:** `202` `{"ok":true}`. A moment later `state` turns `signedIn` or `error`.

#### DELETE /api/v1/oauth/{name}

Signs out. Client ID and client secret stay. **Response:** `200` `{"ok":true}`.

**Errors of these routes:**

| Status | Condition |
|---|---|
| 400 | `invalidJson`, `invalid JSON`: the body is not a JSON object |
| 403 | `forbiddenOrigin`: not sent from the device's own web UI |
| 404 | `notFound`, `no @oauth line`: no such script, or it does not sign in. `unknown route`: a name that cannot be a script, or a path after the name other than `start` or `code` |
| 405 | `methodNotAllowed` |
| 422 | `validationFailed`: for example `client id missing` or `sign-in expired or unknown` |

The `404` comes first: a script without an `@oauth` line answers it to every method, before
`405` and `403`.

```bash
curl -X POST http://<awtrix-ip>/api/v1/oauth/Spotify \
  -H "Content-Type: application/json" \
  -H "X-Awtrix-OAuth: 1" \
  -H "Origin: http://<awtrix-ip>" \
  -d '{"clientId":"4f1c...","clientSecret":"..."}'
```
<!-- /only -->

### Back up and restore scripts

Scripts can be read and written over the HTTP API, for example by a backup script:

```bash
# read one script back, verbatim
curl http://<awtrix-ip>/api/v1/apps/script/Weather

# install or replace (the body is Berry source, not JSON)
curl -X PUT http://<awtrix-ip>/api/v1/apps/script/Weather \
  -H 'Content-Type: text/plain' --data-binary @weather.ax

# every app on the device, with origin, compile state and metadata
curl http://<awtrix-ip>/api/v1/apps

# the settings a script offers, with their current values
curl http://<awtrix-ip>/api/v1/apps/Weather/config

# change one of them (the app restarts with the new value)
curl -X PATCH http://<awtrix-ip>/api/v1/apps/Weather/config \
  -H 'Content-Type: application/json' -d '{"lat":"48.14"}'
```

`GET` returns the source exactly as stored and `PUT` takes it exactly as sent, so backing up every
script is a simple shell loop.

**A script that fails to compile still installs.** `PUT` answers `200`, the app appears in the
rotation, and the `error` field in the answer holds the error message and line. Check that field,
or a typo looks like a working upload.

The source does not include the script's sounds. See [Script sounds](#script-sounds).
<!-- only tc002 -->
Sign-ins are not part of the script and not part of a backup. After a restore, sign in again.
<!-- /only -->

### Removing a script

Use `DELETE /api/v1/apps/{name}`, the same route that removes a pushed app. It deletes the source,
the saved data and the app in the rotation. The script's [sounds](#script-sounds) stay.
[`DELETE /api/v1/apps/script/{name}/sounds`](#delete-apiv1appsscriptnamesounds) removes them.
Deleting twice is safe: the second call also answers `200`. See
[`DELETE /api/v1/apps/{name}`](#delete-apiv1appsname).

```bash
curl -X DELETE "http://<awtrix-ip>/api/v1/apps/clock"
```

### Script sounds

A script can bring its own MP3s. They are stored in a folder named after the script,
`/SCRIPTS/<name>/<sound>.mp3`.
<!-- only esp32 -->
This clock keeps them but does not play them.
<!-- /only -->
<!-- only esp32-s3 tc002 -->
The script plays them by name: `sound.play("boost")` plays `boost.mp3` from its own folder, or
`/MP3/boost.mp3` when its folder has none. See [Sound and music](../guides/scripting/sound.md).
<!-- /only -->

* Replacing the script's source keeps its sounds, and so does
  [removing the script](#delete-apiv1appsname). Sounds without a script stay listed, marked
  `orphan`, until they are deleted.
* A sound is named like a [stored MP3](#post-apiv1audiomp3): 1–32 characters of `A-Za-z0-9_-`
  plus `.mp3`. The folder holds MP3s only.
* A script's sound may have the name of a melody or of an MP3 in `/MP3`. The folder is its own
  set of names.

To hear one, <!-- only esp32-s3 tc002 -->send [`POST /api/v1/audio/play`](#post-apiv1audioplay) with
`{"file":"<name>/<sound>"}`, or <!-- /only -->open `GET /SCRIPTS/<name>/<sound>.mp3` in a browser.

### GET /api/v1/apps/script/{name}/sounds

Lists a script's sounds with a SHA-256 checksum of each, so an installer can tell which ones it
still has to upload.

**Request:** no body.

**Response:** `200`:

```json
{"files":[{"name":"boost.mp3","size":20411,
           "sha256":"df577af7d16e9636420caf755518243926865f63220570e4d9e0a33a6e605c09"}],
 "usedBytes":176128,"totalBytes":13107200}
```

| Field | Meaning |
|---|---|
| `files` | one entry per sound: `name` with `.mp3`, `size` in bytes, `sha256` as 64 lowercase hex digits. `[]` when the script has no sounds |
| `usedBytes`, `totalBytes` | the whole storage, as in [`GET /api/v1/audio/mp3`](#get-apiv1audiomp3) |

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: `{name}` is not `[A-Za-z0-9_-]{1,32}` |
| 404 | `notFound`, `no such script`: no script of that name and no sounds left from one |
| 405 | wrong method: `allowed: GET, POST, DELETE` |

```bash
curl "http://<awtrix-ip>/api/v1/apps/script/Racer/sounds"
```

### POST /api/v1/apps/script/{name}/sounds

Uploads one MP3 into a script's folder.

**Request:** a `multipart/form-data` upload of one file. The script must be installed first. The
file name becomes the sound name: 1–32 characters of `A-Za-z0-9_-` plus `.mp3`. The content must
be an MP3. A sound with the same name is replaced.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: `{name}` is not a script name, or the file name is not a valid MP3 name |
| 400 | `badRequest`: no file arrived, or the upload was interrupted |
| 401 | authentication failed |
| 403 | `forbidden`: uploads are not allowed in setup mode |
| 404 | `notFound`, `no such script` |
| 415 | `unsupportedMediaType`: the file is not an MP3 |
| 507 | `insufficientStorage`, `write failed`: the file could not be saved. The previous version is kept |

```bash
curl -X POST "http://<awtrix-ip>/api/v1/apps/script/Racer/sounds" -F "file=@boost.mp3"
```

### DELETE /api/v1/apps/script/{name}/sounds/{sound}

Deletes one of a script's sounds, and stops it first if it is playing.

**Request:** no body. `{sound}` is the name without `.mp3`. The script does not have to be
installed: sounds kept from a deleted script are deleted the same way.

**Response:** `200` `{"ok":true}`. The folder is removed with its last sound.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: `{name}` is not a script name, or `{sound}` is not a valid MP3 name |
| 403 | `forbidden`: not allowed in setup mode |
| 404 | `notFound`, `no such MP3` |
| 405 | wrong method: `allowed: DELETE` |

```bash
curl -X DELETE "http://<awtrix-ip>/api/v1/apps/script/Racer/sounds/boost"
```

### DELETE /api/v1/apps/script/{name}/sounds

Deletes all of a script's sounds and its folder, and stops any that is playing. Use it before or
after [`DELETE /api/v1/apps/{name}`](#delete-apiv1appsname) to remove a script completely.

**Request:** no body.

**Response:** `200` `{"ok":true}`: also when there was nothing to delete.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: `{name}` is not `[A-Za-z0-9_-]{1,32}` |
| 403 | `forbidden`: not allowed in setup mode |

```bash
curl -X DELETE "http://<awtrix-ip>/api/v1/apps/script/Racer/sounds"
```

### GET /api/v1/scripts/shared

Lists the values scripts share with each other through the `shared` module. See
[Talking to other apps](../guides/scripting/several-apps.md#talking-to-other-apps).

**Request:** no body.

**Response:** `200` with an array, grouped by owner and sorted by key. `[]` when nothing is shared.

```json
[{"owner":"weather","key":"temp","type":"real","value":21.5,"ageMs":3200},
 {"owner":"weather","key":"unit","type":"string","value":"C","ageMs":3200}]
```

| Field | Meaning |
|---|---|
| `owner` | the name of the script that wrote it: only that script can change it |
| `key` | the key. Scripts read it as `owner.key` |
| `type` | `int`, `real`, `bool` or `string`: only single values can be shared |
| `value` | the value, in its own JSON type (an infinite or invalid number is `null`) |
| `ageMs` | milliseconds since it was last written |

This list is read-only. It is empty after a restart, and removing or re-saving a script removes
everything it shared.

**Errors:**

| Status | Condition |
|---|---|
| 405 | `methodNotAllowed`, `allowed: GET` |
| 503 | `unavailable`, `no scripting`: this firmware has no scripting |

```bash
curl "http://<awtrix-ip>/api/v1/scripts/shared"
```

---

## Notifications

### POST /api/v1/notifications

Shows a one-time message on top of the rotation.

**Request:** every app payload field ([App & notification payload](payload.md)), plus these:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `name` | string | - | a name to [dismiss it by](#delete-apiv1notificationsname) later |
| `hold` | boolean | `false` | keep it shown until it is dismissed |
| `stack` | boolean | `true` | wait behind other notifications instead of replacing them |
| `wakeup` | boolean | `false` | switch the display on if it is off |
| `sound` | string \| object \| array | - | a [sound object](#the-sound-object), played as an alert when the notification appears. `""` and `null` mean no sound. See [Payload → Sound](payload.md#sound) |

The payload is accepted whole or not at all. Validation errors answer `422` with the key in `field`, and
nothing is shown. `effect` and `overlay` must be names from
[`GET /api/v1/capabilities`](#get-apiv1capabilities). All rules:
[Payload → Errors](payload.md#errors).

An **array** is accepted only with exactly one object in it, which becomes the notification. Unlike
pushed apps, notifications are not split into several: more than one element answers `422`.

`sound` is checked when the notification arrives, with the rules of
[audio playback](#post-apiv1audioplay). A notification with an invalid `sound` is refused whole.
A name that is not stored, or a sound the clock cannot play, is not an error: the notification
shows without sound.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 422 | `field: "<key>"`: an unknown key, an invalid color, an unknown mode word, a wrong `scroll`, a wrong `draw` command, or an `effect`/`overlay` name AWTRIX does not know |
| 422 | an array with more than one element: `one notification per request` |
| 422 | `field: "sound"` or `"sound.<key>"`, for example `sound.rtttl` or `sound[1].file`: the sound is not valid. The messages are those of [audio playback](#post-apiv1audioplay) |
| 507 | the notification queue is full (`insufficientStorage`). See [Limits](limits.md#apps-and-notifications) |
| 405 | wrong method: `allowed: POST` |

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"Doorbell","icon":"1234","textColor":"#FF0000","hold":true,"sound":{"rtttl":"d:d=4,o=5,b=120:c,e,g"}}'
```

### DELETE /api/v1/notifications/active

Dismisses the notification shown.

**Request:** no body.

**Response:** always `200` `{"ok":true}`, also when no notification is shown.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: DELETE` |

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/active
```

### DELETE /api/v1/notifications/{name}

Dismisses the notification with this `name`, wherever it is in the queue.

**Request:** no body. The notification does not have to be shown. Removing a waiting one does
not affect the one shown.

`active` is reserved: `/api/v1/notifications/active` always means the notification that is shown, so a
notification named `active` cannot be dismissed by name. Use a different name.

A name does not protect a notification: anyone who can reach the API can dismiss it. To limit who
can use the API, turn on [HTTP authentication](system.md#identity-web-server-and-authentication).

**Response:** `200` `{"ok":true}`: a notification with that name was removed.

**Errors:**

| Status | Condition |
|---|---|
| 404 | no notification in the queue has that name (`notFound`) |
| 405 | wrong method: `allowed: DELETE` |

```bash
# push one that you can dismiss later
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"name":"backup-job","text":"Backup running","hold":true}'

# dismiss it, whatever else has arrived since
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/backup-job
```

---

## Indicators

The three indicators are small lights on the right edge of the display: id 1 at the top, id 2 in the
middle, id 3 at the bottom. They can blink (`blinkMs`) or fade in and out (`fadeMs`). Their state is
also shown in [`GET /api/v1/device`](#get-apiv1device) and sent over MQTT and to Home Assistant.

### PUT /api/v1/indicators/{id}

Switches an indicator on, changes its color, or sets it to blink or fade.

**Request:** `{id}` is `1`, `2` or `3`. The body must not be empty.

| Key | Type | Range | When left out | Units |
|---|---|---|---|---|
| `color` | color | any color form | on/off and color unchanged | - |
| `blinkMs` | integer | 0–65535, **not checked** | **unchanged** | ms |
| `fadeMs` | integer | 0–65535, **not checked** | **unchanged** | ms |

* **Only `color` switches the indicator on or off.** Without `color`, on/off and the color stay.
* `color` `0` or `null` switches it **off** and **keeps the stored color**, so Home Assistant can
  send it back unchanged.
* Any other color sets the color and switches it **on**.
* `blinkMs` and `fadeMs` stay as they are when left out. Only
  [`DELETE /api/v1/indicators/{id}`](#delete-apiv1indicatorsid) resets them to `0`.
* A `blinkMs` or `fadeMs` outside 0–65535 is not refused. It is stored as `0`.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 415 | body sent with a non-JSON `Content-Type` (`unsupportedMediaType`) |
| 422 | `field: "color"`: not a valid color. Nothing changes |
| 422 | empty or `{}` body: `body required` |
| 404 | `notFound`, `id must be 1..3`: any other id, including `10` |
| 404 | `unknown route`: the path without an id, `/api/v1/indicators/` |
| 405 | wrong method: `allowed: PUT, DELETE` |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/indicators/1 \
  -H "Content-Type: application/json" \
  -d '{"color":"#FF0000","blinkMs":500}'
```

### DELETE /api/v1/indicators/{id}

Switches an indicator off and resets it: color `0`, `blinkMs` `0`, `fadeMs` `0`.

**Request:** no body.

**Response:** always `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 404 | `notFound`, `id must be 1..3`: any other id |
| 405 | wrong method: `allowed: PUT, DELETE` |

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/indicators/1
```

---

## Audio

<!-- only esp32 -->
Everything the buzzer and the DFPlayer play: melodies and DFPlayer tracks. This clock stores MP3
files, for example the sounds a script brings along, but it does not play them.
<!-- /only -->
<!-- only esp32-s3 -->
Everything the buzzer, the DFPlayer and an I²S amplifier play: melodies, DFPlayer tracks, stored
MP3s and internet radio.
<!-- /only -->
<!-- only tc002 -->
Everything the speaker plays: stored MP3s, melodies, songs for the synthesizer, speech, recordings
and internet radio.
<!-- /only -->

What a device can play is in `audio` in [`GET /api/v1/capabilities`](#get-apiv1capabilities). How
loud each sound plays: [Sound settings](settings.md#sound).

### The sound object

`POST /api/v1/audio/play`, `<P>/cmd/audio/play` and a notification's `sound` all take the same
value. It has three forms:

* **A string** is a stored name: `"ding"` is the same as `{"file":"ding"}`.
* **An object** has exactly one source key, plus options.
* **A list** holds 1 to 4 strings or objects. The clock plays the first entry it can play: it has
  the hardware for it, and for `file` the file is stored.<!-- only esp32-s3 tc002 --> `station` is not allowed in a list.<!-- /only -->

Source keys, exactly one per object:

| Key | Value | Plays |
|---|---|---|
<!-- only esp32 -->
| `file` | a stored name (`"ding"`) | a stored melody |
<!-- /only -->
<!-- only esp32-s3 -->
| `file` | a stored name (`"ding"`) or a script's sound (`"Racer/boost"`) | a stored MP3 or melody |
<!-- /only -->
<!-- only tc002 -->
| `file` | a stored name (`"ding"`), a script's sound (`"Racer/boost"`), or an `http://` / `https://` address | a stored MP3 or melody, or an MP3 downloaded from the address (`audio.url`) |
<!-- /only -->
| `rtttl` | RTTTL text, up to 512 characters | the melody in the request (`audio.rtttl`) |
<!-- only esp32-s3 tc002 -->
| `song` | [song text](songs.md) | the synthesizer (`audio.song`) |
<!-- /only -->
<!-- only tc002 -->
| `speech` | text, 1 to 512 bytes | the clock's voice, once a voice is installed (`audio.speech`). How it reads text: [Speech](../guides/sounds.md#speech) |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `track` | integer 1–2999 | a DFPlayer track (`audio.track`) |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `station` | a station name, a position in the list (integer from 0), or a stream address | internet radio (`audio.radio`). See [Radio](#radio) |
<!-- /only -->

Options:

| Key | Type | Meaning |
|---|---|---|
| `loop` | boolean | repeat the sound until it is stopped or a new alert replaces it. In a notification: while the notification is shown.<!-- only esp32-s3 tc002 --> Not with `station`<!-- /only --> |
<!-- only tc002 -->
| `nextBar` | boolean | scripts only, with a looping `song`. See [Sound and music](../guides/scripting/sound.md) |
<!-- /only -->

How `file` finds a name:

<!-- only tc002 -->
* An address is downloaded and played. The answer comes at once. If the download fails, nothing
  plays and [`alert.error`](#get-apiv1audio) says why. A file can be up to 4 MB.
<!-- /only -->
<!-- only esp32 -->
* A name is the melody `/MELODIES/<name>.txt`.
<!-- /only -->
<!-- only esp32-s3 tc002 -->
* `"Script/name"` is looked up in that script's folder only, `/SCRIPTS/<Script>/<name>.mp3`.
* A plain name is the MP3 `/MP3/<name>.mp3`, else the melody `/MELODIES/<name>.txt`. A script's
  own call looks in `/SCRIPTS/<script>/<name>.mp3` first.
<!-- /only -->

Names are 1 to 32 characters of `A-Za-z0-9_-`. An MP3 in `/MP3` and a melody in `/MELODIES` never
share a name, see [`nameTaken`](errors.md#error-codes).

Who sends a sound decides its group, and so its volume: everything sent here is an
**alert**<!-- only esp32-s3 tc002 -->, and `station` is the **radio**<!-- /only -->. What happens when sounds meet:
[What plays over what](../guides/sounds.md#what-plays-over-what).

### GET /api/v1/audio

Reads what each group plays<!-- only esp32-s3 tc002 -->, and the radio station list<!-- /only -->.

**Request:** no body.

**Response:** `200`:

<!-- only esp32 -->
```json
{
  "radio": {
    "playing": false,
    "station": "",
    "title": "",
    "error": "",
    "underruns": 0,
    "decodeUs": 0,
    "starvedMs": 0,
    "bufferBytes": 0
  },
  "app": {"playing": false, "name": "", "error": ""},
  "alert": {"playing": true, "name": "ding", "error": ""},
  "stations": []
}
```

| Key | Type | Meaning |
|---|---|---|
| `radio` | object | always as shown: this clock plays no radio |
| `app.playing` | boolean | a script plays a sound |
| `alert.playing` | boolean | an alert plays |
| `app.name`, `alert.name` | string | what the group played last. The `file` value as sent (`ding`), else `rtttl`, or the track number as text |
| `app.error`, `alert.error` | string | always `""` |
| `stations` | array | the stored station list |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
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
  "alert": {"playing": true, "name": "ding", "error": ""},
  "stations": [{"name": "SWR3", "url": "https://liveradio.swr.de/sw282p3/swr3/"}]
}
```

| Key | Type | Meaning |
|---|---|---|
| `radio.playing` | boolean | a stream is playing |
| `radio.station` | string | the station name, or the address for a stream played by address |
| `radio.title` | string | the last track title the station sent, empty until one arrives |
| `radio.error` | string | why playback stopped. Cleared when the next play succeeds |
| `radio.underruns` | integer | how often playback fell behind. Each one is a dropout you hear |
| `radio.decodeUs` | integer | average time in microseconds to decode one MP3 frame. One frame is 24000 to 72000 µs of audio (26100 µs at 44.1 kHz), so a value near that means the device cannot keep up |
| `radio.starvedMs` | integer | milliseconds spent waiting for stream data. A high value points to a slow network rather than a slow device |
| `radio.bufferBytes` | integer | stream data received but not yet played: how long a network pause playback can bridge |
<!-- only esp32-s3 -->
| `app.playing` | boolean | a script plays a sound |
| `alert.playing` | boolean | an alert plays |
| `app.name`, `alert.name` | string | what the group played last. The `file` value as sent (`ding`, `Racer/boost`), else `rtttl`, or the track number as text |
| `app.error`, `alert.error` | string | always `""` |
<!-- /only -->
<!-- only tc002 -->
| `app.playing` | boolean | a script plays a sound, an effect or music |
| `alert.playing` | boolean | an alert plays |
| `app.name`, `alert.name` | string | what the group played last. The `file` value as sent (`ding`, `Racer/boost`, an address), else the key (`rtttl`, `song`, `speech`) |
| `app.error`, `alert.error` | string | why the last sound from an address did not play, for example `HTTP 404`. Cleared by the next sound of that group |
<!-- /only -->
| `stations` | array | the stored station list |

The four numbers from `underruns` to `bufferBytes` show playback health. `underruns` and
`starvedMs` keep counting across stations, so compare two readings.<!-- only esp32-s3 --> All four
are `0` while the board cannot play radio.<!-- /only -->
<!-- /only -->

The same document is published, retained, on [`<P>/state/audio`](mqtt.md#state-topics) on every
change of any group.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl http://<awtrix-ip>/api/v1/audio
```

### POST /api/v1/audio/play

Plays a sound<!-- only esp32-s3 tc002 -->, or starts a radio station<!-- /only -->.

**Request:** a [sound object](#the-sound-object): a string, an object or a list.

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H "Content-Type: application/json" \
  -d '{"rtttl":"beep:d=4,o=5,b=120:c,e,g"}'
```

The sound plays as an alert, at `volume × alertVolume`.<!-- only esp32-s3 tc002 --> `station` plays as the radio, at
`volume × radioVolume`.<!-- /only -->

* A new alert replaces the alert that is playing.
* With `"loop": true` the alert repeats until [`POST /api/v1/audio/stop`](#post-apiv1audiostop)
  stops the `alert` group or everything, or until a new alert replaces it.
<!-- only esp32-s3 tc002 -->
* While an alert plays, the radio pauses and comes back afterwards.
<!-- /only -->
<!-- only tc002 -->
* A song plays once. With `"loop": true` it repeats.
<!-- /only -->
<!-- only esp32-s3 tc002 -->
* `station` takes a station name, its position in the list (counted from 0), or a stream address.
  A stream address pointing to an `.m3u` or `.pls` playlist plays its first entry.
<!-- /only -->

**Response:** `200` `{"ok":true}`. The sound starts right after.

**Errors:**

Mistakes in the body are checked first. Then the stored files and the hardware are checked.

See [Audio playback errors](errors.md#audio-playback) for the status, field and message of
each failure.

Inside a list, `field` starts with the entry's position: `[1].rtttl`.

<!-- only esp32-s3 tc002 -->
Play a script's own sound:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H "Content-Type: application/json" \
  -d '{"file":"Racer/boost"}'
```
<!-- /only -->
<!-- only tc002 -->

Speak once a voice is installed, play `ding` otherwise:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H "Content-Type: application/json" \
  -d '[{"speech":"Good morning. It is 7:30."},"ding"]'
```
<!-- /only -->
<!-- only esp32 esp32-s3 -->

Play DFPlayer track 3, or the melody `ding` when no DFPlayer is set up:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H "Content-Type: application/json" \
  -d '[{"track":3},"ding"]'
```
<!-- /only -->
<!-- only esp32-s3 tc002 -->

Start a station by its position in the list:

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/play \
  -H "Content-Type: application/json" \
  -d '{"station":0}'
```
<!-- /only -->

### POST /api/v1/audio/stop

Stops what is playing.

**Request:** no body or `{}` to stop everything, or `{"group": ...}` to stop one group:

| `group` | Stops |
|---|---|
| `"alert"` | the alert that is playing |
| `"app"` | every sound a script plays<!-- only tc002 -->: single sounds, effects and music<!-- /only --> |
<!-- only esp32-s3 tc002 -->
| `"radio"` | the radio |
<!-- /only -->

**Response:** always `200` `{"ok":true}` for a valid body.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 422 | `field: "group"`: `must be alert, app or radio` |
| 422 | `unknown field`: `field` names a key other than `group` |
| 422 | `must be an object`: the body is JSON, but not an object |
| 405 | wrong method: `allowed: POST` |

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/stop \
  -H "Content-Type: application/json" \
  -d '{"group":"alert"}'
```

<!-- only tc002 -->
### POST /api/v1/audio/clip

Plays a recording once, such as a voice message from a phone. The clip is not stored.

**Request:** the body is the audio file itself, with any `Content-Type`, up to 2 MiB:

* WAV: 16-bit PCM, mono or stereo, at 16000, 22050, 24000, 32000, 44100 or 48000 Hz
* MP3: MPEG-1, 2 or 2.5 layer III. An ID3 tag in front is fine

A clip is an alert, like a sound from [`POST /api/v1/audio/play`](#post-apiv1audioplay). It plays
at `volume × alertVolume` and replaces an alert that is playing. A radio station pauses and comes
back afterwards. [`POST /api/v1/audio/stop`](#post-apiv1audiostop) stops it.

**Response:** `200` `{"ok":true}` once the clip starts.

**Errors:**

| Status | Condition |
|---|---|
| 413 | `payloadTooLarge`: the body is larger than the device takes |
| 422 | `body required`: the body is empty |
| 422 | `not WAV or MP3`, or `unsupported WAV`: a WAV that is not 16-bit PCM at one of the rates above |
| 503 | `unavailable`, `no clip playback`: the device plays no clips |
| 503 | `unavailable`, `speaker unavailable`: the speaker cannot play right now |
| 405 | wrong method: `allowed: POST` |

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/clip \
  -H "Content-Type: audio/wav" \
  --data-binary @message.wav
```
<!-- /only -->

### GET /api/v1/audio/melodies

Lists all stored melodies with their content.

**Request:** no body.

**Response:** `200`:

```json
{"melodies":[{"name":"doorbell","rtttl":"doorbell:d=4,o=5,b=100:e,c",
              "bytes":26,"notes":2,"durationMs":2400,"valid":true}],
 "usedBytes":41216,"totalBytes":1048576}
```

| Field | Meaning |
|---|---|
| `name` | the melody name: the file is `/MELODIES/<name>.txt` |
| `rtttl` | the file content, unchanged |
| `bytes` | file size |
| `notes`, `durationMs` | number of notes and length. Both `0` when the melody cannot be read |
| `valid` | whether the melody can be read |
| `error`, `index` | only when `valid` is `false`: the reason and the position of the error |
| `usedBytes`, `totalBytes` | the whole storage, not only melodies |

A melody with an error is **listed, not hidden**, with `valid: false` and the reason in `error`, so
you can open and fix it.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl http://<awtrix-ip>/api/v1/audio/melodies
```

### PUT /api/v1/audio/melodies/{name}

Saves a melody.

**Request:** `{name}` is 1–24 characters of `A-Z`, `a-z`, `0-9`, `_` and `-`. The body is
`{"rtttl": "d=4,o=5,b=100:e,c"}`.

The melody title is always set to `{name}`: a two-part `defaults:notes` text gets the name added in
front, and a three-part text has its title replaced.

**Response:** `201` when the melody is new, `200` when it replaced an existing one.

**Errors:**

| Status | Condition |
|---|---|
| 400 | body is not valid JSON (`invalidJson`) |
| 409 | `nameTaken`, `name taken`: an MP3 `/MP3/{name}.mp3` exists. An MP3 and a melody never share a name |
| 415 | `Content-Type` is not `application/json` |
| 422 | `field: "name"`: the name is not 1–24 of `[A-Za-z0-9_-]` |
| 422 | `field: "rtttl"`: missing, not a string, or cannot be read. `message` gives the reason and the position |
| 507 | `insufficientStorage`: storage is full |

To rename a melody, save it under the new name, then delete the old one.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/audio/melodies/doorbell \
  -H "Content-Type: application/json" \
  -d '{"rtttl":"d=4,o=5,b=100:e,c"}'
```

### DELETE /api/v1/audio/melodies/{name}

Deletes a melody.

**Request:** no body.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 404 | `notFound`, `melody not found` |
| 405 | wrong method: `allowed: PUT, DELETE` |

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/audio/melodies/doorbell
```

### GET /api/v1/audio/mp3

Lists the stored MP3s: your own under `files`, and the sounds of each installed script under
`scripts`.

**Request:** no body.

**Response:** `200`:

```json
{"files":[{"name":"ding.mp3","size":40118}],
 "scripts":[{"name":"awtrix-gp","title":"AWTRIX GP","orphan":false,
             "files":[{"name":"boost.mp3","size":20411}]}],
 "usedBytes":176128,"totalBytes":13107200}
```

| Field | Meaning |
|---|---|
| `files` | the MP3s in `/MP3`, `{name, size}` each |
| `scripts` | one entry per script folder with at least one [sound](#script-sounds): `name` is the script name, `title` its `@name` header (or the script name without one), `orphan` is `true` for sounds whose script was deleted, and `files` its sounds |
| `usedBytes`, `totalBytes` | the whole storage, shared with icons, melodies and scripts |

```bash
curl http://<awtrix-ip>/api/v1/audio/mp3
```

### POST /api/v1/audio/mp3

Uploads one MP3 into `/MP3`.

**Request:** a `multipart/form-data` upload of one file. There is no `?dir=`. The file name is the
name you play it by, so it must be **1–32 characters of `A-Za-z0-9_-` plus `.mp3`**. The content
must be an MP3.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: the file name is not a valid MP3 name. Nothing is stored |
| 400 | `badRequest`: no file arrived, or the upload was interrupted |
| 401 | authentication failed |
| 403 | `forbidden`: uploads are not allowed in setup mode |
| 409 | `nameTaken`, `name taken`: a melody of that name exists. An MP3 and a melody never share a name. Nothing is stored |
| 415 | `unsupportedMediaType`: the file is not an MP3 |
| 507 | `insufficientStorage`, `write failed`: the file could not be saved. The previous version is kept |

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/mp3 -F "file=@ding.mp3"
```

### DELETE /api/v1/audio/mp3/{name}

Deletes one MP3.

**Request:** no body. `{name}` is the name without `.mp3`.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: not a valid MP3 name |
| 404 | `notFound`, `no such MP3` |
| 405 | wrong method: `allowed: DELETE` |

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/audio/mp3/ding
```

### POST /api/v1/audio/mp3/rename

Renames one MP3 in `/MP3`. Sounds in a script's own folder cannot be renamed.

**Request:** a JSON body:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `from` | string | yes | the current name without `.mp3`, for example `ding` |
| `to` | string | yes | the new name: 1 to 32 letters, digits, `_` or `-` |

Alarms, notifications and scripts that play the old name stay silent until you change them.

**Response:** `200` `{"ok":true}`, also when `from` and `to` are the same.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidJson`: the body is not a JSON object |
| 400 | `invalidName`: a name is not valid (`field` says which) |
| 403 | `forbidden`: not allowed in setup mode |
| 404 | `notFound`, `no such MP3` |
| 409 | `nameTaken`: an MP3 or a melody with the new name exists |
| 500 | `internalError`: renaming failed. The MP3 keeps its name |

```bash
curl -X POST http://<awtrix-ip>/api/v1/audio/mp3/rename -H "Content-Type: application/json" -d '{"from":"ding","to":"bell"}'
```

<!-- only esp32-s3 tc002 -->
### Radio

<!-- only esp32-s3 -->
Internet radio plays when the board has PSRAM and an I²S amplifier is set up.
`capabilities.audio.radio` says whether this device can play it. Without it, a play request
answers `503 unavailable` and the web UI hides the Radio section. The station list can be read and
edited either way.
<!-- /only -->
<!-- only tc002 -->
Internet radio plays on the speaker.
<!-- /only -->

To start a station, use [`POST /api/v1/audio/play`](#post-apiv1audioplay) with `station`: a name,
a position in the list, or a stream address. To stop it, use
[`POST /api/v1/audio/stop`](#post-apiv1audiostop) with `{"group":"radio"}`. Wiring, limits and
troubleshooting: **[Internet radio](../guides/radio.md)**.

### GET /api/v1/audio/stations

Reads the stored station list<!-- only esp32-s3 -->, also on a device that cannot play radio<!-- /only -->.

**Request:** no body.

**Response:** `200` `{"stations":[{"name":"Example","url":"https://example.com/radio.mp3"}]}`.

```bash
curl http://<awtrix-ip>/api/v1/audio/stations
```

### PUT /api/v1/audio/stations

Replaces the whole station list. There is no route for a single station.

**Request:**

```json
{"stations": [{"name": "SWR3", "url": "https://liveradio.swr.de/sw282p3/swr3/"}]}
```

A plain array is also accepted. Limits: at most **32** stations, name 1–24 characters, URL at most
255 characters starting with `http` or `https`, every name only once.

**Response:** `200` `{"ok":true}`. The list is saved.

**Errors:** the list is saved whole or not at all. The error names the wrong entry:

```json
{"error":{"code":"validationFailed","message":"must not be empty","field":"stations[1].name"}}
```

| Status | Condition |
|---|---|
| 400 | body is not valid JSON |
| 422 | an entry is not valid, or there are more than 32 |
| 405 | wrong method: `allowed: GET, PUT` |
| 507 | `insufficientStorage`, `applied, not saved yet`: active until restart, but saving failed |

Free storage and repeat the request before rebooting after a 507.<!-- only tc002 --> AWTRIX also
retries saving by itself.<!-- /only -->

```bash
curl -X PUT http://<awtrix-ip>/api/v1/audio/stations \
  -H "Content-Type: application/json" \
  -d '{"stations":[{"name":"SWR3","url":"https://liveradio.swr.de/sw282p3/swr3/"}]}'
```
<!-- /only -->

---

## Capabilities

### GET /api/v1/capabilities

Lists what this device supports: effect, transition, overlay and palette names, sound outputs<!-- only esp32 esp32-s3 -->,
sensors and pin rules<!-- /only --><!-- only tc002 --> and
sensors<!-- /only -->. Read the names here instead of writing them into your code.

**Request:** no body.

**Response:** `200`:

<!-- only esp32 esp32-s3 -->
```json
{"effects":[...],"transitions":[...],"overlays":[...],"palettes":[...],"audio":{...},"gpio":{...},"sensors":{...}}
```
<!-- /only -->
<!-- only tc002 -->
```json
{"effects":[...],"transitions":[...],"overlays":[...],"palettes":[...],"audio":{...},"gpio":null,"sensors":{...}}
```
<!-- /only -->

| Key | Count | Values |
|---|---|---|
| `effects` | 19 | `BrickBreaker`, `Checkerboard`, `ColorWaves`, `Fade`, `Fireworks`, `LookingEyes`, `Matrix`, `MovingLine`, `Pacifica`, `PingPong`, `Plasma`, `PlasmaCloud`, `Radar`, `Ripple`, `Snake`, `SwirlIn`, `SwirlOut`, `TheaterChase`, `TwinklingStars` |
| `transitions` | 22 | `Random`, `Slide`, `Dim`, `Zoom`, `Rotate`, `Pixelate`, `Curtain`, `Ripple`, `Blink`, `Reload`, `Fade`, `Cover`, `Uncover`, `Split`, `Blinds`, `Blocks`, `Flash`, `Diamond`, `Wave`, `Rain`, `Melt`, `Interlace` |
| `overlays` | 6 | `drizzle`, `frost`, `rain`, `snow`, `storm`, `thunder` |
| `palettes` | 8 | `Cloud`, `Lava`, `Ocean`, `Forest`, `Stripe`, `Party`, `Heat`, `Rainbow`: the built-in ones. List palette files with `GET /api/v1/files?dir=/PALETTES` |
| `paletteEffects` | - | the effects that use a palette. See [Effects & overlays](../guides/effects.md) |
| `audio` | - | one flag per kind of sound the device can play. See below |
<!-- only esp32 esp32-s3 -->
| `microphone` | - | always `false`: this clock has no microphone |
| `sensors` | - | which sensors the device has. See below |
| `gpio` | - | the chip's pin rules. See below |
<!-- /only -->
<!-- only tc002 -->
| `microphone` | - | `true` when scripts and music visualizations can hear the microphone. The web UI then shows the source choice under System › Audio. It says the microphone exists, not that it works at this moment: it can be briefly unavailable, for example during an update |
| `sensors` | - | which sensors the device has. See below |
| `gpio` | - | always `null`: the pins of this clock are fixed |
<!-- /only -->
| `scriptUpdates` | - | always `true`: [script updates](#script-updates) are supported |

`effects` and `overlays` are sorted alphabetically. `transitions` has its own fixed order. All
effect, overlay, palette and transition names accept any upper/lower case: `"matrix"`, `"Matrix"`
and `"MATRIX"` are the same effect. This list shows the spelling AWTRIX uses in its answers.

<!-- only tc002 -->
These keys appear only while the feature is available:

| Key | Meaning |
|---|---|
| `ble` | `true`: scripts can use Bluetooth LE, see [Bluetooth LE](../guides/ble.md) |
| `gamepad` | `true`: scripts can read a gamepad. See [`GET /api/v1/gamepad`](#get-apiv1gamepad) |
| `gamepadRemote` | `true`: a phone can be a gamepad. See [`POST /api/v1/gamepad/remote`](#post-apiv1gamepadremote) |
| `mqttTls` | `true`: MQTT can connect over TLS. See [`GET /api/v1/mqtt/tls`](#get-apiv1mqtttls) |
| `voice` | `true`: Home Assistant Voice is available. See [`/api/v1/voice`](#apiv1voice) |
| `bootSound` | `true`: the clock plays a sound when it starts. The [`bootSound`](settings.md#sound) setting turns it off |
| `enlargeApps` | `true`: pushed apps and notifications can be shown at double size. The [`enlargeApps`](settings.md#global-text) setting turns it off |
| `oauth` | `true`: scripts can sign in to services, see [Script sign-in](#script-sign-in) |
| `crypto` | `true`: scripts can `import crypto`, see [Hashing](../guides/hashing.md) |
| `layout` | `true`: scripts can use prepared layouts |
| `tcp` | `true`: scripts can `import tcp`, see [TCP in scripts](../guides/tcp.md) |
| `clockFaces` | the names the [`clockFace`](settings.md#tc002-clock-faces) setting accepts |

`gamepad`, `gamepadRemote`, `oauth`, `crypto`, `layout` and `tcp` need scripting switched on. The `layouts` object
holds the limits of prepared layouts.
<!-- /only -->
Further keys describe the display size and fonts. They are listed in the
[OpenAPI description](../api/openapi.yaml).

#### `audio`: what the device can play

One flag for each kind of sound<!-- only esp32 esp32-s3 -->, so an app can tell "no buzzer" from "no DFPlayer"<!-- /only -->. Every
flag is always present:

<!-- only esp32 -->
```json
"audio":{"mp3":false,"rtttl":true,"song":false,"speech":false,"track":true,"radio":false,"url":false,"effect":false,"clip":false}
```

| Flag | `true` when |
|---|---|
| `rtttl` | melodies play: a buzzer pin is set |
| `track` | `dfplayer` is on and both DFPlayer pins are set |

`mp3`, `song`, `speech`, `radio`, `url`, `effect` and `clip` are always `false` on this clock.
<!-- /only -->
<!-- only esp32-s3 -->
```json
"audio":{"mp3":true,"rtttl":true,"song":true,"speech":false,"track":false,"radio":true,"url":false,"effect":false,"clip":false}
```

| Flag | `true` when |
|---|---|
| `mp3` | stored MP3s play: the board has PSRAM and an I²S amplifier is set up |
| `rtttl` | melodies play: a buzzer pin is set |
| `song` | the synthesizer plays [song text](songs.md). Needs the same hardware as `mp3` |
| `track` | `dfplayer` is on and both DFPlayer pins are set |
| `radio` | internet radio plays. Needs the same hardware as `mp3` |

`speech`, `url`, `effect` and `clip` are always `false` on this clock.
<!-- /only -->
<!-- only tc002 -->
```json
"audio":{"mp3":true,"rtttl":true,"song":true,"speech":true,"track":false,"radio":true,"url":true,"effect":true,"clip":true}
```

| Flag | `true` when |
|---|---|
| `mp3` | stored MP3s play on the speaker |
| `rtttl` | melodies play on the speaker |
| `song` | the synthesizer plays [song text](songs.md) |
| `speech` | the clock reads text aloud: a voice is installed |
| `radio` | internet radio plays |
| `url` | `file` takes an `http(s)://` address |
| `effect` | a script's effects and background music play over each other |
| `clip` | recordings sent whole play once, see [`POST /api/v1/audio/clip`](#post-apiv1audioclip) |

`track` is always `false` on this clock.
<!-- /only -->

The web UI shows each Audio section only when its flag is `true`.<!-- only esp32 esp32-s3 --> It hides the Audio tab when
`rtttl`, `track`, `mp3` and `radio` are all `false`.<!-- /only -->

#### `sensors`

<!-- only esp32 esp32-s3 -->
```json
"sensors":{"light":true}
```

| Flag | `true` when |
|---|---|
| `light` | the device has a light sensor: `pinLdr` is set to a pin. Read at start, so a pin change shows after a restart |

Without a light sensor, `autoBrightness` has no effect and the display uses `brightness`. The web UI
then hides the auto-brightness switch and its fields under System, and Home Assistant gets no
**Brightness mode** entity.
<!-- /only -->
<!-- only tc002 -->
```json
"sensors":{"light":false}
```

| Flag | Meaning |
|---|---|
| `light` | always `false`: this clock has no light sensor. `autoBrightness` has no effect, and the display uses `brightness` |
<!-- /only -->

<!-- only esp32 esp32-s3 -->
#### `gpio`: what the chip can do

The pin fields of [`/api/v1/system`](#get-apiv1system) are checked against the chip on your
board.<!-- only esp32 --> This chip has GPIO 0–39, with 34–39 input-only and ADC1 on 32–39.<!-- /only --><!-- only esp32-s3 --> This chip has GPIO 0–48, with no input-only pins and ADC1 on 1–10.<!-- /only -->
Read the rules here instead of keeping your own table.

<!-- only esp32 -->
```json
"gpio":{"soc":"esp32","label":"ESP32","max":39,
        "missing":[],"inputOnly":[[34,39]],
        "reserved":[{"lo":6,"hi":11,"why":"the SPI flash"}],
        "adc1":[[32,39]],"strapping":[[0,0],[2,2],[5,5],[12,12],[15,15]],
        "rtc":[[0,0],[2,2],[4,4],[12,15],[25,27],[32,39]],
        "matrix":[2,4,5,13,14,15,16,18,21,25,26,27,32,33],
        "defaults":{"pinMatrix":32, ...}}
```
<!-- /only -->
<!-- only esp32-s3 -->
```json
"gpio":{"soc":"esp32s3","label":"ESP32-S3","max":48,
        "missing":[[22,25]],"inputOnly":[],
        "reserved":[{"lo":19,"hi":20,"why":"the USB-JTAG interface"},
                    {"lo":26,"hi":37,"why":"the SPI flash and PSRAM"},
                    {"lo":43,"hi":44,"why":"the UART0 console"}],
        "adc1":[[1,10]],"strapping":[[0,0],[3,3],[45,46]],"rtc":[[0,21]],
        "matrix":[13,14,15,16,17,18,21,38,39,40,41,42,47],
        "defaults":{"pinMatrix":21, ...}}
```
<!-- /only -->

| Key | Meaning |
|---|---|
| `soc`, `label` | chip id and display name. `soc` is the same as in the device state |
| `max` | highest GPIO number |
| `missing` | ranges (both ends included) inside `0…max` that do not exist on this chip: refused |
| `inputOnly` | pins that cannot drive an output, so they are refused for the display, buttons, buzzer, I²C and DFPlayer TX<!-- only esp32-s3 -->. Empty on this chip<!-- /only --> |
| `reserved` | pins used by the flash memory<!-- only esp32-s3 -->, PSRAM, USB or the console<!-- /only -->: refused. `why` is also the text of the error message |
| `adc1` | the only pins accepted for `pinBattery` and `pinLdr` |
| `strapping` | pins that affect start-up. Shown as a warning, **never refused** |
| `rtc` | the only pins that can wake the device from [`POST /api/v1/device/sleep`](#post-apiv1devicesleep) with `pinBtnSelect`. Other pins are accepted but cannot wake AWTRIX |
| `matrix` | the only values `pinMatrix` accepts |
| `defaults` | the pins a new device with this chip starts with, and falls back to if the stored pins are invalid |
<!-- /only -->

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl http://<awtrix-ip>/api/v1/capabilities
```

---

## System

<!-- only esp32 esp32-s3 -->
Device configuration: Wi-Fi, MQTT, time server, login, hardware and pins. Each field explained:
[System configuration](system.md). Pin rules in detail: [GPIO & boards](gpio.md).
<!-- /only -->
<!-- only tc002 -->
Device configuration: Wi-Fi, MQTT, time server, login and hardware. Each field explained:
[System configuration](system.md).
<!-- /only -->

### GET /api/v1/system

Reads the device configuration.

**Request:** no body. Add `?secrets=1` to include `wifiPass`, `mqttPass` and `authPass`, for
example for a backup. Without it, they are left out. In setup mode any `secrets` query parameter makes the request fail with `403 forbidden`,
because that access point is open. A request for secrets from another website answers
`403 forbiddenOrigin` ([details](#cross-origin)). When authentication is on, this request needs it
like any other.

**Response:** `200` with every readable field from the [field table](#system-fields) below, under its own
name.

```bash
curl http://<awtrix-ip>/api/v1/system
curl "http://<awtrix-ip>/api/v1/system?secrets=1"
```

### PUT /api/v1/system

In setup mode only `wifiSsid`, `wifiPass` and `hostname` may be written, as strings.
Other fields, duplicate keys and malformed setup bodies answer `403 forbidden`.

Changes one or more configuration fields.

**Request:** any subset of the fields below. All values<!-- only esp32 esp32-s3 --> (and the resulting pin map as a whole)<!-- /only --> are checked before anything is saved.

* **Numbers are range-checked.** A value outside its range answers `422 validationFailed` with the
  key in `field`, and nothing is saved. The ranges are in the table below and in
  [Errors → `PUT /api/v1/system`](errors.md#put-apiv1system).
* **Unknown keys are ignored.** Text fields (`tz`, `hostname`, `buttonCallback`, …) are not
  checked either.<!-- only tc002 --> This clock has no wiring, orientation or `pin…` fields. They are
  ignored like unknown keys, whatever their value.<!-- /only -->
<!-- only esp32 esp32-s3 -->
* **Pin conflicts answer 400.** Duplicate pins, input-only pins and LED data pins outside the allowed
  list are `400 invalidPinConfig`, not 422. See [GPIO validation](#gpio-validation).
<!-- /only -->
* **An empty secret is ignored:** `"authPass": ""` keeps the stored password. Most other text
  fields *can* be cleared with `""`: `ntpServer`, `ip`, `mqttUser` and so on.
* **MQTT and login have an on/off switch.** `mqttEnabled` runs the MQTT client, `authEnabled`
  requires HTTP login. Switching one off keeps the stored host, username and password, and you can
  clear `mqttHost` and `authUser` freely. A switch can only be turned on when its values are set.
  Otherwise it answers `422 validationFailed` with the key in `field`:

    | Set | Requires | `field` on `422` |
    |---|---|---|
    | `mqttEnabled: true` | a non-empty `mqttHost` | `mqttHost` |
    | `authEnabled: true` | a non-empty `authUser` **and** `authPass` | `authUser` |

* **`wifiSsid` cannot be cleared.** `""` answers `422 validationFailed` (`field: wifiSsid`) and
  points to [`POST /api/v1/device/factory-reset`](#post-apiv1devicefactory-reset). A `PUT` never
  clears a secret. A factory reset does.
* **Most changes<!-- only esp32 esp32-s3 -->, and all pin changes,<!-- /only --> need a restart.** The web UI then shows a
  "reboot required" banner.

**Response:** `200` with the whole configuration after the change (secrets left out). It is saved.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidJson`, `invalid JSON` |
<!-- only esp32 esp32-s3 -->
| 400 | `invalidPinConfig` and a message naming the problem: nothing was saved |
<!-- /only -->
| 403 | `forbiddenOrigin`: the request came from another website ([details](#cross-origin)) |
| 422 | `validationFailed`: a number out of range or of the wrong type, or `wifiSsid` cleared. `field` names it |
| 405 | wrong method: `allowed: GET, PUT` |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"ntpServer":"192.168.1.1","statsInterval":30000}'
```

#### Fields {#system-fields}

| Key | Type | Default | Units / notes |
|---|---|---|---|
| `wifiSsid` | string | `""` | Wi-Fi network name |
| `wifiPass` | string | `""` | **secret**: left out on read, `""` ignored on write |
| `netStatic` | boolean | `false` | fixed IP address instead of DHCP |
| `ip` | string | `""` | accepts a network suffix (`192.168.1.50/24`), stored as `ip` + `subnet` |
| `gateway` | string | `""` | |
| `subnet` | string | `""` | |
| `dns1` | string | `""` | |
| `dns2` | string | `""` | |
<!-- only esp32 esp32-s3 -->
| `wifiConnectTimeout` | long | `15000` | ms to wait for Wi-Fi at start (5000–120000) before opening the setup hotspot |
| `wifiRoamRssi` | int | `0` | switch to a stronger access point below this signal in dBm (−90–0). `0` = off |
<!-- /only -->
| `mqttEnabled` | boolean | `false` | MQTT on/off. `true` needs a non-empty `mqttHost` |
| `mqttHost` | string | `""` | |
| `mqttPort` | integer | `1883` | 1–65535 |
| `mqttUser` | string | `""` | |
| `mqttPass` | string | `""` | **secret** |
<!-- only tc002 -->
| `mqttTls` | boolean | `false` | connect to the broker over TLS. See [System → MQTT](system.md#mqtt-and-home-assistant) |
| `mqttTlsPin` | string | `""` | SHA-256 of a broker certificate you trust, 64 lowercase hex digits. `""` trusts none |
<!-- /only -->
| `mqttPrefix` | string | `""` | empty uses the device id |
| `haDiscovery` | boolean | `false` | Home Assistant auto-discovery |
| `haPrefix` | string | `"homeassistant"` | |
| `ntpServer` | string | `"pool.ntp.org"` | |
| `tz` | string | `"CET-1CEST,M3.5.0,M10.5.0/3"` | POSIX time zone text, including daylight-saving rules |
| `tzName` | string | `"Europe/Berlin"` | the time zone name `tz` was chosen from. For display only |
| `hostname` | string | `""` | empty becomes `awtrixng-<uid>` |
<!-- only esp32 esp32-s3 -->
| `webPort` | integer | `80` | 0–65535. `0` means 80. Setup mode always uses 80 |
<!-- /only -->
| `authEnabled` | boolean | `false` | HTTP login on/off. `true` needs `authUser` **and** `authPass` |
| `authUser` | string | `""` | |
| `authPass` | string | `""` | **secret** |
<!-- only esp32 esp32-s3 -->
| `tempOffset` | number | `-9.0` | °C, −20–20 |
| `humOffset` | number | `0.0` | %, −50–50 |
| `batteryDividerRatio` | number | `1.79` | 0.1–10. Battery voltage / pin voltage: with a full battery, set it to `4.2 / (batteryPinMillivolts / 1000)` |
| `minBrightness` | integer | `10` | 0–255 |
| `maxBrightness` | integer | `220` | 0–255 |
| `ldrFactor` | number | `1.0` | 0–10 |
| `ldrGamma` | number | `2.2` | 0.1–10. `1.0` = no curve |
| `ldrOnGround` | boolean | `false` | how the light sensor is wired |
| `brightnessSmoothing` | long | `10000` | ms the display takes to follow a change in room light (0–60000). `0` = at once |
<!-- /only -->
| `lowBatteryThreshold` | integer | `0` | 0–100 %. Below it `GET /api/v1/device` reports `lowBattery: true`. `0` = off |
<!-- only esp32 esp32-s3 -->
| `panelWidth` | integer | `32` | 1–128. `panelWidth × panels` must be 32–128 |
| `panels` | integer | `1` | 1–128. How many panels the LED strip runs through |
| `panelStart` | string | `"topLeft"` | `topLeft` · `topRight` · `bottomLeft` · `bottomRight` |
| `panelWiring` | string | `"rows"` | `rows` · `columns` |
| `panelColorOrder` | string | `"grb"` | `rgb` · `rbg` · `grb` · `gbr` · `brg` · `bgr` |
| `panelSerpentine` | boolean | `true` | every second row or column runs backwards |
| `panelChainReverse` | boolean | `false` | the data cable enters the row of panels at the other end |
| `panelChainSerpentine` | boolean | `false` | every second panel is turned by 180° |
| `mirror` | boolean | `false` | mirrors the picture, not the wiring |
| `rotate` | boolean | `false` | turns the picture 180°. Also swaps the left and right buttons |
<!-- /only -->
| `swapButtons` | boolean | `false` | |
<!-- only esp32 esp32-s3 -->
| `dfplayer` | boolean | `false` | use a DFPlayer Mini on the DF pins. The buzzer keeps working. The DF pins are checked either way |
<!-- /only -->
| `buttonCallback` | string | `""` | web address (URL) called when a button<!-- only tc002 --> or the knob<!-- /only --> is used |
<!-- only esp32 esp32-s3 -->
| `artnet` | boolean | `false` | receive Art-Net DMX (UDP port 6454). Closed while `false` |
<!-- /only -->
| `mirrorShare` | boolean | `false` | let other clocks show this display (UDP port 4212) |
| `mirrorShareApps` | string | `"*"` | apps to share, comma separated. `*` = all, `""` = none |
| `mirrorShareNotifications` | boolean | `true` | share notifications too |
| `mirrorFrom` | string | `""` | IP address or host name of the clock to mirror. `""` = off |
| `mirrorFromApps` | string | `"*"` | apps of that clock to show, comma separated. `*` = all, `""` = none |
| `mirrorFromNotifications` | boolean | `true` | show that clock's notifications too |
| `statsInterval` | integer | `10000` | ms, 1000–600000 |
<!-- only esp32 esp32-s3 -->
| `tempDecimals` | integer | `0` | 0–2 |
<!-- /only -->
| `debugMode` | boolean | `false` | more detailed log lines |
| `scriptingEnabled` | boolean | `true` | run scripts at all. When it is off,<!-- only esp32 esp32-s3 --> about 40 KB of memory stay free and<!-- /only --> no script runs. Scripts stay listable, readable, editable and deletable. Only script updates (`script-update`) and script settings (`PATCH .../config`) answer `503`. Takes effect after a restart. See [System](system.md#miscellaneous) |
<!-- only esp32 esp32-s3 -->
| `pinMatrix` | integer | <!-- only esp32 -->`32`<!-- /only --><!-- only esp32-s3 -->`21`<!-- /only --> | always in use |
| `pinBtnLeft` | integer | <!-- only esp32 -->`26`<!-- /only --><!-- only esp32-s3 -->`11`<!-- /only --> | `-1` = off |
| `pinBtnSelect` | integer | <!-- only esp32 -->`27`<!-- /only --><!-- only esp32-s3 -->`12`<!-- /only --> | `-1` = off |
| `pinBtnRight` | integer | <!-- only esp32 -->`14`<!-- /only --><!-- only esp32-s3 -->`13`<!-- /only --> | `-1` = off |
| `pinBattery` | integer | <!-- only esp32 -->`34`<!-- /only --><!-- only esp32-s3 -->`1`<!-- /only --> | `-1` removes all battery fields and the Battery app |
| `pinLdr` | integer | <!-- only esp32 -->`35`<!-- /only --><!-- only esp32-s3 -->`2`<!-- /only --> | `-1` = off |
| `pinBuzzer` | integer | <!-- only esp32 -->`15`<!-- /only --><!-- only esp32-s3 -->`7`<!-- /only --> | `-1` = off |
| `pinI2cSda` | integer | <!-- only esp32 -->`21`<!-- /only --><!-- only esp32-s3 -->`8`<!-- /only --> | `-1` = off |
| `pinI2cScl` | integer | <!-- only esp32 -->`22`<!-- /only --><!-- only esp32-s3 -->`9`<!-- /only --> | `-1` = off |
| `pinDfRx` | integer | <!-- only esp32 -->`23`<!-- /only --><!-- only esp32-s3 -->`17`<!-- /only --> | checked whenever set (`≥ 0`), also with `dfplayer` off |
| `pinDfTx` | integer | `18` | checked whenever set (`≥ 0`), also with `dfplayer` off |
<!-- /only -->
<!-- only esp32-s3 -->
| `pinI2sBclk` | integer | `5` | I²S bit clock |
| `pinI2sLrclk` | integer | `6` | I²S word-select clock |
| `pinI2sDout` | integer | `4` | I²S data to the amplifier |
| `pinI2sMclk` | integer | `-1` | I²S master clock, only for amplifiers that need one |
| `pinAmpEnable` | integer | `-1` | amplifier on/off pin, set HIGH at start |
<!-- /only -->

<!-- only tc002 -->
The answer also holds a few fields that have no effect on this clock, such as `webPort` and
`artnet`. Leave them as they are.
<!-- /only -->
<!-- only esp32 esp32-s3 -->
<!-- only esp32 -->
The defaults are the pins of the Ulanzi TC001. `-1` switches a feature off. Every `pin*` field
must be `-1` or a GPIO the chip has (`0–39`). Anything else is `422 validationFailed`.
<!-- /only -->
<!-- only esp32-s3 -->
The defaults are the pins a new board starts with. `-1` switches a feature off. Every `pin*`
field must be `-1` or a GPIO the chip has (`0–48` except `22–25`). Anything else is
`422 validationFailed`.

`pinI2sBclk`, `pinI2sLrclk` and `pinI2sDout` belong together: set all three, or all three to `-1`.
Setting only some answers `422 validationFailed` naming the missing pin. `pinI2sMclk` and
`pinAmpEnable` are optional, but can only be set when the three are set: otherwise also `422`.
<!-- /only -->

#### GPIO validation

When the pin map has a problem, `PUT /api/v1/system` answers `400 invalidPinConfig` and the message
names the first problem found. Pins are checked in this order: `pinMatrix`, `pinBtnLeft`,
`pinBtnSelect`, `pinBtnRight`, `pinBattery`, `pinLdr`, `pinBuzzer`, `pinI2cSda`, `pinI2cScl`,
`pinDfRx`, `pinDfTx`<!-- only esp32-s3 -->, `pinI2sBclk`, `pinI2sLrclk`, `pinI2sDout`, `pinI2sMclk`, `pinAmpEnable`<!-- /only -->. Each
pin is first checked for range, reserved and input-only pins. After all pins pass, the ADC1 rule
and duplicates are checked.

The rules and the exact messages for each chip are on
**[GPIO & boards](gpio.md#validation-rules)**.

Pins that must be able to drive an output: `pinMatrix`, the three button pins, `pinBuzzer`,
`pinI2cSda`, `pinI2cScl`<!-- only esp32 --> and `pinDfTx`<!-- /only --><!-- only esp32-s3 -->, `pinDfTx`, `pinI2sBclk`, `pinI2sLrclk`, `pinI2sDout`,
`pinI2sMclk` and `pinAmpEnable`<!-- /only -->. `pinMatrix` is always in use. Every other pin, including `pinDfRx` and `pinDfTx`,
is in use and checked when it is `>= 0`. `dfplayer` only decides whether the DFPlayer is used, not
whether its pins are checked.
<!-- /only -->

### GET /api/v1/system/wifi-scan

Scans for Wi-Fi networks. The scan takes a moment, so ask again until results arrive.

**Request:** no body.

**Response:**

| Status | Body | Condition |
|---|---|---|
| 202 | `{"scanning":true}` | a scan was just started, or is still running: ask again |
| 200 | array of networks | the results (possibly `[]`) |

| Key | Type | Meaning |
|---|---|---|
| `ssid` | string | network name |
| `rssi` | integer | signal strength in dBm |
| `enc` | boolean | `false` for an open network |

Results are delivered once: the next request starts a new scan and answers `202` again.

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl http://<awtrix-ip>/api/v1/system/wifi-scan   # 202, then ask again
```

### GET /api/v1/logs

Reads the device log shown in the web UI console, a few lines at a time.

**Request:**

| Query param | Type | Default | Meaning |
|---|---|---|---|
| `after` | integer | `0` | return only lines with a number greater than this |

**Response:** `200`:

| Key | Type | Meaning |
|---|---|---|
| `next` | integer | number of the newest line. `0` when the log is empty |
| `lines` | array of strings | every line numbered higher than `after` |

To follow the log, pass the last `next` as `after` in the next request. AWTRIX keeps the **last 34
lines**, each up to 120 characters. Older lines are dropped. Each line starts with the time
`HH:MM:SS ` once the clock has the time from the internet, before that with the seconds since start,
`[   123s] `.

<!-- only tc002 -->
The log also shows messages from the clock's system about Wi-Fi, the network address, the time
server, updates, the speaker and restarts of AWTRIX, such as
`wifi: association rejected: wrong key`. Messages from while AWTRIX was not running (during the
start, or why it stopped) appear once it runs again, up to the last 16 lines.
<!-- /only -->

**Errors:**

| Status | Condition |
|---|---|
| 405 | wrong method: `allowed: GET` |

```bash
curl "http://<awtrix-ip>/api/v1/logs?after=0"
```

<!-- only tc002 -->
### GET /api/v1/gamepad

Reads the two gamepad slots and the phones that play.

This route exists only while [capabilities](#get-apiv1capabilities) say `"gamepad": true`.
Otherwise it answers `404`.

**Request:** no body.

**Response:** `200`:

| Key | Type | Meaning |
|---|---|---|
| `devices` | array | always two entries, slot `1` and slot `2` |
| `devices[].id` | integer | the slot, `1` or `2` |
| `devices[].state` | string | `unpaired` (empty slot), `pairing` (looking for a new one), `waiting` (paired, not connected), `connecting` or `ready` |
| `devices[].name` | string | its Bluetooth name, `""` for an empty slot |
| `devices[].address` | string | its Bluetooth address, `""` for an empty slot |
| `devices[].player` | integer or `null` | `1` or `2` while it is `ready`, otherwise `null` |
| `remotes` | array | one entry for each [phone that plays](#post-apiv1gamepadremote), `[]` when none does |
| `remotes[].session` | integer | the session number of that phone |
| `remotes[].name` | string | the phone's name |
| `remotes[].player` | integer | the player the phone plays, `1` or `2` |

A single `ready` gamepad is player `1`. With two, the one whose first input arrived first is
player `1`. The slot number is not the player number: slot `2` can be player `1`. Without
Bluetooth both slots are `unpaired`.

```bash
curl http://<awtrix-ip>/api/v1/gamepad
# {"devices":[{"id":1,"state":"ready","name":"8BitDo Ultimate","address":"E4:17:D8:BC:EC:1D","player":1},
#  {"id":2,"state":"unpaired","name":"","address":"","player":null}],
#  "remotes":[{"session":7,"name":"Pixel 8","player":2}]}
```

### POST /api/v1/gamepad/pair

Looks for a new gamepad for one minute and pairs the first one it finds into a free slot. Exists
only while [capabilities](#get-apiv1capabilities) say `"ble": true` as well.

**Request:** no body. Put the gamepad into pairing mode first. A gamepad that is paired already is
not taken again.

**Response:**

| Status | Meaning |
|---|---|
| 200 | `{"ok":true,"id":2}` at once: the slot it pairs into. Follow the progress with [`GET /api/v1/gamepad`](#get-apiv1gamepad). |
| 409 | `gamepadsFull`, message `no free slot`: both slots hold a gamepad. [Forget](#delete-apiv1gamepadid) one first. |
| 503 | `unavailable`, message `not available`: Bluetooth is not running. |

Calling it again during the search answers the same slot and does not start the minute over. The
other gamepad stays connected.

```bash
curl -X POST http://<awtrix-ip>/api/v1/gamepad/pair
```

### DELETE /api/v1/gamepad/{id}

Forgets the gamepad in slot `1` or `2`, with its pairing. The other gamepad stays connected.
Exists only while [capabilities](#get-apiv1capabilities) say `"ble": true` as well.

**Request:** no body.

**Response:** `200` `{"ok":true}` at once, also for an empty slot. Any other `{id}` answers `404`
`notFound` with the message `no such gamepad`.

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/gamepad/2
```

### POST /api/v1/gamepad/remote

Makes a phone the gamepad of one player. While it plays, `gamepad.state(player)` is `ready` and
`gamepad.name(player)` is the phone's name. The other player keeps their own gamepad or phone.
When the phone stops, that player's Bluetooth gamepad counts again. Bluetooth is not needed.

This route exists only while [capabilities](#get-apiv1capabilities) say `"gamepadRemote": true`.

**Request:** an optional JSON body:

| Key | Type | Meaning |
|---|---|---|
| `name` | string | the phone's name, up to 32 characters. `"Phone"` when missing or empty |
| `player` | integer | `1` or `2`, the player the phone plays |

Without `player` the clock picks a player that no phone and no Bluetooth gamepad plays, else one
that no phone plays, else player `1`. A phone that asks for a player another phone plays takes
it over. The token of the other phone then stops working.

**Response:**

| Status | Meaning |
|---|---|
| 200 | `{"port":4214,"token":"<32 hex digits>","player":2,"session":7}`: send the controls to this UDP port with this token |
| 400 | `invalidJson`, `invalidName` for a name that is not text or too long, or `invalidPlayer` (`must be 1..2`, `field: "player"`) for a player other than the whole number `1` or `2` |
| 503 | `unavailable`: the phone gamepad cannot start right now |

**Sending the controls:** UDP datagrams of exactly 32 bytes to the port, numbers little-endian.
Send one on every change and at least every 100 ms. After one second without a valid datagram the
controls are released and the session ends. Send a new `POST` for a new token.

| Bytes | Type | Content |
|---|---|---|
| 0 | u8 | version, `1` |
| 1–16 | bytes | the token, as 16 raw bytes |
| 17–20 | u32 | sequence number, higher than the one before. Lower or equal ones are ignored |
| 21–24 | u32 | buttons held, bit n = button number n: A 0, B 1, X 3, Y 4, L1 6, R1 7, L2 8, R2 9, SELECT 10, START 11, HOME 12, L3 13, R3 14 |
| 25 | i8 | D-pad: `-1` released, `0`–`7` clockwise from up |
| 26–29 | u8 | sticks `lx`, `ly`, `rx`, `ry`. `128` is the centre, `0` up or left |
| 30–31 | u8 | triggers `lt`, `rt`, from `0` released to `255` pulled |

Datagrams of another size, version or token are ignored.

```bash
curl -X POST http://<awtrix-ip>/api/v1/gamepad/remote \
  -H "Content-Type: application/json" \
  -d '{"name":"Pixel 8","player":2}'
# {"port":4214,"token":"3f9c0e5a7b21d4c68e0f1a2b3c4d5e6f","player":2,"session":7}
```

### DELETE /api/v1/gamepad/remote/{session}

Ends the session of one phone. Its player's Bluetooth gamepad counts again.

**Request:** no body. `{session}` is the number from
[`POST /api/v1/gamepad/remote`](#post-apiv1gamepadremote).

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 404 | `notFound`, `no such session`: no phone plays under that number |
| 405 | wrong method: `allowed: DELETE` |

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/gamepad/remote/7
```

### GET /api/v1/mqtt/tls

Reads how AWTRIX trusts the MQTT broker when it connects over TLS. See [MQTT → Connect over
TLS](../guides/mqtt.md#connect-over-tls-tc002).

This route and the two below exist only while [capabilities](#get-apiv1capabilities) say
`"mqttTls": true`. Otherwise they answer `404`.

**Request:** no body.

**Response:** `200`:

| Key | Type | Meaning |
|---|---|---|
| `ca` | string | `public`: public certificate authorities and the trusted fingerprint count. `uploaded`: only your uploaded broker CA. `unusable`: the uploaded broker CA cannot be read, and no broker is accepted until you upload or delete it |
| `pending` | string | the SHA-256 of a broker certificate AWTRIX refused and you can trust with [`mqttTlsPin`](system.md#mqtt-and-home-assistant). `null` when there is none |

```bash
curl http://<awtrix-ip>/api/v1/mqtt/tls
# {"ca":"public","pending":"ce781fea…c798"}
```

### PUT /api/v1/mqtt/tls/ca

Uploads the certificate of your own certificate authority for the MQTT broker. From the next
connection attempt on, AWTRIX accepts only a broker with a certificate from this authority that
names `mqttHost`.

**Request:**

| Key | Type | Meaning |
|---|---|---|
| `certificate` | string | one or more CA certificates in PEM, at most 65536 bytes |

**Response:** `200` with the same body as [`GET /api/v1/mqtt/tls`](#get-apiv1mqtttls).

**Errors:**

| Status | Condition |
|---|---|
| 422 | `validationFailed`: `invalid certificate`, `expected a PEM string` or `at most 65536 bytes`, `field` is `certificate` |
| 507 | `insufficientStorage`: `not saved` |

```bash
jq -n --rawfile certificate ca.crt '{$certificate}' |
  curl -X PUT http://<awtrix-ip>/api/v1/mqtt/tls/ca -H "Content-Type: application/json" -d @-
```

### DELETE /api/v1/mqtt/tls/ca

Deletes the uploaded broker CA, also an unusable one. Public certificate authorities and the
trusted fingerprint count again.

**Request:** no body.

**Response:** `200` with the same body as [`GET /api/v1/mqtt/tls`](#get-apiv1mqtttls).

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/mqtt/tls/ca
```

### GET /api/v1/voice {#apiv1voice}

Reads the [Home Assistant Voice](../guides/voice.md) settings and connection state.

This route and `POST` below exist only while [capabilities](#get-apiv1capabilities) say
`"voice": true`. Otherwise they answer `404`.

**Request:** no body.

**Response:** `200`:

| Key | Type | Meaning |
|---|---|---|
| `config.enabled` | boolean | Voice is switched on |
| `config.url` | string | the Home Assistant address, `""` when none is set |
| `config.pipeline` | string | the chosen Assist pipeline ID, `""` for Home Assistant's default |
| `config.device` | string | the Home Assistant device ID whose area is the clock's room, `""` when none is set |
| `config.tokenSet` | boolean | an access token is saved. The token itself is never returned |
| `state` | string | `offline`, `connecting`, `ready`, `starting`, `listening`, `processing`, `speaking` or `error` |
| `error` | string | why the last attempt failed, as a code such as `tokenRejected`. `""` when all is well. The web UI shows each code as a sentence |
| `pipelines` | array | Home Assistant's Assist pipelines once connected. `[]` before |

```bash
curl http://<awtrix-ip>/api/v1/voice
```

### POST /api/v1/voice

Changes the Home Assistant Voice settings.

**Request:** any of `enabled`, `url`, `pipeline`, `device`, `token`, or `"clearToken": true` to
delete the token. Fields you leave out keep their value.

* `device` is a Home Assistant device ID (letters and digits, up to 64), `""` for none.

* `enabled: true` needs an address and a token.
* After you change the address, send the token again.
* With `"enabled": false` the clock closes its connection to Home Assistant, and a long press on
  the knob does nothing.

This route accepts requests only from the device's own web UI: it needs the header
`X-Awtrix-Voice: 1` and an `Origin` header naming the device, as a browser sends it. This keeps
other web pages and scripts from sending your voice to another server.

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 403 | `forbiddenOrigin`: not sent from the device's own web UI |
| 422 | `validationFailed`: `field` names the wrong value: `url`, `token`, `pipeline`, `device` or `enabled` |
| 500 | `internalError`: the settings could not be saved |

```bash
curl -X POST http://<awtrix-ip>/api/v1/voice \
  -H "Content-Type: application/json" \
  -H "X-Awtrix-Voice: 1" \
  -H "Origin: http://<awtrix-ip>" \
  -d '{"enabled":false}'
```
<!-- /only -->

---

## Files

The storage holds your icons, melodies, palettes and MP3s.<!-- only esp32 esp32-s3 --> Its size depends on the flash memory of
the board.<!-- /only --> See [Limits](limits.md#storage). `GET /api/v1/files` reports the real values as
`usedBytes` and `totalBytes`.

### GET /api/v1/files

Lists the files in a folder. File and script-group ordering is unspecified. Clients that need
a particular order should sort the returned names. This also applies to melody, MP3 and
script-sound listings.

**Request:**

| Query param | Type | Default | Meaning |
|---|---|---|---|
| `dir` | string | `/ICONS` | folder to list. A missing leading `/` is added |

**Response:** `200`:

| Key | Type | Meaning |
|---|---|---|
| `files` | array | `{name: string, size: integer}` per file |
| `usedBytes` | integer | storage in use, in bytes |
| `totalBytes` | integer | storage size, in bytes |

A `dir` that does not exist, or is not a folder, answers **200 with an empty `files` list**, not
404.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidPath`: `dir` contains `..` or characters not allowed in a path |

```bash
curl "http://<awtrix-ip>/api/v1/files?dir=/MELODIES"
```

### POST /api/v1/files

Uploads files into one of the asset folders.

**Request:** a `multipart/form-data` upload. The name of the form field does not matter. Every file
in it is taken.

| Query param | Type | Default | Meaning |
|---|---|---|---|
| `dir` | string | `/ICONS` | target folder: **ignored** when the file name starts with `/` |

Where the file goes:

* a file name that starts with `/` is the full path. `?dir=` is ignored,
* otherwise the path is `<dir>/<filename>` (a missing `/` in front of `dir` is added),
* the path must be inside `/ICONS`, `/MELODIES`, `/PALETTES` or `/MP3` and must not contain `..`,
* a file for `/MP3` must be named the way you play it: 1–32 characters of `A-Za-z0-9_-` plus
  `.mp3`,
* an MP3 in `/MP3` and a melody in `/MELODIES` never share a name,
* a missing folder is created.

The content must fit the folder: `/ICONS` takes GIF or JPEG (not PNG), `/MELODIES` RTTTL text,
`/PALETTES` one `RRGGBB` color per line, `/MP3` MP3 files. Empty files are refused.

A refused file never replaces the file that was there. When one request carries several files,
files saved before a failing one stay saved, and the request reports the failure.

**Response:** `200` `{"ok":true}`. Check the result with `GET /api/v1/files`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidPath`: the target is outside the four asset folders, or contains `..` |
| 400 | `invalidName`: a `/MP3` file whose name is not `[A-Za-z0-9_-]{1,32}.mp3` |
| 400 | `badRequest`: no file arrived, or the upload was interrupted or incomplete |
| 401 | authentication failed: answered after the whole upload was sent |
| 403 | `forbidden`: uploads are not allowed in setup mode |
| 409 | `nameTaken`, `name taken`: an MP3 for `/MP3` whose name a melody has, or a melody for `/MELODIES` whose name an MP3 has |
| 415 | `unsupportedMediaType`: the content does not fit the folder: `/ICONS` needs a GIF or JPEG, `/MELODIES` valid RTTTL text, `/PALETTES` plain `RRGGBB` lines, `/MP3` an MP3 (with an ID3 tag or MP3 frame) |
| 507 | `insufficientStorage`, `write failed`: the file could not be saved. The previous version is kept |

```bash
curl -X POST "http://<awtrix-ip>/api/v1/files?dir=/ICONS" \
  -F "file=@1234.jpg"
```

### DELETE /api/v1/files

Deletes one file from the asset folders.

**Request:**

| Query param | Type | Required | Meaning |
|---|---|---|---|
| `path` | string | yes | full path of the file. Must be inside `/ICONS`, `/MELODIES`, `/PALETTES` or `/MP3` and not contain `..` |

Deleting an icon also removes its [Hub link](#icon-origins).

**Response:** `200` `{"ok":true}`.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidPath`: `path` missing, contains `..`, or is outside the four asset folders |
| 404 | `notFound`, `file not found`: the path is valid but the file does not exist |
| 405 | wrong method: `allowed: GET, POST, DELETE` |

```bash
curl -X DELETE "http://<awtrix-ip>/api/v1/files?path=/ICONS/1234.jpg"
```

### POST /api/v1/icons/rename

Renames one icon. Its [Hub link](#icon-origins) moves to the new name.

**Request:** a JSON body:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `from` | string | yes | the current file name, for example `mail.gif` |
| `to` | string | yes | the new file name: 1 to 32 letters, digits, `_` or `-`, and the same extension |

Apps, notifications and scripts that use the old name show no icon until you change them.

**Response:** `200` `{"ok":true}`, also when `from` and `to` are the same.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidJson`: the body is not a JSON object |
| 400 | `invalidName`: a name is not valid, or `to` has a different extension |
| 404 | `notFound`: the icon does not exist |
| 409 | `nameTaken`: an icon with the new name exists, as `.gif` or `.jpg` |
| 500 | `internalError` or `storageError`: renaming failed. The icon keeps its name |
| 403 | `forbidden`: not allowed in setup mode |

```bash
curl -X POST http://<awtrix-ip>/api/v1/icons/rename   -H "Content-Type: application/json"   -d '{"from":"mail.gif","to":"letter.gif"}'
```

---

## Firmware upload

### POST /update

Installs a new firmware. The web UI uses this route too.

**Request:** a `multipart/form-data` upload of one firmware file.

<!-- only esp32 esp32-s3 -->
The file is the firmware `.bin` file. It is checked first. The running firmware is replaced only
after the whole file has arrived and passed the check, so a refused or interrupted upload leaves
your device as it was.

**Response:** `200` `{"ok":true}`, then AWTRIX restarts with the new firmware.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `badRequest`: no file arrived, more than one file was sent, or the upload was incomplete |
<!-- only esp32 -->
| 400 | `wrongChip`: the file is for another chip, is a `usb-*.bin` meant for the first install over USB, or is not a firmware file |
<!-- /only -->
<!-- only esp32-s3 -->
| 400 | `wrongChip`: the file is for another chip, for the other kind of PSRAM (quad vs octal), is a `usb-*.bin` meant for the first install over USB, or is not a firmware file |
<!-- /only -->
| 401 | authentication failed |
| 403 | `forbidden`: firmware upload is not allowed in setup mode. `forbiddenOrigin`: the request came from another website ([details](#cross-origin)) |
| 500 | `internalError`, `update failed`: the firmware could not be written |
<!-- /only -->
<!-- only esp32 -->

```bash
curl -X POST http://<awtrix-ip>/update -F "firmware=@firmware-awtrix-ng.bin"
```
<!-- /only -->
<!-- only esp32-s3 -->

```bash
curl -X POST http://<awtrix-ip>/update -F "firmware=@firmware-awtrix-ng-s3-octal.bin"
```
<!-- /only -->
<!-- only tc002 -->
The file is the update file `awtrix-ng-tc002.awup`, at most 8 MiB, in the field `firmware`. The
clock answers first, then installs it and restarts. The new version must keep running for one
minute. If it fails to start three times, the clock shows USB RECOVERY and can be installed again
over USB.

**Response:** `200` `{"ok":true,"applying":true}`: the clock installs the update and restarts.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `badRequest`: not exactly one file in the field `firmware`. `invalidPackage`: damaged, incomplete or unsupported file. `wrongTarget`: a file made for another device |
| 401 | authentication failed |
| 403 | `forbiddenOrigin`: the request came from another website ([details](#cross-origin)) |
| 409 | `notNewer`: not newer than the installed version. `insufficientStorage`: the file is larger than the clock can hold, the message names both sizes. `updateBusy`: another update is running or still being confirmed |
| 413 | `payloadTooLarge`: larger than 8 MiB. `insufficientMemory`: not enough free memory to take the file |

```bash
curl -X POST http://<awtrix-ip>/update -F "firmware=@awtrix-ng-tc002.awup"
```
<!-- /only -->

---

## Backup restore

### POST /api/v1/restore

Restores a backup made in the web UI.

**Request:** a `multipart/form-data` upload of the backup `.zip`, as the web UI creates it
(uncompressed).

* It **works in setup mode**, so a new or reset device gets its Wi-Fi and settings back from the
  backup alone.
* Authentication works as for [`POST /api/v1/files`](#post-apiv1files). A device without a login
  (such as a new device in setup mode) accepts the restore. A device with `authEnabled` needs the
  credentials.
* `manifest.json` must be the first entry. It must name the app `awtrix-ng` and a supported
  `backupFormat`, or nothing is restored.

| Entry | Effect |
|---|---|
| `manifest.json` | must be first. If it is not valid, the whole backup is refused |
| `config/wifi.json` | `{wifiSsid, wifiPass}` added to the Wi-Fi configuration |
| `config/system.json` | the rest of the device configuration, checked like [`PUT /api/v1/system`](#put-apiv1system) |
| `config/settings.json` | display and behavior settings, checked like [`PATCH /api/v1/settings`](#patch-apiv1settings): applied at once |
| `config/icon-origins.json` | the [icon Hub links](#icon-origins). Links to icons that are not there are dropped |
| `apploop.json` | app order and switched-off apps, same format as [`PUT /api/v1/apps/order`](#put-apiv1appsorder) |
| `ICONS/*`, `MELODIES/*`, `PALETTES/*`, `MP3/*`, `SCRIPTS/*` | written into the matching folder |
| `SCRIPTS/<name>/<sound>.mp3` | a script's [sound](#script-sounds), checked like any MP3. No other file under `SCRIPTS/` may be in a folder |

Each file is checked like an upload to [`POST /api/v1/files`](#post-apiv1files). A damaged entry,
an entry outside its folder, content that does not fit its folder, or an unknown entry is
**skipped with a warning**. The rest of the restore continues. A skipped file never replaces the
file that was there. Entries already restored stay restored when a later one fails. Size limits:
[Limits](limits.md#backup-restore).

**Response:** this route answers with its own format, not the usual [error body](#errors), because
a restore can partly succeed.

| Status | Body | Condition |
|---|---|---|
| 200 | `{"ok":true,"applied":{...},"warnings":[...]}` | the backup had a valid manifest: even if every other entry was skipped |

`applied` counts the restored entries per kind: `wifi`, `system`, `settings`, `appLoop`,
`radioStations`, `icons`, `iconOrigins`, `melodies`, `palettes`, `mp3` (including script sounds),
`scripts`, and `skipped` for refused entries. `warnings` has one line per skipped entry.

New Wi-Fi credentials and `config/system.json` take effect after a restart:
[`POST /api/v1/device/reboot`](#post-apiv1devicereboot), which also works in setup mode.

**Errors:**

| Status | Body | Condition |
|---|---|---|
| 400 | `{"ok":false,"error":"..."}` | the backup was refused: not a zip, no `manifest.json`, a manifest for a different app, or an unsupported `backupFormat` |
| 400 | `badRequest` error body | no file arrived, or the upload was interrupted |
| 401 | `unauthorized` error body | authentication failed |
| 403 | `forbidden` error body | in setup mode: the request came from another web origin or host, or carried a `secrets` parameter |
| 403 | `forbiddenOrigin` error body | the request came from another website ([details](#cross-origin)) |
<!-- only esp32 esp32-s3 -->
| 404 | `notFound` error body, `unknown route` | a method other than `POST` |
| 507 | `insufficientStorage` error body | the backup was restored but could not be saved yet. Free storage and repeat the restore before rebooting |
<!-- /only -->
<!-- only tc002 -->
| 405 | `methodNotAllowed` error body, `allowed: POST` | a method other than `POST` |
| 507 | `insufficientStorage` error body | the backup was restored but could not be saved yet. Free storage and repeat the restore before rebooting. AWTRIX also retries saving by itself |
<!-- /only -->

```bash
curl -X POST http://<awtrix-ip>/api/v1/restore -F "file=@backup.zip"
```

---

## Icon origins

An icon origin remembers which [AWTRIX Hub](../guides/icons.md) icon a local icon was installed
from. The links are kept after a restart and are in every backup.

All three routes need authentication when it is on. All are refused in setup mode, including `GET`.
Other methods answer `405 methodNotAllowed` outside setup mode.

### GET /api/v1/icons/origins

Lists the links between installed icons and their Hub originals.

**Request:** no body.

**Response:** `200`, with `Cache-Control: no-store`. Links to icons that are missing on the
device are left out. At most 64 links are kept, so there are no pages.

```json
{"icons":[{"name":"mail.gif","hub":"https://awtrix.de/icons/","slug":"mail","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}]}
```

`sha256` is the checksum of the local file at the time the link was saved, sent by whoever saved
it. It does not prove who made the icon and does not allow publishing it. Replacing the file keeps
the link, so compare `sha256` with the current file before calling it an unchanged Hub copy.

```bash
curl http://<awtrix-ip>/api/v1/icons/origins
```

### PUT /api/v1/icons/origins

Saves or replaces the link for one icon.

**Request:** a JSON body of at most 1024 bytes with the four fields shown above:

* `name`: the full file name, matching `[A-Za-z0-9_-]{1,32}\.(gif|jpg)`, of an icon that exists,
* `slug`: matching `[a-z0-9_-]{1,32}`,
* `hub`: an `https://` address ending in `/icons/`, at most 240 characters, without user name,
  query, `#` part, `%` escapes or `.`/`..` path parts. Other hosts, ports and path prefixes are
  allowed,
* `sha256`: the checksum of the local file.

The four fields must each appear once and be text. Unknown fields are ignored.

**Response:** `200` `{"ok":true}`. Sending the same request again is safe. A new link for the same
file replaces the old one.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidOrigin`: a field is missing or not valid |
| 404 | `notFound`: the icon does not exist |
| 413 | `payloadTooLarge`: the body is larger than 1024 bytes |
| 507 | `insufficientStorage`: 64 links or 16 KiB of link data are reached |
| 500 | `storageError`: the links could not be read or saved |
| 403 | `forbidden`: not allowed in setup mode |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/icons/origins \
  -H "Content-Type: application/json" \
  -d '{"name":"mail.gif","hub":"https://awtrix.de/icons/","slug":"mail","sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}'
```

### DELETE /api/v1/icons/origins?name=mail.gif

Removes the link for one icon. The icon itself stays.

**Request:** no body. `name` is the full file name.

**Response:** `200` `{"ok":true}`, also when there was no link.

Deleting the icon with [`DELETE /api/v1/files`](#delete-apiv1files) removes its link too. If the
link cannot be removed, the icon is not deleted.

**Errors:**

| Status | Condition |
|---|---|
| 400 | `invalidName`: not a valid icon file name |
| 500 | `storageError`: the links could not be saved |
| 403 | `forbidden`: not allowed in setup mode |

```bash
curl -X DELETE "http://<awtrix-ip>/api/v1/icons/origins?name=mail.gif"
```

## Web UI and static assets

### GET /

Opens the web UI. The same page is at `/`, `/index.html` and `/fullscreen`.

`/fullscreen` shows only the live display, scaled to fill the window in whole-pixel steps: made
for embedding in a dashboard.

**Request:** no body. Any HTTP method returns the page.

**Response:**

| Status | Condition |
|---|---|
| 200 | `text/html` with `Content-Encoding: gzip`, `ETag: <build etag>`, `Cache-Control: no-cache` |
| 304 | the browser's `If-None-Match` matches the `ETag`. Empty body |

**Errors:**

| Status | Condition |
|---|---|
| 401 | authentication is on and the credentials are missing or wrong |

```bash
curl --compressed http://<awtrix-ip>/ -o index.html
```

### GET /ICONS/*, /MELODIES/*, /PALETTES/*, /MP3/*, /SCRIPTS/*, /apploop.json

Downloads a stored file.

**Request:** no body. `GET` only.

* `/ICONS/`, `/MELODIES/`, `/PALETTES/` and `/MP3/` work in every mode.
* `/SCRIPTS/*`, `/apploop.json` and `/radio.json` (the station list) work only outside setup mode.
* A script's [sound](#script-sounds) is at `/SCRIPTS/<name>/<sound>.mp3`, ready to play in a
  browser.
* A path containing `..` is refused.

**Response:**

| Status | Condition |
|---|---|
| 200 | the file, with an `ETag` and `Cache-Control: no-cache` |
| 304 | the browser's `If-None-Match` matches the file's `ETag`. Empty body |

The content type follows the file extension: `.jpg`/`.jpeg` → `image/jpeg`, `.gif` → `image/gif`,
`.png` → `image/png`, `.txt` → `text/plain`, `.mp3` → `audio/mpeg`, anything else →
`application/octet-stream`.

**Errors:**

| Status | Condition |
|---|---|
| 401 | authentication is on and the credentials are missing or wrong |
| 404 | `notFound`, `file not found`: the file does not exist, or the path is a folder. A folder name without the closing `/`, such as `/ICONS`, is no route and answers `unknown route` |

```bash
curl http://<awtrix-ip>/ICONS/1234.jpg -o 1234.jpg
```

### Unknown routes

Any other path answers **404** `notFound` with the message `unknown route`.

---

## Route index

| Method | Path | Notes |
|---|---|---|
| GET | `/api/v1/device` | [state & statistics](#get-apiv1device) |
| GET | `/api/v1/version` | [JSON version](#get-apiv1version) |
| GET | `/version` | [plain-text version](#get-version) |
| POST | `/api/v1/device/reboot` | [200, then restarts](#post-apiv1devicereboot) |
<!-- only esp32 esp32-s3 -->
| POST | `/api/v1/device/sleep` | [200, then sleeps](#post-apiv1devicesleep) |
<!-- /only -->
| POST | `/api/v1/device/factory-reset` | [200, then erases everything](#post-apiv1devicefactory-reset) |
| GET | `/api/v1/settings` | [all settings](#get-apiv1settings) |
| PATCH | `/api/v1/settings` | [returns all settings](#patch-apiv1settings) |
| POST | `/api/v1/settings/reset` | [200, then resets](#post-apiv1settingsreset) |
| GET | `/api/v1/display` | [power, brightness, overlay, mood light](#get-apiv1display) |
| PATCH | `/api/v1/display` | [power / overlay](#patch-apiv1display) |
| PUT | `/api/v1/display/moodlight` | [one color on the whole display; `{}` → 422](#put-apiv1displaymoodlight) |
| DELETE | `/api/v1/display/moodlight` | [off](#delete-apiv1displaymoodlight) |
| GET | `/api/v1/display/screen` | [current picture](#get-apiv1displayscreen) |
| GET | `/api/v1/apps` | [all apps](#get-apiv1apps) |
| PUT | `/api/v1/apps/active` | [show an app](#put-apiv1appsactive) |
| POST | `/api/v1/apps/next` | [next](#post-apiv1appsnext) |
| POST | `/api/v1/apps/previous` | [previous](#post-apiv1appsprevious) |
| PUT | `/api/v1/apps/order` | [which apps are on, and in what order](#put-apiv1appsorder) |
| PUT | `/api/v1/apps/{name}/enabled` | [one app on or off](#put-apiv1appsnameenabled) |
| PUT | `/api/v1/apps/pushed/{name}` | [`{}` → 422; 507 at the limit](#put-apiv1appspushedname) |
| DELETE | `/api/v1/apps/{name}` | [any kind of app; safe to repeat](#delete-apiv1appsname) |
| GET | `/api/v1/apps/script/{name}` | [script source, `text/plain`](#get-apiv1appsscriptname) |
| PUT | `/api/v1/apps/script-update/{name}` | [update a script only if unchanged](#script-updates) |
| PUT | `/api/v1/apps/script/{name}` | [body is the script source; an error is still a 200](#put-apiv1appsscriptname) |
| GET | `/api/v1/apps/{name}/config` | [a script's settings](#get-apiv1appsnameconfig) |
| PATCH | `/api/v1/apps/{name}/config` | [change a script's settings](#patch-apiv1appsnameconfig) |
| GET | `/api/v1/apps/builtin/{name}/config` | [a built-in app's settings](#get-apiv1appsbuiltinnameconfig) |
| PATCH | `/api/v1/apps/builtin/{name}/config` | [change a built-in app's settings](#patch-apiv1appsbuiltinnameconfig) |
| GET | `/api/v1/apps/{name}/data` | [a script's stored data](#get-apiv1appsnamedata) |
| PATCH | `/api/v1/apps/{name}/data` | [change a script's stored data](#patch-apiv1appsnamedata) |
| GET | `/api/v1/apps/script/{name}/sounds` | [a script's sounds, with SHA-256](#get-apiv1appsscriptnamesounds) |
| POST | `/api/v1/apps/script/{name}/sounds` | [upload one sound](#post-apiv1appsscriptnamesounds) |
| DELETE | `/api/v1/apps/script/{name}/sounds/{sound}` | [delete one sound](#delete-apiv1appsscriptnamesoundssound) |
| DELETE | `/api/v1/apps/script/{name}/sounds` | [delete all of a script's sounds](#delete-apiv1appsscriptnamesounds) |
| GET | `/api/v1/scripts/shared` | [values scripts share](#get-apiv1scriptsshared) |
| POST | `/api/v1/notifications` | [show a notification](#post-apiv1notifications) |
| DELETE | `/api/v1/notifications/active` | [dismiss the one shown](#delete-apiv1notificationsactive) |
| DELETE | `/api/v1/notifications/{name}` | [dismiss by name, anywhere in the queue](#delete-apiv1notificationsname) |
| PUT | `/api/v1/indicators/{id}` | [indicator lights](#put-apiv1indicatorsid) |
| DELETE | `/api/v1/indicators/{id}` | [switch an indicator off](#delete-apiv1indicatorsid) |
| GET | `/api/v1/audio` | [what is playing<!-- only esp32-s3 tc002 -->, and the station list<!-- /only -->](#get-apiv1audio) |
| POST | `/api/v1/audio/play` | [a sound object: <!-- only esp32 -->file/rtttl/track<!-- /only --><!-- only esp32-s3 -->file/rtttl/track/station<!-- /only --><!-- only tc002 -->file/rtttl/song/speech/station<!-- /only -->, optional `loop`, or a list](#post-apiv1audioplay) |
| POST | `/api/v1/audio/stop` | [everything, or one `group`: alert/app<!-- only esp32-s3 tc002 -->/radio<!-- /only -->](#post-apiv1audiostop) |
<!-- only tc002 -->
| POST | `/api/v1/audio/clip` | [body is a WAV or MP3 file, played once](#post-apiv1audioclip) |
<!-- /only -->
| GET | `/api/v1/audio/melodies` | [all melodies](#get-apiv1audiomelodies) |
| PUT | `/api/v1/audio/melodies/{name}` | [save; the title becomes `{name}`](#put-apiv1audiomelodiesname) |
| DELETE | `/api/v1/audio/melodies/{name}` | [404 if missing](#delete-apiv1audiomelodiesname) |
| GET | `/api/v1/audio/mp3` | [stored MP3s, and each script's sounds](#get-apiv1audiomp3) |
| POST | `/api/v1/audio/mp3` | [upload an MP3](#post-apiv1audiomp3) |
| DELETE | `/api/v1/audio/mp3/{name}` | [delete an MP3](#delete-apiv1audiomp3name) |
| POST | `/api/v1/audio/mp3/rename` | [rename an MP3](#post-apiv1audiomp3rename) |
<!-- only esp32-s3 tc002 -->
| GET | `/api/v1/audio/stations` | [stored stations](#get-apiv1audiostations) |
| PUT | `/api/v1/audio/stations` | [replaces the whole list](#put-apiv1audiostations) |
<!-- /only -->
| GET | `/api/v1/capabilities` | [what this device supports](#get-apiv1capabilities) |
| GET | `/api/v1/system` | [device configuration](#get-apiv1system) |
| PUT | `/api/v1/system` | [change configuration<!-- only esp32 esp32-s3 -->; pins checked<!-- /only -->](#put-apiv1system) |
| GET | `/api/v1/system/wifi-scan` | [202 while scanning](#get-apiv1systemwifi-scan) |
| GET | `/api/v1/logs` | [device log](#get-apiv1logs) |
<!-- only tc002 -->
| GET | `/api/v1/gamepad` | [both gamepads and their players](#get-apiv1gamepad) |
| POST | `/api/v1/gamepad/pair` | [pair a gamepad into a free slot](#post-apiv1gamepadpair) |
| DELETE | `/api/v1/gamepad/{id}` | [forget one gamepad](#delete-apiv1gamepadid) |
| POST | `/api/v1/gamepad/remote` | [make a phone a gamepad](#post-apiv1gamepadremote) |
| DELETE | `/api/v1/gamepad/remote/{session}` | [end one phone's session](#delete-apiv1gamepadremotesession) |
| GET | `/api/v1/mqtt/tls` | [how the MQTT broker is trusted](#get-apiv1mqtttls) |
| PUT | `/api/v1/mqtt/tls/ca` | [upload the broker's CA](#put-apiv1mqtttlsca) |
| DELETE | `/api/v1/mqtt/tls/ca` | [delete the broker's CA](#delete-apiv1mqtttlsca) |
| GET | `/api/v1/voice` | [Home Assistant Voice state](#apiv1voice) |
| POST | `/api/v1/voice` | [change Home Assistant Voice](#post-apiv1voice) |
| GET | `/api/v1/oauth` | [scripts that sign in](#get-apiv1oauth) |
| GET, POST, DELETE | `/api/v1/oauth/{name}` | [a script's sign-in](#get-apiv1oauthname) |
| POST | `/api/v1/oauth/{name}/start` | [start a sign-in](#post-apiv1oauthnamestart) |
| POST | `/api/v1/oauth/{name}/code` | [finish a sign-in](#post-apiv1oauthnamecode) |
<!-- /only -->
| GET | `/api/v1/files` | [list](#get-apiv1files) |
| POST | `/api/v1/files` | [upload](#post-apiv1files) |
| DELETE | `/api/v1/files` | [by `?path=`, asset folders only](#delete-apiv1files) |
| POST | `/api/v1/icons/rename` | [rename an icon](#post-apiv1iconsrename) |
| GET, PUT, DELETE | `/api/v1/icons/origins` | [Hub links of icons](#icon-origins) |
| POST | `/update` | [firmware](#post-update) |
| POST | `/api/v1/restore` | [backup ZIP; works in setup mode](#post-apiv1restore) |
| GET | `/`, `/index.html`, `/fullscreen` | [web UI](#get) |
| GET | `/ICONS/*`, `/MELODIES/*`, `/PALETTES/*`, `/MP3/*`, `/SCRIPTS/*`, `/apploop.json` | [stored files](#web-ui-and-static-assets) |

## Related

* [Conventions](conventions.md): keys, durations, colors and the error body
* [Errors](errors.md): every error code
* [App & notification payload](payload.md): the fields for pushed apps and notifications
* [Settings](settings.md) and [System configuration](system.md): every key
* [MQTT](mqtt.md): the same commands over MQTT
* [Limits](limits.md): sizes and counts
