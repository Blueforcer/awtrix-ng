# Errors

What AWTRIX answers when a request fails: the error body, every error code, and the messages for
the most common mistakes. [Error codes](#error-codes) lists every code the API returns.

## The error body

Every failed HTTP request gets a JSON answer in this shape:

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
| `error.code` | string | yes | A fixed name for the error. Check this in your code, never `message`. |
| `error.message` | string | yes | A reason for people to read. The wording may change between firmware versions. |
| `error.field` | string | **no** | The key that caused the error. Only there when AWTRIX knows which key it was - otherwise the key is left out completely (never `null`, never `""`). |

The one exception is `POST /api/v1/restore` - see [Restore result](#restore-result).

## Restore result

Once a backup file arrives, `POST /api/v1/restore` answers with this object instead, whether the
restore worked or not:

```json
{
  "ok": true,
  "applied": {
    "wifi": 1, "system": 1, "settings": 1, "appLoop": 3, "radioStations": 0,
    "icons": 2, "iconOrigins": 0, "melodies": 0, "palettes": 1, "mp3": 0, "scripts": 0,
    "skipped": 0
  },
  "warnings": []
}
```

| Key | Type | Meaning |
| --- | --- | --- |
| `ok` | boolean | Whether the backup was accepted and restored. |
| `error` | string | Only when `ok` is `false` and the whole backup was rejected (for example, it has no `manifest.json`). A plain message, not an error code. |
| `applied` | object | How many items of each kind were restored. Always present. |
| `warnings` | array of strings | Problems with single files (for example, a file that could not be written). The rest of the restore still goes on. |

The status is `200` when `ok` is `true` and `400` when it is `false`.

These answers use the normal error body instead:

* The upload ends without a file in it: `400` with
  `{"error":{"code":"badRequest","message":"no file received"}}`.
* The backup was restored, but AWTRIX could not save it yet: `507 insufficientStorage`,
  `restored, not saved yet`.<!-- only tc002 --> AWTRIX retries automatically.<!-- /only --><!-- only esp32 esp32-s3 --> Free storage and repeat the restore before
  rebooting.<!-- /only -->
* Login is on and the credentials are wrong: `401 unauthorized`.

## Error codes

| Code | Status | Returned by | Meaning |
| --- | --- | --- | --- |
| `invalidJson` | 400 | any route with a body | The body is not valid JSON. On some routes an empty body also lands here - see [Content-Type](#content-type-the-empty-body-trap). |
<!-- only tc002 -->
| `invalidPlayer` | 400 | `POST /api/v1/gamepad/remote` | `player` is not the whole number `1` or `2`. The message is `must be 1..2`, with `field: "player"`. Phones that play stay as they are. |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `invalidPinConfig` | 400 | `PUT /api/v1/system` only | The GPIO pins do not work together. Nothing was saved. See [GPIO validation](#gpio-validation-invalidpinconfig). |
<!-- /only -->
| `invalidPath` | 400 | `GET` / `POST` / `DELETE /api/v1/files` | The file name or path is outside `/ICONS`, `/MELODIES`, `/PALETTES`, `/MP3`, or contains `..`. Nothing was written or removed. |
| `invalidName` | 400 | every route with an app name in its path, the MP3 and script-sound routes, `POST /api/v1/files` for `/MP3`, `POST /api/v1/audio/mp3/rename`, `POST /api/v1/icons/rename`, `DELETE /api/v1/icons/origins`<!-- only tc002 -->, `POST /api/v1/gamepad/remote`<!-- /only --> | An app or script name does not match `[A-Za-z0-9_-]{1,32}` or is `active`, `next`, `previous` or `order` (`field: "name"`), <!-- only tc002 -->a phone name is not text or longer than 32 characters (`field: "name"`), <!-- /only -->or an MP3 or icon file name is not valid. Renaming an icon or an MP3 names `from` or `to` in `field`; other file-name errors may omit it. Nothing is read or written. |
| `invalidOrigin` | 400 | `PUT /api/v1/icons/origins` | A field of the icon link is missing or not valid. See [Icon origins](http.md#put-apiv1iconsorigins). |
| `invalidMethodOverride` | 400 | any route, when `X-HTTP-Method-Override` is sent | The header is on something other than a `POST`, names anything but `PUT`/`PATCH`/`DELETE`, or targets the script upload. Nothing was done. See [Method override](http.md#method-override). |
| `badRequest` | 400 | uploads: `POST /update`, `/api/v1/files`, `/api/v1/audio/mp3`, `/api/v1/apps/script/{name}/sounds`, `/api/v1/restore` | No file arrived, more than one arrived where one is expected, or the upload was interrupted or incomplete. `message` says which, for example `no file received`. |
<!-- only esp32 -->
| `wrongChip` | 400 | `POST /update` (firmware upload) | The file is not an update for this device: made for a different chip, a `usb-*.bin` meant for the first flash over USB, or not a firmware file at all. `message` says which. The installed firmware is not touched. |
<!-- /only -->
<!-- only esp32-s3 -->
| `wrongChip` | 400 | `POST /update` (firmware upload) | The file is not an update for this device: made for a different chip, for the other kind of PSRAM (quad or octal), a `usb-*.bin` meant for the first flash over USB, or not a firmware file at all. `message` says which. The installed firmware is not touched. |
<!-- /only -->
<!-- only tc002 -->
| `invalidPackage` | 400 | `POST /update` | The package is damaged, incomplete or in an unknown format. Download it again. |
| `wrongTarget` | 400 | `POST /update` | The package is made for another device. |
<!-- /only -->
| `unauthorized` | 401 | every route, when login is on | Missing or wrong username or password. See [Authentication](#authentication-401). |
| `forbidden` | 403 | routes unavailable in setup mode | Not allowed while AWTRIX is in setup mode. See [Setup mode lockdown](#provisioning-lockdown-403). |
| `forbiddenOrigin` | 403 | `GET /api/v1/system?secrets=1`, `PUT /api/v1/system`, `POST /api/v1/restore`, `POST /update`, `POST /api/v1/device/factory-reset`<!-- only tc002 -->, `POST /api/v1/voice` and the script sign-in changes<!-- /only --> | The request came from a web page of another site: its `Origin` header names another host, or `Sec-Fetch-Site` is neither `same-origin` nor `none`. Nothing was done. See [Requests from other web pages](http.md#cross-origin). |
| `notFound` | 404 | many routes | Unknown route, or an app, sound or file with that name does not exist. |
| `methodNotAllowed` | 405 | any known route | The path exists, but not for this method. `message` lists the allowed methods. |
| `payloadTooLarge` | 413 | any route with a body | The body is larger than allowed for that route ([Limits](limits.md#requests)). Nothing was applied. |
<!-- only tc002 -->
| `insufficientMemory` | 413 | `POST /update` | Not enough free memory to take the package right now. |
<!-- /only -->
| `unsupportedMediaType` | 415 | `PUT` / `PATCH` (except script uploads), file and MP3 uploads | The `Content-Type` is not `application/json` (see [Content-Type](#content-type-the-empty-body-trap)), or an uploaded file does not suit its folder: `/ICONS` takes GIF and JPEG, `/MELODIES` RTTTL text, `/PALETTES` `RRGGBB` lines, `/MP3` and script sounds MP3. Nothing is stored. |
| `scriptChanged` | 409 | `PUT /api/v1/apps/script-update/{name}` | The stored source is no longer the `expected_source` you sent, or a script with that name already exists. See [Script updates](http.md#script-updates). |
<!-- only tc002 -->
| `notNewer` | 409 | `POST /update` | The package is not newer than the installed version. Going back to an older version works over USB only. |
| `gamepadsFull` | 409 | `POST /api/v1/gamepad/pair` | Both gamepad slots hold a gamepad. The message is `no free slot`. Forget one with `DELETE /api/v1/gamepad/{id}` first. |
| `updateBusy` | 409 | `POST /update` | Another update is being uploaded or installed, or the last one is still being confirmed. Try again in a few minutes. |
<!-- /only -->
| `nameTaken` | 409 | `PUT /api/v1/audio/melodies/{name}`, `POST /api/v1/audio/mp3`, `POST /api/v1/files` into `/MP3` or `/MELODIES`, `POST /api/v1/audio/mp3/rename`, `POST /api/v1/icons/rename` | An MP3 and a melody would share a name, or renaming an MP3 or an icon would use an existing name (`field: "to"`). The message is `name taken`. Nothing is stored. The sounds in a script's own folder do not count. |
| `validationFailed` | 422 | write routes | The JSON is valid, but a value is not. Usually has `field`. **Nothing was applied.** |
| `insufficientStorage` | 507 | pushed apps, notification queue, script install and script settings, <!-- only tc002 -->broker CA uploads, <!-- /only -->file and MP3 uploads, app order, <!-- only esp32-s3 tc002 -->radio stations, <!-- /only -->restore, large request bodies | A store or queue is full ([Limits](limits.md#apps-and-notifications)), or a change could not be saved. App order, <!-- only esp32-s3 tc002 -->radio stations <!-- /only -->and restore may already be applied; see the status notes below. Also returned when AWTRIX has too little free memory for a large body - a small body (a reboot, a short JSON) is never refused this way. Has `field: "name"` on a script install or when a script's settings do not fit, and `field: "source"` when there was not enough memory to receive the script at all.<!-- only tc002 --> An update package that does not fit answers `409` with this code, and the message says how many megabytes are missing.<!-- /only --> |
| `internalError` | 500 | firmware updates, other commands | The request reached AWTRIX, but the action failed. A failed firmware update reads `update failed`. |
| `storageError` | 500 | `/api/v1/icons/origins` | The icon links could not be read or saved. |
<!-- only tc002 -->
| `notSupported` | 501 | `POST /update` | This clock takes no update packages over the network. Update it over USB. |
| `scanUnavailable` | 503 | `GET /api/v1/system/wifi-scan` | Wi-Fi scanning is unavailable while the setup hotspot is open. Enter the network name manually. |
<!-- /only -->
| `unavailable` | 503 | `GET /api/v1/apps/script/{name}`, `/api/v1/apps/{name}/config`, `PUT /api/v1/apps/script-update/{name}`, `GET /api/v1/scripts/shared`, `POST /api/v1/audio/play`<!-- only tc002 -->, `POST /api/v1/audio/clip`, `POST /api/v1/gamepad/pair`, `POST /api/v1/gamepad/remote`<!-- /only --> | This firmware has no scripting, scripting is switched off (`scriptingEnabled`; only for `PATCH` on `/config` and for `script-update`)<!-- only esp32 esp32-s3 --> or the device cannot play the sound you asked for (for a list: none of its entries)<!-- /only --><!-- only tc002 -->, the device cannot play the sound you asked for (for a list: none of its entries), Bluetooth is not running for `gamepad/pair`, or the phone port cannot be opened for `gamepad/remote` (`not available`)<!-- /only -->. `POST /api/v1/audio/stop` never answers 503. A `PUT` on the script path answers `500 internalError` instead. |
| `serviceBusy` | 503 | script install<!-- only esp32-s3 -->, radio play<!-- /only --><!-- only tc002 -->, large bodies<!-- /only --> | AWTRIX is busy for a moment; try again shortly. This is not a full store like `insufficientStorage`. The answer has `Retry-After: 2`. Has `field: "name"` on a script install<!-- only esp32-s3 --> and `field: "station"` when a radio stream is refused for lack of memory<!-- /only -->.<!-- only tc002 --> A body over 64 KiB gets it while another large body is still arriving.<!-- /only --> |

`field` is present when one key is at fault. Clients must allow it to be absent with any error
code. For example, renaming an icon can report `invalidName` with `from` or `to`, or `nameTaken`
with `to`; a melody body that is not valid JSON reports `invalidJson` with `rtttl`.

How large a body may be and how many apps and notifications fit: [Limits](limits.md).

## Status codes

| Status | Applied? | Notes |
| --- | --- | --- |
| 200 | yes | `{"ok":true}` on most write routes. `PATCH /api/v1/settings` returns the **complete settings** after the change. |
| 400 | no | Nothing was read or applied. |
| 401 | no | Login required. |
| 403 | no | Not allowed in setup mode, or from another site. |
| 404 | no | - |
| 405 | no | Wrong method for this route. |
<!-- only esp32 esp32-s3 -->
| 409 | no | Script update refused because the script changed (`scriptChanged`), or an MP3 and a melody would share a name (`nameTaken`). |
| 413 | no | Body too large. |
<!-- /only -->
<!-- only tc002 -->
| 409 | no | Script update refused because the script changed (`scriptChanged`); update refused: not newer, another update running, or not enough space; both gamepad slots taken (`gamepadsFull`); or an MP3 and a melody would share a name (`nameTaken`). |
| 413 | no | Body too large, or not enough memory for an update. |
<!-- /only -->
| 415 | no | Wrong `Content-Type` on a `PUT`/`PATCH`, or an uploaded file that does not match its folder. |
| 422 | **no** | The first invalid value stops the whole request. A `PATCH` is applied completely or not at all. |
| 500 | maybe | The action started and reported a failure. |
<!-- only tc002 -->
| 501 | no | This clock takes no updates over the network. |
<!-- /only -->
| 503 | no | Not available on this device (`unavailable`), or busy for a moment (`serviceBusy`). |
| 507 | maybe | A store or queue was full, memory was too low, or an applied change could not be saved. Read the message as described below. |

File uploads return `507 insufficientStorage` with `write failed` if the
file cannot be written or published. The existing destination is retained; a directory
created for that failed upload is removed when empty. Each file in a multipart request
is published separately: a later failure does not undo earlier successful files.

App order<!-- only esp32-s3 tc002 --> and radio station<!-- /only --> changes return `507 insufficientStorage` with
`applied, not saved yet` when the new state is active but could not be saved. The previous
saved file remains intact. Free storage and repeat the request before rebooting.<!-- only tc002 --> AWTRIX
also retries saving automatically.<!-- /only -->

A `507` for a full store or queue reads `storage full`. A script install refused for
lack of memory has its own message and `field: "name"` or `field: "source"` - see
[Script installation](#script-installation).

`device/reboot`, `device/sleep`, `device/factory-reset` and `settings/reset` send their
`200 {"ok":true}` **before** AWTRIX restarts or goes to sleep, so you always get the answer.

### 404 messages

| Route or cause | `message` |
| --- | --- |
| `PUT /api/v1/apps/active` with an unknown name | `app not found` |
| `POST /api/v1/audio/play` with a `file` name nothing is stored under | `nothing called "<name>"` |
| `POST /api/v1/audio/play` with a script's sound that does not exist, such as `"Racer/x"` | `no file "<name>"` |
<!-- only esp32-s3 tc002 -->
| `POST /api/v1/audio/play` with a `station` name or position not in the list | `unknown station` / `no station at that position` |
<!-- /only -->
| `GET /api/v1/apps/script/{name}`, `/api/v1/apps/{name}/config` or a script-sound route for a script that does not exist | `no such script` |
| `/api/v1/apps/builtin/{name}/config` for an app that is no built-in app of this device, whose sensor is missing, or whose name a pushed app has | `no such app` |
| `DELETE /api/v1/audio/mp3/{name}`, or a script sound, that does not exist | `no such MP3` |
| `DELETE /api/v1/audio/melodies/{name}` for a melody that does not exist | `melody not found` |
| `PUT /api/v1/icons/origins` for an icon that does not exist | `icon file not found` |
| `/api/v1/indicators/{id}` where `{id}` is not `1`, `2` or `3` | `id must be 1..3` |
<!-- only tc002 -->
| `/api/v1/gamepad/{id}` where `{id}` is not `1` or `2` | `no such gamepad` |
| `DELETE /api/v1/gamepad/remote/{session}` for a phone session that has ended or never existed | `no such session` |
<!-- /only -->
| A missing file under `/ICONS/`, `/MELODIES/`, `/PALETTES/`, or `DELETE /api/v1/files` | `file not found` |
| A path that is not a route at all, such as `/ICONS` without the closing `/` | `unknown route` |
| Anything else not found | `not found` |

```bash
curl -i -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H 'Content-Type: application/json' \
  -d '{"name":"NoSuchApp"}'
# HTTP/1.1 404 Not Found
# {"error":{"code":"notFound","message":"app not found"}}
```

## Method not allowed

A wrong method on a supported route below answers `405`, and the `message` is
`allowed: <list>`. An unknown path answers `404 notFound` with `unknown route`.

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
| `/api/v1/apps/{name}/enabled` | `PUT` |
| `/api/v1/apps/pushed/{name}` | `PUT` |
| `/api/v1/apps/script/{name}` | `GET, PUT` |
| `/api/v1/apps/script-update/{name}` | `PUT` |
| `/api/v1/apps/script/{name}/sounds` | `GET, POST, DELETE` |
| `/api/v1/apps/script/{name}/sounds/{sound}` | `DELETE` |
| `/api/v1/apps/{name}/config` | `GET, PATCH` |
| `/api/v1/apps/{name}/data` | `GET, PATCH` |
| `/api/v1/apps/builtin/{name}/config` | `GET, PATCH` |
| `/api/v1/apps/{name}` | `DELETE` |
| `/api/v1/scripts/shared` | `GET` |
| `/api/v1/settings` | `GET, PATCH` |
| `/api/v1/settings/reset` | `POST` |
| `/api/v1/display` | `GET, PATCH` |
| `/api/v1/display/moodlight` | `PUT, DELETE` |
| `/api/v1/display/screen` | `GET` |
| `/api/v1/indicators/{id}` | `PUT, DELETE` |
| `/api/v1/audio` | `GET` |
| `/api/v1/audio/melodies` | `GET` |
| `/api/v1/audio/melodies/{name}` | `PUT, DELETE` |
| `/api/v1/audio/play` | `POST` |
| `/api/v1/audio/stop` | `POST` |
<!-- only tc002 -->
| `/api/v1/audio/clip` | `POST` |
<!-- /only -->
| `/api/v1/audio/mp3` | `GET, POST` |
| `/api/v1/audio/mp3/{name}` | `DELETE` |
| `/api/v1/audio/mp3/rename` | `POST` |
<!-- only esp32-s3 tc002 -->
| `/api/v1/audio/stations` | `GET, PUT` |
<!-- /only -->
| `/api/v1/device` | `GET` |
| `/api/v1/device/reboot` | `POST` |
| `/api/v1/device/sleep` | `POST` |
| `/api/v1/device/factory-reset` | `POST` |
| `/api/v1/system` | `GET, PUT` |
| `/api/v1/system/wifi-scan` | `GET` |
| `/api/v1/capabilities` | `GET` |
| `/api/v1/version` · `/version` | `GET` |
| `/api/v1/logs` | `GET` |
<!-- only tc002 -->
| `/api/v1/gamepad` | `GET` |
| `/api/v1/gamepad/pair` | `POST` |
| `/api/v1/gamepad/{id}` | `DELETE` |
| `/api/v1/gamepad/remote` | `POST` |
| `/api/v1/gamepad/remote/{session}` | `DELETE` |
| `/api/v1/mqtt/tls` | `GET` |
| `/api/v1/mqtt/tls/ca` | `PUT, DELETE` |
| `/api/v1/oauth` | `GET` |
| `/api/v1/oauth/{name}` | `GET, POST, DELETE` |
| `/api/v1/oauth/{name}/start` · `/api/v1/oauth/{name}/code` | `POST` |
| `/api/v1/voice` | `GET, POST` |
<!-- /only -->
| `/api/v1/files` | `GET, POST, DELETE` |
<!-- only tc002 -->
| `/api/v1/restore` | `POST` |
<!-- /only -->
| `/api/v1/icons/rename` | `POST` |
| `/api/v1/icons/origins` | `GET, PUT, DELETE` |

```bash
curl -i -X GET http://<awtrix-ip>/api/v1/notifications
# HTTP/1.1 405 Method Not Allowed
# {"error":{"code":"methodNotAllowed","message":"allowed: POST"}}
```

<!-- only esp32 esp32-s3 -->
`/api/v1/restore` is a route for `POST` only: any other method answers `404 unknown route`.
<!-- /only -->

### Order of the checks

AWTRIX checks a request in this order and answers the first problem it finds:

1. Login: `401`.
2. How the request is sent: a bad method override (`400 invalidMethodOverride`), a page of
   another site on a protected route (`403 forbiddenOrigin`), a body that is too large (`413`),
   a `Content-Type` other than JSON on a `PUT` or `PATCH` (`415`).
3. The method: `405`.
4. The app name in the path: `400 invalidName`.
5. The body: `400 invalidJson`, then `422 validationFailed`.

So `PATCH /api/v1/apps/pushed/bad.name` answers `405 allowed: PUT`, not `400`. A `PUT` to
`/api/v1/apps/script/` without a name, sent as `text/plain`, answers `415`: without a name the
path is no script upload, so the `Content-Type` rule applies.

<!-- only esp32 esp32-s3 -->Two<!-- /only --><!-- only tc002 -->Three<!-- /only --> paths check their name before the method:

* `/api/v1/apps/{name}`: a path without a valid name answers **`400 invalidName`**, not `405`.
  This is what a bare sub-path like `/api/v1/apps/script/` gets: `script/` is not a valid app
  name.
* `/api/v1/indicators/{id}`: an `{id}` other than `1`, `2` or `3` answers `404`
  `id must be 1..3`.
<!-- only tc002 -->
* `/api/v1/oauth/{name}`: a script without an `@oauth` line answers `404` `no @oauth line`,
  before `405` and `403`.
<!-- /only -->

```bash
curl -i -X GET http://<awtrix-ip>/api/v1/apps/script/
# HTTP/1.1 400 Bad Request
# {"error":{"code":"invalidName","message":"invalid name","field":"name"}}
```

`active`, `next`, `previous` and `order` are their own routes with their own methods.
`DELETE /api/v1/apps/next` answers `405 allowed: POST` - it does not delete an app
called `next`.

## Authentication (401)

Login is **off by default**. It turns on only when you set `authEnabled` (`PUT /api/v1/system`),
and that needs both `authUser` **and** `authPass` - a username alone changes nothing. Once on, it
applies in **every** mode, also in setup mode. In setup mode AWTRIX also answers
far fewer routes (see [Setup mode lockdown](#provisioning-lockdown-403)), so a stranger in range
cannot use the write API even before you set a password.

When login is on and your request has no or wrong credentials, AWTRIX answers:

```http
HTTP/1.1 401 Unauthorized
WWW-Authenticate: Basic realm="AWTRIX NG"
Content-Type: application/json

{"error":{"code":"unauthorized","message":"authentication required"}}
```

- The `WWW-Authenticate` header makes a browser show its login window.
- The body is JSON in the normal error shape, not HTML.
- Login protects *everything* AWTRIX serves: the web UI at `/`, the files under `/ICONS/`,
  `/MELODIES/` and `/PALETTES/`, and the upload routes `POST /update` and `POST /api/v1/files`.

```bash
curl -u admin:secret http://<awtrix-ip>/api/v1/device
```

## Setup mode lockdown (403) {#provisioning-lockdown-403}

While AWTRIX is in **setup mode**, these are the only available routes:

| Method | Paths | Purpose |
|---|---|---|
| `GET` | `/`, `/index.html` | Setup page |
| `GET` | `/api/v1/device`, `/api/v1/capabilities`, `/api/v1/system` | Device and setup information, without secrets |
| `GET` | `/api/v1/system/wifi-scan` | Wi-Fi scan<!-- only tc002 -->. It answers `503 scanUnavailable` while the setup hotspot is open<!-- /only --> |
| `PUT` | `/api/v1/system` | Only the string fields `wifiSsid`, `wifiPass` and `hostname` |
| `POST` | `/api/v1/device/reboot` | Restart |
| `POST` | `/api/v1/restore` | [Backup restore](http.md#post-apiv1restore), Wi-Fi included |

Logs, scripts, file lists, secrets export and all other API routes are refused.
A system update with other fields, duplicate fields or malformed JSON is refused as well.
The response is:

```http
HTTP/1.1 403 Forbidden
Content-Type: application/json

{"error":{"code":"forbidden","message":"Wi-Fi setup only"}}
```

The same refusal applies to file and firmware uploads. Any request with a `secrets` query parameter
is refused, even `?secrets=0`. Requests from another web origin are also refused. The setup hotspot itself has no password;
if you have set a web login, it is still required for the allowed routes.

## Content-Type: the empty-body trap

On `PUT` and `PATCH`, AWTRIX rejects a body only when the request has a `Content-Type` header that
is not `application/json`. The header itself is not required. `POST` is not checked.

Exceptions: `PUT /api/v1/apps/script/{name}` takes Berry source and accepts any content type.
`PUT /api/v1/apps/script-update/{name}` also accepts any content type, but its body must still be
JSON. See [installing a script](http.md#put-apiv1appsscriptname).

The trap is `curl -d`: it sends `Content-Type: application/x-www-form-urlencoded` unless you set
another one - and *that* is what fails:

* A `Content-Type` that is not `application/json` gets **`415 unsupportedMediaType`**
  (`expected application/json`) before the body is read. This is what a `curl -d`
  without `-H` gets.
* A request with no `Content-Type` header at all passes; the body is read as JSON.
* On a `POST`, the header is not checked. A JSON body sent with the form type of `curl -d` is
  read as JSON like any other, so `POST /api/v1/notifications` with it shows the notification.
* An **empty or `{}`** body on a route that needs content gets **`422 validationFailed`** with
  the message `body required`.

On the other routes, an empty body and `{}` behave differently: an empty body is never valid JSON,
while `{}` is a valid object with nothing in it.

| Route | Empty body | `{}` body |
| --- | --- | --- |
| `PUT /api/v1/apps/pushed/{name}` | `422` - `body required` | same `422` |
| `PUT /api/v1/display/moodlight` | `422` - `body required` | same `422` |
| `PUT /api/v1/indicators/{id}` | `422` - `body required` | same `422` |
<!-- only esp32-s3 tc002 -->
| `PUT /api/v1/audio/stations` | `422` - `body required` | `422 validationFailed`, `field: "stations"`, `must be an array` |
<!-- /only -->
| `PATCH /api/v1/apps/{name}/config` | `422` - `body required` | `200` - no value changes, but the script restarts |
| `PATCH /api/v1/apps/builtin/{name}/config` | `422` - `body required` | `200` - nothing changed |
| `POST /api/v1/audio/play` | `422` - `body required` | `422` - `needs a sound key` |
| `POST /api/v1/notifications` | `400 invalidJson` | `200` - an empty notification is queued |
| `PATCH /api/v1/settings` | `400 invalidJson` | `200` - nothing changed |
| `PATCH /api/v1/display` | `400 invalidJson` | `200` - nothing changed |
| `POST /api/v1/device/sleep` | `400 invalidJson` | `422 validationFailed`, `field: "durationMs"` |
| `PUT /api/v1/apps/order` | `400 invalidJson` | `400 invalidJson` - the `disabled` array must be present |
| `PUT /api/v1/apps/{name}/enabled` | `422` - `must be true or false` | same `422` |

An empty body never deletes or turns anything off. To delete an app, turn the mood light off or
clear an indicator, use its `DELETE` route (or, over MQTT, send an empty payload to clear).

Always send the header:

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

A rejected *value* names its key in `field`. These are the messages you can get.

### PATCH /api/v1/settings

All keys are checked before anything is changed. The **first** invalid key is reported.

| `message` | Cause |
| --- | --- |
| `unknown field` | The key is not a setting. Unknown keys **are rejected**, so a typo fails loudly. |
| `must be a boolean` | A true/false setting got something else. |
| `must be an integer` | A whole-number setting got something else. `true` and `false` do not count as numbers. |
| `out of range` | A number is outside the documented range. |
| `must be an integer >= 0` | A duration got a negative number or a number with a fraction. |
| `must be a positive number` | A decimal setting got something that is not a number, or a number `<= 0`. |
| `must be one of: <names>` | A setting with a fixed list of words got another word. The message lists the allowed ones. |
| `must be a color` | A color setting got something that is not a color. `PUT /api/v1/indicators/{id}` and `PUT /api/v1/display/moodlight` answer the same message with `field: "color"`. |
| `must be a color or null` | A color setting that allows `null` got something that is neither. |
| `must be a color` | A color inside `weekdayBar` got something that is not a color, or `null` - these four do not allow `null`. |
| `must be weekday names` | `weekdayBar.weekendDays` is not an array, or holds something other than the seven lowercase English day names. `field` is `weekdayBar.weekendDays`. |

```bash
curl -i -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"brightness":999}'
# HTTP/1.1 422 Unprocessable Entity
# {"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

### Script installation

`PUT /api/v1/apps/script/{name}` returns `507 insufficientStorage` for these limits.
A `507` has one of <!-- only esp32 esp32-s3 -->five<!-- /only --><!-- only tc002 -->four<!-- /only --> messages. Each needs a different fix:

| Problem | Message | What helps |
|---|---|---|
| Too large to receive | `source over <n> bytes`, `field: "source"` | delete a script, or restart |
| Too large to process | `not enough memory` | delete a script, or restart |
| All scripts together too large | `Berry heap over budget (<n>/<limit> bytes)`, `field: "name"` | delete a script |
| Not enough free memory | `not enough memory to compile (<free>/<needed> bytes)`, `field: "name"` | delete a script, or restart |
<!-- only esp32 esp32-s3 -->
| Memory too fragmented | `heap too fragmented to compile (<largest block> < <source> bytes)`, `field: "name"` | restart |
<!-- /only -->

"All scripts together too large" applies only to new names. Replacing an installed script is never
refused for that reason. The other <!-- only esp32 esp32-s3 -->four<!-- /only --><!-- only tc002 -->three<!-- /only --> apply to replacements too. A shorter script may still
install when a longer one was refused. All limits: [Limits](limits.md#scripting).


### Audio playback

The body is checked before stored files and audio hardware. Inside a list, `field` starts with
the entry position, for example `[1].rtttl`. These messages have status `422 validationFailed`:

| Route | `field` | `message` |
| --- | --- | --- |
| `POST /api/v1/audio/play` | *(none)* | `must be a string, object or list` |
| `POST /api/v1/audio/play` | *(none)* | `needs a sound key`: none of `file`, `rtttl`, `song`, `speech`, `track` or `station` was sent |
| `POST /api/v1/audio/play` | the first of these keys | `one sound key only`: more than one was sent |
| `POST /api/v1/audio/play` | the key | `unknown field`: a key that is not a sound key, `loop` or `nextBar` |
| `POST /api/v1/audio/play` | *(none)* | `must have 1 to 4 entries`: a list that is empty or longer than 4 |
| `POST /api/v1/audio/play` | `file` | `invalid name`: not a name, not `Script/name` and not an `http(s)://` address. `invalid URL`: an address that cannot be used |
| `POST /api/v1/audio/play` | `rtttl` | `must be a string`, or why the melody could not be read and at which character |
<!-- only tc002 -->
| `POST /api/v1/audio/play` | `song` | `must be song text`, or why the song cannot be read, with line and column |
| `POST /api/v1/audio/play` | `speech` | `must be 1..512 bytes`, or with a voice installed `no words to speak` |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `POST /api/v1/audio/play` | `track` | `must be 1..2999` |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `POST /api/v1/audio/play` | `station` | `not here`: in a list, or in a notification's `sound`. `expected name, position or address`. `invalid URL`: a stream address that cannot be used |
<!-- /only -->
| `POST /api/v1/audio/play` | `loop` | `must be true or false`<!-- only esp32-s3 tc002 -->, or `not with station`<!-- /only --> |
<!-- only tc002 -->
| `POST /api/v1/audio/play` | `nextBar` | `only with a looping song`: scripts only |
| `POST /api/v1/audio/clip` | *(none)* | `body required`, `not WAV or MP3` or `unsupported WAV` |
<!-- /only -->
| `POST /api/v1/audio/stop` | `group` | `must be alert, app or radio` |
| `POST /api/v1/audio/stop` | the key | `unknown field`: a key other than `group` |
| `POST /api/v1/audio/play` | *(none)* | `body required` - empty body |

Playback can also fail after validation:

| Status | Code | `message` | Meaning |
|---|---|---|---|
| 404 | `notFound` | `nothing called "<name>"` | No MP3 or melody has that name. |
| 404 | `notFound` | `no file "<name>"` | The named script has no such sound. |
<!-- only esp32-s3 tc002 -->
| 404 | `notFound` | `unknown station` / `no station at that position` | The station name or position is absent. |
<!-- /only -->
| 503 | `unavailable` | `no sound output`, `no melody output`, `no MP3 output`, `no DFPlayer`, `no synthesizer`, `no text-to-speech`, `no URL playback`, `no audio output` | The device cannot play that kind of sound. |
| 503 | `unavailable` | `nothing playable` | No entry in the list can be played. |
<!-- only tc002 -->
| 503 | `unavailable` | `speaker unavailable` | The speaker is busy, for example with Home Assistant Voice. |
<!-- /only -->
<!-- only esp32-s3 -->
| 503 | `serviceBusy` | `not enough memory for TLS` | An HTTPS station needs more free memory. `field` is `station`. |
<!-- /only -->

### Other routes

| Route | `field` | `message` |
| --- | --- | --- |
| `PATCH /api/v1/display` | `power` | `must be a boolean` |
| `PATCH /api/v1/display` | `overlay` | `must be a string or null` |
| `PATCH /api/v1/display` | `overlay` | `unknown overlay` |
| `PATCH /api/v1/display` | `overlaySettings` | `must be an object` |
| `PATCH /api/v1/display` | `overlaySettings.palette` | `unknown palette` |
| `POST /api/v1/device/sleep` | `durationMs` | `must be an integer > 0` |
| `POST /api/v1/notifications` | *(none)* | `one notification per request` |
| `POST /api/v1/notifications` | `sound`, or `sound.<key>` | the sound is not valid. The messages are the ones of `POST /api/v1/audio/play` below, with the field under `sound`, for example `sound.rtttl` or `sound[1].file` |
| `PUT /api/v1/apps/pushed/{name}` | *(none)* | `body required` - empty or `{}` body |
| `PUT /api/v1/display/moodlight` | *(none)* | `body required` - empty or `{}` body |
| `PUT /api/v1/indicators/{id}` | *(none)* | `body required` - empty or `{}` body |
<!-- only esp32-s3 tc002 -->
| `PUT /api/v1/audio/stations` | *(none)* | `body required` - empty body |
| `PUT /api/v1/audio/stations` | `stations` | `must be an array` |
<!-- /only -->
| `PUT /api/v1/apps/script/{name}` | `source` | `body must be the script` - empty body |
| `PATCH /api/v1/apps/{name}/config`, `PATCH /api/v1/apps/builtin/{name}/config` | the key | `unknown setting` - the app has no setting of that name |
| `PATCH /api/v1/apps/{name}/config`, `PATCH /api/v1/apps/builtin/{name}/config` | *(none)* | `no settings` - the app has no settings at all |
| `PUT /api/v1/apps/pushed/{name}`, `POST /api/v1/notifications` | `effect` | the payload names an effect AWTRIX does not know |
| `PUT /api/v1/apps/pushed/{name}`, `POST /api/v1/notifications` | `overlay` | the payload names an overlay AWTRIX does not know |

Two routes check less:

* `PUT /api/v1/display/moodlight` rejects an empty or `{}` body with `422` (above). A bad
  `brightness` or `kelvin` value is cut to fit instead of rejected. A `color` that is not a color
  gets `422 validationFailed` with `field: "color"`, as on `PUT /api/v1/indicators/{id}`.
* `PUT /api/v1/apps/active` reads the body as the app name as it is, so a malformed body answers
  `404 app not found`, not `400`.

### Per-app effect and overlay names

A pushed app or notification with an unknown `effect` or `overlay` gets `422 validationFailed`
with `field: "effect"` or `field: "overlay"`, and **nothing is stored**. In an array payload, one
bad element rejects the whole array. The accepted names are listed by
[`GET /api/v1/capabilities`](http.md#get-apiv1capabilities). The global `overlay` on
`PATCH /api/v1/display` is checked the same way.

### PUT /api/v1/system

Every number is checked for type and range before anything is saved. A rejected request changes
nothing, and `field` names the key:

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
<!-- only esp32 esp32-s3 -->
| `panelWidth` | 1–128 |
| `panels` | 1–128 |
<!-- /only -->
| `tempOffset` | −20–20 |
| `humOffset` | −50–50 |
| `batteryDividerRatio` | 0.1–10 |
| `ldrFactor` | 0–10 |
| `ldrGamma` | 0.1–10 |
| `brightnessSmoothing` | 0–60000 |
<!-- only esp32 esp32-s3 -->
| any `pin*` | −1 (off), or a GPIO from `0` to `gpioMax` of your chip; the message is `must be -1 or 0..<max>`. The further pin rules are in [GPIO validation](#gpio-validation-invalidpinconfig) |
<!-- /only -->

A value of the wrong type answers `must be an integer` (or `must be a number` on the five decimal
fields). A value of the right type outside the range answers `out of range`.<!-- only esp32 esp32-s3 --> `panelStart`,
`panelWiring` and `panelColorOrder` answer `must be one of: <names>`. `panelSerpentine`, `mirror`
and `rotate` answer `must be a boolean`.<!-- /only -->

These rules look at several keys together. They are checked against the result after your change,
so a request that sets only one of the keys is checked too:

| Condition | `field` | Message |
| --- | --- | --- |
<!-- only esp32 esp32-s3 -->
| `panelWidth × panels` outside 32–128 | `panelWidth` | `width must be <minimum>..<maximum>` |
<!-- /only -->
| `minBrightness` greater than `maxBrightness` | `minBrightness` | `must be <= maxBrightness` |
| `netStatic: true` with an `ip` but no mask | `subnet` | `needs a mask` |
| `netStatic: true` and `subnet` is not a mask like `255.255.255.0` | `subnet` | `not a mask` |
| `netStatic: true` and `ip` is the network or broadcast address, `0.x`, `127.x` or `224.x` and above | `ip` | `not a host address` |
| `netStatic: true` and `gateway`, `dns1` or `dns2` equals `ip` or is `127.x` or `224.x` and above | that key | `not a host address` |

The five static-address fields - `ip`, `gateway`, `subnet`, `dns1`, `dns2` - must be an address
like `192.168.1.50` (no leading zeros) or the empty string. `0.0.0.0` as `gateway` or DNS server
means none. `ip` may also include the mask (`192.168.1.50/24`); it is
then saved as separate `ip` and `subnet` values.

Anything else answers `must be IPv4 or ""`, on `ip` `must be IPv4, IPv4/prefix or ""`. Sending a
`/prefix` on `ip` and a `subnet` in the same request answers `422` on `subnet`:
`mask already set on ip`.

Some switches need other keys first. Each answers `422 validationFailed` with the key in `field`:

| Condition | Field | Message |
| --- | --- | --- |
| `mqttEnabled: true` with an empty `mqttHost` | `mqttHost` | `set mqttHost first` |
| `authEnabled: true` with an empty `authUser`/`authPass` | `authUser` | `set authUser and authPass first` |
| `wifiSsid` set to `""` | `wifiSsid` | `cannot be empty; use <route>` |
<!-- only esp32 esp32-s3 -->
| only some of `pinI2sBclk` / `pinI2sLrclk` / `pinI2sDout` set | the first one not set | `set all three I2S pins or none` |
| `pinI2sMclk` set while the three I2S pins are `-1` | `pinI2sMclk` | `needs the three I2S pins` |
| `pinAmpEnable` set while the three I2S pins are `-1` | `pinAmpEnable` | `needs the three I2S pins` |
<!-- /only -->

`wifiSsid` cannot be emptied, because an empty Wi-Fi name puts AWTRIX into setup mode. Unknown
keys on this route are **ignored**, and other text fields are saved as sent.<!-- only esp32 esp32-s3 --> Pin conflicts -
two functions on one pin, input-only pins, an unsupported LED data pin - answer
`400 invalidPinConfig` instead; see [GPIO validation](#gpio-validation-invalidpinconfig).<!-- /only -->

<!-- only esp32 esp32-s3 -->
## GPIO validation (invalidPinConfig)

Only `PUT /api/v1/system` returns this code. AWTRIX checks **all pins together, as they would be
after your change**, before saving anything. So a rejected request changes nothing, and a single
pin that looks fine can still fail because it clashes with a pin already set.

The status is **400** and there is **no `field`** - the pin's name is in the `message`:

| `message` | Cause |
| --- | --- |
| `pinMatrix: unsupported pin (use …)` | The display cannot be driven from this pin - see [the pin list](gpio.md#1-matrix-pin-whitelist). |
| `<pin>: invalid <chip> GPIO (0-<maximum>, or -1)` | The number does not exist on this chip. |
<!-- only esp32 -->
| `<pin>: GPIO <range> are reserved for <purpose>` | These pins belong to the flash memory. |
| `<pin>: GPIO <range> are input-only` | This function needs a pin that can output. |
<!-- /only -->
<!-- only esp32-s3 -->
| `<pin>: GPIO <range> are reserved for <purpose>` | These pins belong to the flash memory and PSRAM, the USB connection or the serial console. |
<!-- /only -->
| `pinBattery: must be ADC1 (GPIO <range>)` | Other analog pins do not work while Wi-Fi is on. |
| `pinLdr: must be ADC1 (GPIO <range>)` | Same. |
| `duplicate pin <n> (<pinA>, <pinB>)` | Two functions use the same GPIO. When one of them is `pinMatrix`, the message also says how to fix it - see [No duplicates](gpio.md#6-no-duplicates). |

Names and ranges in angle brackets depend on the selected pins and chip. The values per
chip are in [Rules come from the chip](gpio.md#rules-come-from-the-chip) and in
[`GET /api/v1/capabilities`](http.md#gpio-what-the-chip-can-do).

<!-- only esp32 -->
```bash
curl -i -X PUT http://<awtrix-ip>/api/v1/system \
  -H 'Content-Type: application/json' \
  -d '{"pinBuzzer":34}'
# HTTP/1.1 400 Bad Request
# {"error":{"code":"invalidPinConfig","message":"pinBuzzer: GPIO 34-39 are input-only"}}
```
<!-- /only -->
<!-- only esp32-s3 -->
```bash
curl -i -X PUT http://<awtrix-ip>/api/v1/system \
  -H 'Content-Type: application/json' \
  -d '{"pinBuzzer":34}'
# HTTP/1.1 400 Bad Request
# {"error":{"code":"invalidPinConfig","message":"pinBuzzer: GPIO 26-37 are reserved for the SPI flash and PSRAM"}}
```
<!-- /only -->
<!-- /only -->

## Errors over MQTT

MQTT has no status codes. The result of a command is published as a JSON object; `ok` says
whether it worked. A failure contains the **same error object** as over HTTP:

```json
{"ok":false,"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

Success is `{"ok":true}`. <!-- only esp32 tc002 -->Seven<!-- /only --><!-- only esp32-s3 -->Eight<!-- /only --> codes can appear over MQTT:

| Code | When | `message` |
| --- | --- | --- |
| `invalidJson` | the payload is not valid JSON | `invalid JSON` |
| `validationFailed` | a value was rejected | the reason, or `invalid value` |
| `notFound` | unknown app, sound or notification name | `not found`, or for a sound the message HTTP gives, such as `nothing called "<name>"` |
| `insufficientStorage` | the notification queue or the pushed apps are full | the reason, or `storage full` |
| `unavailable` | the command needs hardware this device does not have | for example `no audio output` |
<!-- only esp32-s3 -->
| `serviceBusy` | a radio stream is refused for lack of memory; try again | `not enough memory for TLS` |
<!-- /only -->
| `internalError` | the command failed or was not understood | `command failed` |
| `invalidName` | an app name in the topic is not `[A-Za-z0-9_-]{1,32}` or is a reserved name (`active`, `next`, `previous`, `order`) - only `cmd/apps/pushed/{name}` can return it | `invalid name` |

All other codes are HTTP only. `methodNotAllowed`, `unauthorized`, `forbidden`, `forbiddenOrigin`,
`unsupportedMediaType`, `payloadTooLarge` and `invalidMethodOverride` are about how an HTTP
request is sent.
<!-- only esp32 esp32-s3 -->
`invalidPath`, `badRequest` and `wrongChip` belong to uploads and updates, and `invalidPinConfig`
to `PUT /api/v1/system`. MQTT reaches none of these.
<!-- /only -->
<!-- only tc002 -->
`invalidPath`, `badRequest`, `invalidPackage`, `wrongTarget`, `notNewer`, `updateBusy`,
`insufficientMemory` and `notSupported` belong to uploads and updates, and `invalidPlayer` and
`gamepadsFull` to the gamepad routes. MQTT reaches none of these.
<!-- /only -->

Every rejected command, over MQTT or HTTP, is also published on
[`<P>/event/error`](mqtt.md#eventerror), so one subscription shows all of them.

A command larger than the [MQTT size limit](limits.md#requests) is dropped unread. You get no
`/result` reply at all, not a `payloadTooLarge`.

## Responses that look like errors but are not

| Response | Route | Meaning |
| --- | --- | --- |
| `202 {"scanning":true}` | `GET /api/v1/system/wifi-scan` | The scan is running; ask again for the result. |
| `304`, empty body | `GET /` | Your browser's copy of the web UI is up to date. |
| `302` to `http://<ap-ip>/` | captive-portal reads, setup mode only | Sends a phone or laptop to the setup page. Sent before any login check; writes to a foreign host are refused. |
| connection dropped | `POST /update` | A **successful** firmware upload restarts AWTRIX right away, so the connection drops. `reboot`, `sleep`, `factory-reset` and `settings/reset` answer `200` first, then restart. |

## Related

- [Conventions](conventions.md)
- [Limits](limits.md)
- [HTTP API](http.md)
- [MQTT API](mqtt.md)
