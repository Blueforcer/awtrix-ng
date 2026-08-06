# Errors

Every failing request on the HTTP API answers `application/json` in the same shape, with one
exception: `POST /api/v1/restore` answers with its own result shape - see
[Restore result](#restore-result). [Error codes](#error-codes) lists every code the API returns.

## The error body

```json
{
  "error": {
    "code": "validationFailed",
    "message": "out of range",
    "field": "brightness"
  }
}
```

| Key | Type | Always present | Meaning |
| --- | --- | --- | --- |
| `error.code` | string | yes | Stable machine-readable identifier. Match on this, never on `message`. |
| `error.message` | string | yes | Human-readable reason. The exact wording may change between firmware versions - do not match on it. |
| `error.field` | string | **no** | The offending key. Emitted **only** when AWTRIX knows which field failed - otherwise the key is omitted entirely (never `null`, never `""`). |

## Restore result

`POST /api/v1/restore` does not use the shape above. Every response to it - success or failure
- is this result object instead:

```json
{
  "ok": true,
  "applied": {
    "wifi": 1, "system": 1, "settings": 1, "appLoop": 3, "radioStations": 0,
    "icons": 2, "melodies": 0, "palettes": 1, "scripts": 0, "skipped": 0
  },
  "warnings": []
}
```

| Key | Type | Meaning |
| --- | --- | --- |
| `ok` | boolean | Whether the archive was accepted and applied. |
| `error` | string | Present only when `ok` is `false` and the whole archive was rejected outright (for example, no `manifest.json`). A plain message, not an `error.code`. |
| `applied` | object | Per-category counts of what was restored, present whether or not the restore succeeded. |
| `warnings` | array of strings | Non-fatal problems with individual entries (for example, a file that could not be finalized); the restore still proceeds. |

The status is `200` when `ok` is `true` and `400` when it is `false`.

One case answers in the standard error body instead: if the upload finishes without ever
receiving a backup file, the response is `400` with
`{"error":{"code":"badRequest","message":"no backup file received"}}` - there is nothing to report
counts for.

## Error codes

| Code | Status | Emitted by | Meaning |
| --- | --- | --- | --- |
| `invalidJson` | 400 | any route with a body | The body is not parseable JSON. On the routes where an empty body is a parse error, a missing/empty body lands here too - see [Content-Type](#content-type-the-empty-body-trap). |
| `invalidPinConfig` | 400 | `PUT /api/v1/system` only | The merged GPIO map is unusable. Nothing was saved. See [GPIO validation](#gpio-validation-invalidpinconfig). |
| `invalidPath` | 400 | `POST` / `DELETE /api/v1/files` | The filename or path is outside `/ICONS`, `/MELODIES`, `/PALETTES`, or contains `..`. Nothing was written or removed. |
| `invalidName` | 400 | every route that names an app in its path | The name does not match `[A-Za-z0-9_-]{1,32}`. Checked before the payload is parsed. Carries `field: "name"`. |
| `invalidMethodOverride` | 400 | routing, when `X-HTTP-Method-Override` is present | The header sits on something other than a `POST`, names anything but `PUT`/`PATCH`/`DELETE`, or tries to reach the raw script upload. Nothing was routed. See [Method override](http.md#method-override). |
| `badRequest` | 400 | `POST /api/v1/restore` only | The multipart upload finished without a backup file ever being received. See [Restore result](#restore-result). |
| `wrongChip` | 400 | `POST /update` (firmware upload) | The upload is not an update image for this device: built for a different chip, a `factory-*.bin` meant for a first flash over USB, or not a firmware image at all. `message` says which. Nothing reached flash. |
| `unauthorized` | 401 | every route, when auth is on | Missing or wrong HTTP Basic credentials. See [Authentication](#authentication-401). |
| `forbidden` | 403 | uploads and write routes, AP mode only | The request is not permitted during provisioning - the setup AP exposes reads, Wi-Fi setup and a reboot only. See [Provisioning lockdown](#provisioning-lockdown-403). |
| `notFound` | 404 | routing and several handlers | Unknown route, or a named app/sound/file that does not exist. |
| `methodNotAllowed` | 405 | routing | The path exists but not for this method. `message` always lists the allowed ones. |
| `unsupportedMediaType` | 415 | `PUT` / `PATCH` with a body, except a script upload; `POST /api/v1/files` | Either the `Content-Type` is not `application/json` (see [Content-Type](#content-type-the-empty-body-trap)), or an uploaded file's leading bytes do not suit the target folder - `/ICONS` takes GIF and JPEG, `/MELODIES` and `/PALETTES` take text. A rejected upload stores nothing. |
| `payloadTooLarge` | 413 | any route with a body | The body is over the ceiling for that route ([Limits](limits.md#requests)). Nothing was read. A script source upload carries `field: "source"`. |
| `validationFailed` | 422 | write routes | The JSON parsed, but a value is invalid. Usually carries `field`. **Nothing was applied.** |
| `insufficientStorage` | 507 | pushed apps, notification queue, script store, or a large request body | A store or queue is full ([Limits](limits.md#apps-and-notifications)); the request was rejected instead of silently dropped. Also returned when AWTRIX is too low on memory to act on a large body - a small body (a bare reboot, a short JSON) is never refused this way. Carries `field: "name"` when a script install hit `scriptLimit`, `field: "source"` when there was not enough memory to receive the source at all. |
| `internalError` | 500 | dispatch fallback, OTA, uploads | The command reached AWTRIX and failed. A failed firmware flash reads `firmware update failed (bad image or storage full)`. |
| `unavailable` | 503 | `GET /api/v1/apps/script/{name}`, `GET /api/v1/scripts/shared`, `POST /api/v1/radio/play`, `POST /api/v1/radio/stop` | The build has no scripting platform (so there is no source to answer with) or no audio output. A `PUT` on the script path answers `500 internalError` instead. |
| `serviceBusy` | 503 | script install, radio play | A transient refusal, distinct from a hard `insufficientStorage` capacity limit; retry shortly. The response carries `Retry-After: 2`. Carries `field: "name"` on a script install and `field: "url"` when a radio stream is refused for lack of memory. |

`field` appears on five codes only, so do not write clients that require it. `invalidName` always
carries `field: "name"`. `payloadTooLarge` and `insufficientStorage` carry it on the script routes,
and `serviceBusy` carries `"name"` (script install) or `"url"` (radio play). `validationFailed`
carries it for every rejected *value*, but not when a route rejects a missing body - there is no
offending key to name.

The values behind `payloadTooLarge` and `insufficientStorage` - how large a body may be, how many
apps, notifications and scripts fit - are collected in [Limits](limits.md).

## Status codes

| Status | Applied? | Notes |
| --- | --- | --- |
| 200 | yes | `{"ok":true}` on most write routes. `PATCH /api/v1/settings` instead returns the **full resulting settings object**. |
| 400 | no | Nothing parsed, nothing applied. |
| 401 | no | AWTRIX never acts on it. |
| 403 | no | Provisioning lockdown; AWTRIX never acts on it. |
| 404 | no | - |
| 405 | no | AWTRIX never acts on it. |
| 415 | no | Wrong `Content-Type` on a `PUT`/`PATCH`, rejected before the body is parsed; or an upload whose content does not match the folder, rejected on its first chunk. |
| 422 | **no** | Validation is validate-then-apply: the first offending key aborts the whole request. A `PATCH` is all-or-nothing. |
| 500 | maybe | The command ran and reported failure. |
| 503 | no | `GET /api/v1/apps/script/{name}` on a build without the scripting platform. |
| 507 | no | A store or queue was full; the write was rejected and nothing was stored. |

A `507` on a full store or queue reads `storage capacity reached`; a script install past
`scriptLimit` says `script limit reached (N installed)` and carries `field: "name"` instead.

`device/reboot`, `device/sleep`, `device/factory-reset` and `settings/reset` write their
`200 {"ok":true}` **before** the restart or deep-sleep is triggered, so the response is delivered
normally.

### 404 messages

| Route or cause | `message` |
| --- | --- |
| `PUT /api/v1/apps/active` with an unknown name | `app not found` |
| `POST /api/v1/sounds/play` with an unknown `name` | `sound not found` |
| `GET /api/v1/apps/script/{name}` for a script that does not exist | `no such script` |
| `DELETE /api/v1/sounds/{name}` for a melody that does not exist | `melody not found` |
| Anything else not found | `not found` |
| `/api/v1/indicators/{id}` where `{id}` is not `1`, `2` or `3` | `indicator id must be 1..3` |
| A missing file under `/ICONS/`, `/MELODIES/`, `/PALETTES/`, or `DELETE /api/v1/files` | `file not found` |
| Path not routed at all | `unknown route` |

```bash
curl -i -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H 'Content-Type: application/json' \
  -d '{"name":"NoSuchApp"}'
# HTTP/1.1 404 Not Found
# {"error":{"code":"notFound","message":"app not found"}}
```

## Method not allowed

The `message` is always `allowed method(s): <list>`. Every route below is matched by the router, so
a wrong method on any of them answers `405`. Only a genuinely unrecognized path falls through to
`404 notFound` with `unknown route`.

| Path | Allowed |
| --- | --- |
| `/api/v1/notifications` | `POST` |
| `/api/v1/notifications/active` | `DELETE` |
| `/api/v1/notifications/{name}` | `DELETE` |
| `/api/v1/apps` | `GET` |
| `/api/v1/apps/active` | `PUT` |
| `/api/v1/apps/next` | `POST` |
| `/api/v1/apps/previous` | `POST` |
| `/api/v1/apps/order` | `PUT` |
| `/api/v1/apps/pushed/{name}` | `PUT` |
| `/api/v1/apps/script/{name}` | `GET, PUT` |
| `/api/v1/apps/{name}` | `DELETE` |
| `/api/v1/scripts/shared` | `GET` |
| `/api/v1/settings` | `GET, PATCH` |
| `/api/v1/settings/reset` | `POST` |
| `/api/v1/display` | `GET, PATCH` |
| `/api/v1/display/moodlight` | `PUT, DELETE` |
| `/api/v1/display/screen` | `GET` |
| `/api/v1/indicators/{id}` | `PUT, DELETE` |
| `/api/v1/sounds` | `GET` |
| `/api/v1/sounds/{name}` | `PUT, DELETE` |
| `/api/v1/sounds/play` | `POST` |
| `/api/v1/sounds/stop` | `POST` |
| `/api/v1/radio` | `GET` |
| `/api/v1/radio/play` | `POST` |
| `/api/v1/radio/stop` | `POST` |
| `/api/v1/radio/stations` | `PUT` |
| `/api/v1/device` | `GET` |
| `/api/v1/device/reboot` | `POST` |
| `/api/v1/device/sleep` | `POST` |
| `/api/v1/device/factory-reset` | `POST` |
| `/api/v1/system` | `GET, PUT` |
| `/api/v1/system/wifi-scan` | `GET` |
| `/api/v1/capabilities` | `GET` |
| `/api/v1/version` · `/version` | `GET` |
| `/api/v1/logs` | `GET` |
| `/api/v1/files` | `GET, POST, DELETE` |

```bash
curl -i -X GET http://<awtrix-ip>/api/v1/notifications
# HTTP/1.1 405 Method Not Allowed
# {"error":{"code":"methodNotAllowed","message":"allowed method(s): POST"}}
```

### App names are checked before the method

On `/api/v1/apps/{name}` the name is validated first, so a path that names nothing valid answers
**`400 invalidName`** rather than `405`. That is what a bare sub-collection path hits: the tail of
`/api/v1/apps/script/` is `script/`, which is not an app name.

```bash
curl -i -X GET http://<awtrix-ip>/api/v1/apps/script/
# HTTP/1.1 400 Bad Request
# {"error":{"code":"invalidName","message":"name must match [A-Za-z0-9_-]{1,32}","field":"name"}}
```

`active`, `next`, `previous` and `order` are matched *before* the by-name route, so they keep their
own allowlists: `DELETE /api/v1/apps/next` is `405 allowed method(s): POST`, not the deletion of an
app called `next`.

## Authentication (401)

Auth is **off by default**. It turns on only when `authEnabled` is set (`PUT /api/v1/system`),
which in turn requires a non-empty `authUser` **and** `authPass` - a username on its own changes
nothing. Once on, it applies in **every** mode, including AP/provisioning - there is no blanket
bypass. During provisioning AWTRIX also answers far fewer routes - reads, Wi-Fi setup and a reboot
(see [Provisioning lockdown](#provisioning-lockdown-403)), so a bystander in range still cannot
drive the write API even before you set a password.

When it is on, AWTRIX speaks standard HTTP Basic:

```http
HTTP/1.1 401 Unauthorized
WWW-Authenticate: Basic realm="AWTRIX NG"
Content-Type: application/json

{"error":{"code":"unauthorized","message":"authentication required"}}
```

- The `WWW-Authenticate` header is what makes a browser show its login prompt.
- The body is **JSON in the standard error shape**, not HTML - so a client
  can parse every status the same way.
- Auth guards *everything* AWTRIX serves, including `GET /` (the web UI) and the static
  `/ICONS/`, `/MELODIES/` and `/PALETTES/` assets.
- The two upload routes (`POST /update`, `POST /api/v1/files`) check credentials inside their upload
  handler and answer the same 401.

```bash
curl -u admin:secret http://<awtrix-ip>/api/v1/device
```

## Provisioning lockdown (403)

While AWTRIX is in **AP / provisioning mode** most of the HTTP API is closed off. Only
reads, the Wi-Fi write (`PUT /api/v1/system`) and `POST /api/v1/device/reboot` are reachable; the
rest of the write API, the multipart **file upload** (`POST /api/v1/files`) and the **firmware
upload** (`POST /update`) answer:

```http
HTTP/1.1 403 Forbidden
Content-Type: application/json

{"error":{"code":"forbidden","message":"not available during provisioning"}}
```

The upload routes carry their own messages - `file upload is disabled during provisioning` and
`firmware update is disabled during provisioning`. The soft-AP itself is still open (no Wi-Fi
password), but very little is reachable and, if you have configured an
`authUser`, still authenticated.

## Content-Type: the empty-body trap

On `PUT` and `PATCH`, a body is rejected only when the request carries a `Content-Type` header
whose value is not `application/json` - the header itself is not required. The one exemption:
`PUT /api/v1/apps/script/{name}` carries Berry source rather than JSON, so it accepts any content
type - see [installing a script](http.md#put-apiv1appsscriptname).

The trap is `curl -d`, which sends `Content-Type: application/x-www-form-urlencoded` by default
rather than omitting the header - and *that* is what gets rejected:

* A body whose `Content-Type` is present and is not `application/json` returns
  **`415 unsupportedMediaType`** (`Content-Type must be application/json`) - before the body is even
  parsed. This is where a `curl -d` without an explicit `-H` lands.
* A request that omits the `Content-Type` header altogether is not rejected by this check; the body
  is read and parsed as JSON normally.
* An **empty or `{}`** body on a route that requires one returns **`422 validationFailed`** with a
  message naming what to send instead.

On the other routes an empty body and a literal `{}` part ways: a zero-byte body is never valid
JSON, while `{}` is a well-formed object with nothing in it.

| Route | Empty body | `{}` body |
| --- | --- | --- |
| `PUT /api/v1/apps/pushed/{name}` | `422` - `a JSON body is required; use DELETE /api/v1/apps/{name} to remove the app` | same `422` |
| `PUT /api/v1/display/moodlight` | `422` - `a JSON body is required; use DELETE to turn the mood light off` | same `422` |
| `PUT /api/v1/indicators/{id}` | `422` - `a JSON body is required; use DELETE to turn the indicator off` | same `422` |
| `POST /api/v1/radio/play` | `422` - `a JSON body is required; name a station, an index or a url` | same `422` |
| `PUT /api/v1/radio/stations` | `422` - `a JSON body is required; send {"stations":[...]}` | `422 validationFailed`, `field: "stations"`, `must be an array` |
| `POST /api/v1/notifications` | `400 invalidJson` | `200` - an empty notification is queued |
| `PATCH /api/v1/settings` | `400 invalidJson` | `200` - nothing changed |
| `PATCH /api/v1/display` | `400 invalidJson` | `200` - nothing changed |
| `POST /api/v1/device/sleep` | `400 invalidJson` | `422 validationFailed`, `field: "durationMs"` |
| `PUT /api/v1/apps/order` | `400 invalidJson` | `400 invalidJson` - the `disabled` array must be present |

Deleting an app, turning the mood light off, or clearing an indicator is done **only** through the
explicit `DELETE` route (or, over MQTT, the empty-payload clear idiom). An empty body is never
destructive.

Always send the header explicitly:

```bash
# WRONG - curl sends application/x-www-form-urlencoded; on a PUT/PATCH this
# returns 415 unsupportedMediaType:
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hello -d '{"text":"hi"}'

# RIGHT
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hello \
  -H 'Content-Type: application/json' \
  -d '{"text":"hi"}'
```

## Validation messages

A rejected *value* names the offending key in `field`. These are the messages AWTRIX produces.

### PATCH /api/v1/settings

Every key in the payload is checked before anything is applied; the **first** offender is reported.

| `message` | Cause |
| --- | --- |
| `unknown field` | The key is not a settings key. Unknown keys **are rejected** - typos fail loudly. |
| `must be a boolean` | A boolean setting got a non-boolean. |
| `must be an integer` | An integer setting got a non-integer. Booleans are explicitly excluded. |
| `out of range` | An integer setting is outside its documented range. |
| `must be a non-negative integer (milliseconds)` | A duration setting got a negative or non-integer value. |
| `must be a positive number` | A float setting is not a number, or is `<= 0`. |
| `must be one of: <names>` | An enum setting got an unlisted value. The allowed names are listed in the message. |
| `must be a color ("#RGB", "#RRGGBB", [r,g,b], ["HSV",h,s,v] or a packed integer)` | A non-nullable color setting got an unparseable value. The same message and `field: "color"` come back from `PUT /api/v1/indicators/{id}` and `PUT /api/v1/display/moodlight`. |
| `must be a color or null` | A nullable color setting got an unparseable, non-null value. |
| `must be a color` | A color inside `weekdayBar` got an unparseable value, or `null` - those four are not nullable. |
| `must be an array of weekday names` | `weekdayBar.weekendDays` was not an array, or held something other than the seven lowercase English day names. `field` is `weekdayBar.weekendDays`. |

```bash
curl -i -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"brightness":999}'
# HTTP/1.1 422 Unprocessable Entity
# {"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

### Other routes

| Route | `field` | `message` |
| --- | --- | --- |
| `PATCH /api/v1/display` | `power` | `must be a boolean` |
| `PATCH /api/v1/display` | `overlay` | `must be a string or null` |
| `PATCH /api/v1/display` | `overlay` | `unknown overlay` |
| `PATCH /api/v1/display` | `overlaySettings` | `must be an object` |
| `PATCH /api/v1/display` | `overlaySettings.palette` | `unknown palette` |
| `POST /api/v1/device/sleep` | `durationMs` | `must be a positive integer (milliseconds)` |
| `POST /api/v1/notifications` | *(none)* | `send one notification per request; an array of more than one is not accepted` |
| `POST /api/v1/sounds/play` | `name` | `one of "name", "rtttl" or "builtin" is required` |
| `POST /api/v1/sounds/play` | `name` | `exactly one of "name", "rtttl" or "builtin" is allowed` - more than one supplied |
| `POST /api/v1/sounds/play` | `rtttl` | `RTTTL is not supported on this sound backend` - DFPlayer board |
| `POST /api/v1/sounds/play` | `builtin` | `the built-in melody is not supported on this sound backend` - DFPlayer board |
| `PUT /api/v1/apps/pushed/{name}` | *(none)* | `a JSON body is required; use DELETE /api/v1/apps/{name} to remove the app` - empty or `{}` body |
| `PUT /api/v1/display/moodlight` | *(none)* | `a JSON body is required; use DELETE to turn the mood light off` - empty or `{}` body |
| `PUT /api/v1/indicators/{id}` | *(none)* | `a JSON body is required; use DELETE to turn the indicator off` - empty or `{}` body |
| `POST /api/v1/radio/play` | *(none)* | `a JSON body is required; name a station, an index or a url` - empty or `{}` body |
| `POST /api/v1/radio/play` | `station` | `give a station name, an index or a url` - none of the three supplied |
| `POST /api/v1/radio/play` | `station` | `unknown station` |
| `POST /api/v1/radio/play` | `index` | `must be an integer`, or `no station at that position` |
| `POST /api/v1/radio/play` | `url` | `must start with http:// or https://`, or `not a usable http or https URL` |
| `PUT /api/v1/radio/stations` | *(none)* | `a JSON body is required; send {"stations":[...]}` - empty body |
| `PUT /api/v1/radio/stations` | `stations` | `must be an array` |
| `PUT /api/v1/apps/script/{name}` | `source` | `request body must be the script source` - empty body |
| `PUT /api/v1/apps/pushed/{name}`, `POST /api/v1/notifications` | `effect` | the payload names an effect AWTRIX does not know |
| `PUT /api/v1/apps/pushed/{name}`, `POST /api/v1/notifications` | `overlay` | the payload names an overlay AWTRIX does not know |

Two routes skip some validation. `PUT /api/v1/display/moodlight` rejects an empty or `{}` body
with `422` (above); a bad `brightness` or `kelvin` value is silently truncated rather than
rejected, but an unparseable `color` is rejected with `422 validationFailed` and `field: "color"` -
the same treatment `PUT /api/v1/indicators/{id}` gives it. `PUT /api/v1/apps/active` uses the body
as a literal app name, so a malformed body answers `404 app not found` rather than `400`.

### Per-app effect and overlay names

A pushed app or notification whose `effect` or `overlay` is not in the registries is rejected with
`422 validationFailed` and `field: "effect"` / `field: "overlay"`, and **nothing is stored** - an
array payload is all-or-nothing, so one bad element rejects the whole batch. The accepted names come
from [`GET /api/v1/capabilities`](http.md#get-apiv1capabilities). This is the same treatment the
global `overlay` on `PATCH /api/v1/display` has.

Boot replay is more tolerant: a stored app that fails this check is skipped at boot rather than
rejected, so the rest of your rotation still comes up.

### PUT /api/v1/system

Every numeric field is range- and type-checked before anything is stored, so a rejected request
changes nothing and `field` names the offending key:

| Field(s) | Accepted range |
| --- | --- |
| `mqttPort` | 1–65535 |
| `webPort` | 0–65535 (`0` = 80) |
| `wifiConnectTimeout` | 5000–120000 |
| `wifiRoamRssi` | −90–0 |
| `statsInterval` | 1000–600000 |
| `tempDecimals` | 0–2 |
| `lowBatteryThreshold` | 0–100 |
| `minBrightness`, `maxBrightness` | 0–255 |
| `panelWidth` | 1–128 |
| `panels` | 1–128 |
| `tempOffset` | −20–20 |
| `humOffset` | −50–50 |
| `batteryDividerRatio` | 0.1–10 |
| `ldrFactor` | 0–10 |
| `ldrGamma` | 0.1–10 |
| `brightnessSmoothing` | 0–60000 |
| `scriptLimit` | 0–32 |
| `scriptMaxBytes` | 1024–32768 |
| any `pin*` | −1 (disabled), or a GPIO in `0`–`gpioMax` for the running chip; the message is `must be -1 (disabled) or a GPIO in 0..<max>`. The deeper pin rules run separately - see [GPIO validation](#gpio-validation-invalidpinconfig) |

A value of the wrong type answers `must be an integer` (or `must be a number` on the five decimal
fields); a value of the right type outside the range answers `out of range`. `panelStart` and
`panelWiring` answer `must be one of: <names>`, and `panelSerpentine`, `mirror` and `rotate`
answer `must be a boolean`.

Three cross-field rules are checked on the **merged** configuration, so a partial body that sets
only one half of a pair is caught too:

| Condition | `field` | Message |
| --- | --- | --- |
| `panelWidth × panels` outside 32–128 | `panelWidth` | `panelWidth x panels must come to between 32 and 128 pixels` |
| `minBrightness` greater than `maxBrightness` | `minBrightness` | `must not be greater than maxBrightness` |
| `netStatic: true` with a non-empty `ip` but no mask | `subnet` | `a static IP needs a mask; give it as /24 on "ip" or set "subnet"` |

The five static-address fields - `ip`, `gateway`, `subnet`, `dns1`, `dns2` - must be a dotted
quad or the empty string. `ip` may also carry the mask as a CIDR suffix (`192.168.1.50/24`),
which is stored as the separate `ip` and `subnet` values.

Anything else answers `must be an IPv4 address like 192.168.1.50, or "" to leave it unset`; on
`ip` the message reads `must be an IPv4 address like 192.168.1.50 or 192.168.1.50/24, or "" to
leave it unset`. Sending both a `/prefix` on `ip` and a non-empty `subnet` in one request answers
`422` on `subnet`: `the /prefix on "ip" already names the mask; send one or the other`.

Two subsystems are gated by their own switch, and each gate is refused unless the fields it
needs are present. Blanking `wifiSsid` is refused outright - an empty SSID is what drops AWTRIX
into AP mode. Each answers `422 validationFailed` with the key in `field`:

| Condition | Field | Message |
| --- | --- | --- |
| `mqttEnabled: true` with an empty `mqttHost` | `mqttHost` | `set a broker host before enabling MQTT` |
| `authEnabled: true` with an empty `authUser`/`authPass` | `authUser` | `set a username and password before requiring login` |
| `wifiSsid` set to `""` | `wifiSsid` | `an empty string is not a clear; use POST /api/v1/device/factory-reset instead` |
| only some of `pinI2sBclk` / `pinI2sLrclk` / `pinI2sDout` set | the first unset one | `the I2S pins work as a set: give all three, or -1 for all three` |

Unknown keys are still **ignored** (the route is a partial merge) and every other string is stored
as sent. The deeper GPIO rules - duplicate pins, input-only pins, the matrix driver whitelist -
still answer `400 invalidPinConfig` rather than 422; see
[GPIO validation](#gpio-validation-invalidpinconfig).

## GPIO validation (invalidPinConfig)

`PUT /api/v1/system` is the only route that emits this code. The incoming fields are merged onto a
scratch copy of the configuration and the **whole resulting pin map** is validated before anything is
written - so a rejected request changes nothing, and a single valid-looking field can still fail
because of how it combines with the pins already stored.

The status is **400**, and there is **no `field`** - the offending pin's name is in the `message`:

| `message` | Cause |
| --- | --- |
| `pinMatrix: unsupported pin (compiled drivers: …)` | The matrix data pin has no compiled driver - see [the driver list](gpio.md#1-matrix-pin-whitelist). |
| `<pin>: not a valid ESP32 GPIO (0-39, or -1 = disabled)` | Out of range for the chip. |
| `<pin>: GPIO 6-11 are reserved for the SPI flash` | Would break the flash. |
| `<pin>: GPIO 34-39 are input-only` | An output-capable pin was required. |
| `pinBattery: must be an ADC1 pin (GPIO 32-39, usable while WiFi is on)` | ADC2 is unusable while WiFi is on. |
| `pinLdr: must be an ADC1 pin (GPIO 32-39, usable while WiFi is on)` | Same. |
| `duplicate pin <n> (<pinA>, <pinB>)` | Two enabled functions claim the same GPIO. When one of them is `pinMatrix` the message goes on to name the fix - see [No duplicates](gpio.md#6-no-duplicates). |

Each message is built from the running chip's rules, so the numbers above are the ESP32's and an
ESP32-S3 answers with its own. The per-chip values are in
[Rules come from the chip](gpio.md#rules-come-from-the-chip) and in
[`GET /api/v1/capabilities`](http.md#gpio-what-the-chip-can-do).

```bash
curl -i -X PUT http://<awtrix-ip>/api/v1/system \
  -H 'Content-Type: application/json' \
  -d '{"pinBuzzer":34}'
# HTTP/1.1 400 Bad Request
# {"error":{"code":"invalidPinConfig","message":"pinBuzzer: GPIO 34-39 are input-only"}}
```

## Errors over MQTT

MQTT has no status codes. A command result is published as a JSON object whose `ok` field carries the
verdict; failures embed the **same error object**:

```json
{"ok":false,"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

Success is `{"ok":true}`. Eight codes can appear over MQTT:

| Code | Emitted when | `message` |
| --- | --- | --- |
| `invalidJson` | the payload is not valid JSON | `payload is not valid JSON` |
| `validationFailed` | a value was rejected | the specific reason, or `invalid value` |
| `notFound` | unknown app, sound, or notification name | `not found` |
| `insufficientStorage` | the notification queue or the pushed-app slots are full | the specific reason, or `storage capacity reached` |
| `unavailable` | the command needs hardware this board does not have | e.g. `this build has no audio output` |
| `serviceBusy` | a radio stream is refused for lack of memory; try again | the specific reason, or `device is busy, try again` |
| `internalError` | the command failed or was not understood | `command failed` |
| `invalidName` | an app name in the topic is not `[A-Za-z0-9_-]{1,32}` - only `cmd/apps/pushed/{name}` can raise it | `name must match [A-Za-z0-9_-]{1,32}` |

The other ten codes are HTTP-only. `methodNotAllowed`, `unauthorized`, `forbidden`,
`unsupportedMediaType`, `payloadTooLarge` and `invalidMethodOverride` describe request framing,
which MQTT has no equivalent for; `invalidPath`, `badRequest` and `wrongChip` belong to the upload
routes, and `invalidPinConfig` to `PUT /api/v1/system` - none of which MQTT can reach.

A published command over the [MQTT payload ceiling](limits.md#requests) is dropped before it is
parsed, so it produces no `/result` reply at all rather than a `payloadTooLarge`.

## Responses that look like errors but are not

| Response | Route | Meaning |
| --- | --- | --- |
| `202 {"scanning":true}` | `GET /api/v1/system/wifi-scan` | Scan running; poll again for the result array. |
| `304`, empty body | `GET /` | Your cached web UI is current - the `If-None-Match` header matched. |
| `302` to `http://<ap-ip>/` | any route, AP mode only | Captive-portal redirect for a foreign `Host` header. Sent *before* auth. |
| connection dropped | `POST /update` | Only a **successful** firmware upload reboots inside the request; the connection drops. `reboot`, `sleep`, `factory-reset` and `settings/reset` answer `200` first, then restart. |
