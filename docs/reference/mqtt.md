# MQTT topics

This page lists every MQTT topic AWTRIX reads and publishes. AWTRIX connects to one broker. The
command, state and availability topics below are everything there is. Home Assistant discovery
uses these same topics; it is not a second interface.

Command payloads are exactly the same as the HTTP request bodies, so anything you can send with
`curl` you can also publish. There are no read commands over MQTT: instead, AWTRIX *publishes* its
state to retained topics.

The [Conventions](conventions.md) apply to every topic: camelCase keys, durations in whole
milliseconds with an `...Ms` suffix, the color forms (`"#RRGGBB"` out; `"RRGGBB"`, `"RGB"`,
`[r,g,b]`, `["HSV",h,s,v]` or a number in; `null` = use the default / off) and the error format
`{"error":{"code","message","field?"}}`.

## Connection

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `mqttEnabled` | bool | - | `false` | - | Main switch. **`true` connects to the broker** (needs a `mqttHost`); `false` keeps the settings but never connects. |
| `mqttHost` | string | - | `""` | - | Broker host name or IP address. |
| `mqttPort` | int | 1–65535 | `1883` | - | Broker port. |
| `mqttUser` | string | - | `""` | - | User name. If both user and `mqttPass` are empty, AWTRIX connects without login. |
| `mqttPass` | string | - | `""` | - | Password. |
| `mqttPrefix` | string | - | `""` | - | Topic prefix `<P>`. Empty → the device uid. |
| `haDiscovery` | bool | - | `false` | - | Announce AWTRIX to Home Assistant. |
| `haPrefix` | string | - | `"homeassistant"` | - | Home Assistant discovery prefix. |

These keys are part of the system configuration, not the settings – see
[System configuration](system.md). The MQTT client ID is the device **uid**.

### Topic prefix `<P>` {#the-prefix}

`<P>` is the value of `mqttPrefix`. If you leave it empty, AWTRIX uses its uid – the
12-character MAC address, for example `a4cf12ab34cd/cmd/notify`. That works, but is hard to read,
so set a name.

**Every example on this page uses `awtrixNG` as the prefix.** Topics outside `<P>/` are ignored.

## Command topics

AWTRIX reads only topics under **`<P>/cmd/`**. Topics under `<P>/state/` are only published by
AWTRIX. `<P>/cmd` and `<P>/cmd/` alone do nothing.

| Topic | Payload | HTTP equivalent |
|---|---|---|
| `<P>/cmd/notify` | notification JSON | `POST /api/v1/notifications` |
| `<P>/cmd/notify/dismiss` | ignored | `DELETE /api/v1/notifications/active` |
| `<P>/cmd/notify/dismiss/<name>` | ignored | `DELETE /api/v1/notifications/{name}` |
| `<P>/cmd/apps/pushed/<name>` | pushed-app JSON; empty **or** `{}` deletes | `PUT /api/v1/apps/pushed/{name}` / `DELETE /api/v1/apps/{name}` |
| `<P>/cmd/apps/switch` | app name **or** `{"name":"...","fast":bool}` | `PUT /api/v1/apps/active` |
| `<P>/cmd/apps/next` | ignored | `POST /api/v1/apps/next` |
| `<P>/cmd/apps/previous` | ignored | `POST /api/v1/apps/previous` |
| `<P>/cmd/apps/order` | `{"order":["Time","weather"],"disabled":["Battery"]}`; `disabled` is required, `order` is optional | `PUT /api/v1/apps/order` |
| `<P>/cmd/apps/<name>/enabled` | `true` or `false` | `PUT /api/v1/apps/{name}/enabled` |
| `<P>/cmd/settings` | partial settings JSON | `PATCH /api/v1/settings` |
| `<P>/cmd/settings/reset` | ignored | `POST /api/v1/settings/reset` |
| `<P>/cmd/display` | `{"power":bool?,"overlay":"rain"\|null?}` | `PATCH /api/v1/display` |
| `<P>/cmd/display/moodlight` | mood light JSON; empty = off | `PUT` / `DELETE /api/v1/display/moodlight` |
| `<P>/cmd/indicators/1` \| `/2` \| `/3` | `{"color","blinkMs","fadeMs"}`; empty or `{}` = off | `PUT` / `DELETE /api/v1/indicators/{id}` |
<!-- only esp32 -->
| `<P>/cmd/audio/play` | a [sound object](http.md#the-sound-object): `"ding"`, `{"file"}` \| `{"rtttl"}` \| `{"track"}`, or a list | `POST /api/v1/audio/play` |
| `<P>/cmd/audio/stop` | `{"group":"alert"\|"app"}`; empty or `{}` = everything | `POST /api/v1/audio/stop` |
<!-- /only -->
<!-- only esp32-s3 -->
| `<P>/cmd/audio/play` | a [sound object](http.md#the-sound-object): `"ding"`, `{"file"}` \| `{"rtttl"}` \| `{"track"}` \| `{"station"}`, or a list | `POST /api/v1/audio/play` |
<!-- /only -->
<!-- only tc002 -->
| `<P>/cmd/audio/play` | a [sound object](http.md#the-sound-object): `"ding"`, `{"file"}` \| `{"rtttl"}` \| `{"song"}` \| `{"speech"}` \| `{"station"}`, or a list | `POST /api/v1/audio/play` |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `<P>/cmd/audio/stop` | `{"group":"alert"\|"app"\|"radio"}`; empty or `{}` = everything | `POST /api/v1/audio/stop` |
| `<P>/cmd/audio/stations` | `{"stations":[…]}` | `PUT /api/v1/audio/stations` |
<!-- /only -->
| `<P>/cmd/device/reboot` | ignored | `POST /api/v1/device/reboot` |
| `<P>/cmd/device/sleep` | `{"durationMs":ms}` | `POST /api/v1/device/sleep` |
| `<P>/cmd/screen/get` | ignored | publishes `<P>/state/screen` |
<!-- only tc002 -->
| `<P>/cmd/voice/start` | ignored | - |
<!-- /only -->

A topic that is not in this table gets **no `/result`** at all – no error, no answer. So a typo in
a topic is silent. This includes `<P>/cmd/indicators/<bad-id>`, where HTTP would answer 404
`id must be 1..3`. If a command seems to do nothing, check the topic spelling first.

**Not available over MQTT:** factory reset works only over HTTP
(`POST /api/v1/device/factory-reset`). Publishing to `<P>/cmd/device/factory-reset` does nothing
and gets no answer.

### notify

Publishes a notification. The payload is the same body as the HTTP route - see
[App & notification payload](payload.md).

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/notify' \
  -m '{"text":"Doorbell","textColor":"#FF0000","durationMs":10000}'
```

### notify/dismiss

Dismisses the active notification. Payload is ignored.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/notify/dismiss' -m ''
```

### notify/dismiss/&lt;name&gt;

Dismisses the notification sent with that `name`, wherever it is in the queue. Payload is ignored.
If no notification has that name, the result is `notFound`.

With names, several senders sharing one AWTRIX can each dismiss only their own messages. It is not
a protection – any client that knows the name can dismiss that notification.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/notify'   -m '{"name":"backup-job","text":"Backup running","hold":true}'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/notify/dismiss/backup-job' -m ''
```

### apps/pushed/&lt;name&gt;

`<name>` is the rest of the topic after `apps/pushed/`. It must be 1 to 32 characters from
`A-Z a-z 0-9 _ -` (`[A-Za-z0-9_-]{1,32}`) – the same rule as over HTTP. An **empty payload or
`{}`** deletes the app; anything else creates or replaces it.

```bash
# create / replace
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/pushed/weather' \
  -m '{"text":"21C","icon":"2422"}'

# delete (both forms work)
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/pushed/weather' -m ''
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/pushed/weather' -m '{}'
```

`<P>/cmd/apps/pushed/` with an empty name does nothing. An invalid name is answered on
`<topic>/result` and nothing is stored:

```json
{"ok":false,"error":{"code":"invalidName","message":"invalid name","field":"name"}}
```

This topic removes **pushed** apps only. Scripts cannot be installed or removed over MQTT; to
remove one, use `DELETE /api/v1/apps/{name}` over HTTP.

### apps/switch

Send either **just the app name** or a JSON object. The payload is read as JSON only when it
starts with `{`.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `name` | string | any app id | - | - | App to switch to. |
| `fast` | bool | - | `false` | - | `true` = switch instantly; `false` = animated transition. |

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/switch' -m 'Time'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/switch' \
  -m '{"name":"Time","fast":true}'
```

An unknown app answers `notFound`. A JSON object without `name` is taken as an app name as it
is, and so also answers `notFound`.

### apps/next, apps/previous

Payload ignored.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/next' -m ''
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/previous' -m ''
```

### apps/order

`order` lists the apps that rotate, in that order. `disabled` is the complete list of switched-off
apps: every app it does not name is switched on. An app may appear more than once in `order`.
`disabled` is always required, `order` is optional.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/order' \
  -m '{"order":["Time","weather","Date"],"disabled":["Battery"]}'
```

Send `disabled` alone to keep the order. Battery is then off and every other app on:

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/order' \
  -m '{"disabled":["Battery"]}'
```

### apps/&lt;name&gt;/enabled

Switches one app on (`true`) or off (`false`). Every other app stays as it is. The rules are those
of [PUT /api/v1/apps/{name}/enabled](http.md#put-apiv1appsnameenabled).

<!-- only esp32 esp32-s3 -->
```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/Battery/enabled' -m 'false'
```
<!-- /only -->
<!-- only tc002 -->
```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/Status/enabled' -m 'false'
```
<!-- /only -->

### settings

Any subset of the settings JSON, the same as `PATCH /api/v1/settings`. If one key is wrong,
nothing is applied and the result names it in `field`. All keys, ranges and defaults are in
[Settings](settings.md).

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/settings' \
  -m '{"brightness":120,"autoBrightness":false}'
```

A rejected value answers, for example:

```json
{"ok":false,"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

### settings/reset

Payload ignored. **Deletes the stored settings** and restarts. The `/result` may not arrive
because of the restart.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/settings/reset' -m ''
```

### display

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/display' -m '{"power":false}'
```

### display/moodlight

An **empty payload turns the mood light off**, like `DELETE /api/v1/display/moodlight`.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/display/moodlight' \
  -m '{"color":"#3366FF","brightness":120}'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/display/moodlight' -m ''
```

### indicators/1 … indicators/3

The ID must be **`1`, `2` or `3`**. Anything else is ignored without an answer. The three
indicators are the dots on the display's right edge – `1` top, `2` middle, `3` bottom. Their state
is published in `<P>/state/device`.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `color` | color | - | - | - | Sets the color and turns the indicator **on**. `0` / `null` turns it **off** and keeps the stored color. An unreadable color is rejected. |
| `blinkMs` | int | 0–65535 | `0` | ms | Blink interval. `0` or left out = no blinking. |
| `fadeMs` | int | 0–65535 | `0` | ms | Fade interval. `0` or left out = no fading. |

```bash
# on, red, blinking
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/indicators/1' \
  -m '{"color":"#FF0000","blinkMs":500}'

# full reset (both forms)
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/indicators/1' -m ''
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/indicators/1' -m '{}'
```

An empty payload or `{}` resets the indicator completely (off, black, no blinking or fading).
Every other payload sets blinking and fading anew, so `{"color":"#00FF00"}` gives a steady green
light. Without `color` the indicator keeps its color and stays on or off. `{"color":0}` switches it
off and keeps the stored color.

### audio/play

The payload is a [sound object](http.md#the-sound-object), as for
[`POST /api/v1/audio/play`](http.md#post-apiv1audioplay): a stored name, an object with exactly one
source key, or a list of 1 to 4 of them. The sound plays as an alert.<!-- only esp32-s3 tc002 --> `station` starts the radio.<!-- /only -->

| Key | Type | Value | Plays |
|---|---|---|---|
<!-- only esp32 -->
| `file` | string | a stored name | a stored melody |
<!-- /only -->
<!-- only esp32-s3 -->
| `file` | string | a stored name or `Script/name` | a stored MP3 or melody |
<!-- /only -->
<!-- only tc002 -->
| `file` | string | a stored name, `Script/name`, or an `http(s)://` address | a stored MP3 or melody, or an MP3 from the address |
<!-- /only -->
| `rtttl` | string | RTTTL, up to 512 characters | the melody in the payload |
<!-- only esp32-s3 tc002 -->
| `song` | string | [song text](songs.md) | a song on the synthesizer |
<!-- /only -->
<!-- only tc002 -->
| `speech` | string | 1 to 512 bytes | the text, read aloud (needs a voice) |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `track` | integer | 1–2999 | a DFPlayer track |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `station` | string or integer | a station name, a position from 0, or a stream address | internet radio |
<!-- /only -->
| `loop` | bool | `true` / `false` | not a sound of its own: repeats the sound until it is stopped or a new alert replaces it.<!-- only esp32-s3 tc002 --> Not with `station`<!-- /only --> |

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/audio/play' -m '"ding"'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/audio/play' \
  -m '{"rtttl":"two:d=4,o=5,b=200:c,e"}'
```

<!-- only esp32-s3 tc002 -->
```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/audio/play' -m '{"station":"SWR3"}'
```
<!-- /only -->

A payload without a source key answers:

```json
{"ok":false,"error":{"code":"validationFailed","message":"needs a sound key"}}
```

The errors are those of [`POST /api/v1/audio/play`](http.md#post-apiv1audioplay).<!-- only tc002 --> Song text with an
error answers with `field` `song`; `message` gives the reason, line and column.<!-- /only --> The whole message,
topic<!-- only tc002 --> and song<!-- /only --> included, may be at most 8192 bytes. A longer one is dropped without an answer.

`{"ok":true}` means the sound was accepted. If the master volume or the alert volume is `0`, it
plays silently.

`cmd/audio/stop` stops everything with an empty payload or `{}`. `{"group":"alert"}` stops the
alert that plays<!-- only esp32 --> and `{"group":"app"}` every sound of the scripts<!-- /only --><!-- only esp32-s3 tc002 -->, `{"group":"app"}` every sound of the scripts, and `{"group":"radio"}` the radio<!-- /only -->.
Another value answers `must be alert, app or radio`.

### device/reboot

Payload ignored. Restarts AWTRIX; the `/result` may not arrive because of the restart.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/device/reboot' -m ''
```

### device/sleep

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `durationMs` | int | `> 0` | - | ms | How long to sleep. Missing, not a whole number, or `<= 0` is rejected with `{"ok":false,"error":{"code":"validationFailed","field":"durationMs",...}}` on `.../result`. |

A valid value answers `{"ok":true}` on `.../result`. Right after that AWTRIX goes into deep sleep
and publishes nothing more.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/device/sleep' \
  -m '{"durationMs":60000}'
```

### screen/get

Payload ignored. Publishes `<P>/state/screen` **and** answers `{"ok":true}` on
`<P>/cmd/screen/get/result`.

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/state/screen' &
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/screen/get' -m ''
```

<!-- only tc002 -->
### voice/start

Payload ignored. Starts [Home Assistant Voice](../guides/voice.md), as if you held the
knob, and answers `{"ok":true}`.

It answers `unavailable`, `voice not ready` while Voice is off, not connected yet, already busy, or
the clock is still starting. [`blockNavigation`](settings.md) does not stop it.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/voice/start' -m ''
```
<!-- /only -->

## The `/result` reply

Every command on a topic from the table above is answered on `<cmd topic>/result`, not retained,
QoS 0:

```
awtrixNG/cmd/settings        ->  awtrixNG/cmd/settings/result
awtrixNG/cmd/apps/pushed/x   ->  awtrixNG/cmd/apps/pushed/x/result
```

Success is exactly:

```json
{"ok":true}
```

A failure has the same error body as HTTP, inside `ok:false` (`field` is left out when there is
none):

```json
{"ok":false,"error":{"code":"validationFailed","message":"invalid value","field":"brightness"}}
```

Eight codes can appear here; they and their messages are listed in
[Errors – Errors over MQTT](errors.md#errors-over-mqtt). HTTP-only codes such as
`methodNotAllowed`, `unauthorized` and `unsupportedMediaType` never appear over MQTT.

The `code` values and messages are the same as over HTTP, with one exception: an unknown app in
`cmd/apps/switch` answers `not found`, where HTTP says `app not found`.

AWTRIX never reads a `/result` topic as a command, so its answers cannot trigger new commands.

To see every failure in one place, subscribe to [`<P>/event/error`](#eventerror) instead.

## State topics

| Topic | Payload | Retained | Published |
|---|---|---|---|
| `<P>/state/device` | device JSON (`GET /api/v1/device` shape) | **yes** | every `statsInterval` (default **10 s**), and at once when power or an indicator changes |
| `<P>/state/settings` | settings JSON (`GET /api/v1/settings` shape) | **yes** | on every settings change, and on connect |
| `<P>/state/apps/active` | app name, plain string (not JSON) | **yes** | immediately on change, and on connect |
| `<P>/state/audio` | audio JSON (`GET /api/v1/audio` shape): `radio`, `app`, `alert` and `stations` | **yes** | on every change of the radio, the app or the alert group (play, stop, title change, error), and on connect |
| `<P>/state/capabilities` | `{"effects":[…],"paletteEffects":[…],"transitions":[…],"overlays":[…],"palettes":[…],"audio":{…},"gpio":{…},"sensors":{"light":bool},…}` | **yes** | once per connect |
| `<P>/state/prefix` | `<P>` itself, plain string (not JSON) | **yes** | once per connect |
| `<P>/state/buttons/left` \| `/select` \| `/right` | `"1"` / `"0"` | no | on every press and release, and on connect |
<!-- only tc002 -->
| `<P>/state/buttons/knob` | `"1"` / `"0"` | no | on every push and release of the knob, and on connect |
| `<P>/event/knob` | `{"turn":N}` | no | when the knob turns |
<!-- /only -->
| `<P>/event/error` | `{"source":…,"request":…,"error":{…}}` | no | when a command over MQTT or HTTP is rejected |
| `<P>/state/screen` | `{"width":W,"height":H,"pixels":[…]}` | no | only as the reply to `cmd/screen/get` |

`state/device` is published at least every `statsInterval` (default 10 000 ms, minimum 1 000). It
is also published **at once** when the display power or an indicator changes, so a display switched
off over HTTP does not show as on for another ten seconds. These extra messages come at most every
**250 ms**, so an automation that blinks an indicator sends one message every 250 ms, not one per
frame.

A brightness change does not publish at once; with auto-brightness the value changes all the time
and is sent with the regular interval. `state/settings` and `state/apps/active` are sent whenever
they change and do not use `statsInterval`.

### state/device

Same content as `GET /api/v1/device`; see [Device state](device.md) for every field, including
which fields appear only with a battery pin or a sensor.

### state/buttons/&lt;button&gt;

Published on every press and release, whether or not an app reacts to it, and only on the topic
of the button that changed. It is **not retained**, so a press reaches only the subscribers listening at that moment and is never replayed
later. On each connect AWTRIX sends the current state once and deletes any retained message left
on these topics. The buttons are named `left`, `select` and `right`<!-- only tc002 -->, and the knob's push is `knob`<!-- /only -->. The HTTP button webhook (`buttonCallback`) calls the middle
one `middle`.

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/state/buttons/+' -v
```

<!-- only tc002 -->
### event/knob

Sent when the knob turns. `turn` is the number of clicks, positive clockwise and
negative counterclockwise. A fast turn can arrive as one message with several clicks:

```json
{"turn":3}
```

It is not retained: a new subscriber only sees turns made after it subscribed.

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/event/knob' -v
```
<!-- /only -->

### event/error

Sent whenever AWTRIX rejects a command, whether it came over MQTT or HTTP. Use it to find out why
a payload from an automation does nothing, without watching every `/result` topic or reading HTTP
responses:

```json
{"source":"http","request":"PUT /api/v1/apps/pushed/weather","error":{"code":"validationFailed","message":"not allowed with layout","field":"text"}}
```

```json
{"source":"mqtt","request":"awtrixNG/cmd/notify","error":{"code":"invalidJson","message":"invalid JSON"}}
```

| Field | Content |
|---|---|
| `source` | `mqtt` or `http` |
| `request` | the full MQTT topic, or the HTTP method and path |
| `error` | the same error object the sender got back; see [Errors](errors.md#the-error-body) |

Only commands are reported: pushed apps, notifications, settings, apps, audio, display, indicators,
scripts and the device commands. Failed logins, uploads, firmware updates and unknown paths are
not.

It is not retained: a new subscriber only sees errors that happen after it subscribed.

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/event/error' -v
```

### state/capabilities

The `transitions` list holds the 22 names the `transitionEffect` setting accepts, in this order:

```json
["Random","Slide","Dim","Zoom","Rotate","Pixelate","Curtain","Ripple","Blink","Reload","Fade",
 "Cover","Uncover","Split","Blinds","Blocks","Flash","Diamond","Wave","Rain","Melt","Interlace"]
```

`palettes` is the fixed list of built-in palettes,
`["Cloud","Lava","Ocean","Forest","Stripe","Party","Heat","Rainbow"]`; your own palette files are
not in it. `effects` and `overlays` are the names this clock offers – see
[Visual reference](visuals.md).

### state/prefix

The prefix itself – `awtrixNG/state/prefix` contains `awtrixNG`. Retained, so a new subscriber sees
it at once.

It is meant for Home Assistant: the **MQTT prefix** sensor reads it. A blueprint can then start
from the device you pick and find the topic to publish to.

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/state/prefix' -v
```

### state/screen

`pixels` is a list of `width × height` colors, row by row from the top left. Each color is one
number (`0xRRGGBB` written as a decimal number).

## Retain and QoS

**QoS 0 everywhere** – for publishing, subscribing and the last will. A message lost on the way is
not sent again.

| Topic | Retained |
|---|---|
| `<P>/availability` | **yes** |
| `<P>/state/device` | **yes** |
| `<P>/state/settings` | **yes** |
| `<P>/state/apps/active` | **yes** |
| `<P>/state/audio` | **yes** |
| `<P>/state/capabilities` | **yes** |
| `<P>/state/prefix` | **yes** |
| `<P>/state/buttons/*` | no |
<!-- only tc002 -->
| `<P>/event/knob` | no |
<!-- /only -->
| `<P>/event/error` | no |
| `<P>/state/screen` | no |
| `<cmd topic>/result` | no |
| every HA discovery config, entity state and availability publish | **yes** |

A command you publish may be at most **8192 bytes**, topic included. A larger one is dropped without an error and
without `/result` – a very long notification is the usual way to hit this. Messages AWTRIX
publishes *to you* have no such limit: `state/device` and `state/screen` are sent at full size. All
other limits are in [Limits](limits.md).

## Availability and LWT

There is one availability topic, `<P>/availability` (`online` / `offline`, retained, QoS 0).
AWTRIX publishes `online` as soon as it connects. It also registers `offline` as its "last will"
(LWT) with the broker, so the broker publishes `offline` when AWTRIX disappears without saying
goodbye – for example on a power cut.

| Topic | Payload | Retained |
|---|---|---|
| `<P>/availability` | `online` / `offline` | **yes** |

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/availability' -v
```

Home Assistant discovery uses this same topic (`avty_t`) for every entity. An automation that
watches `<P>/availability` keeps working after you turn on `haDiscovery` – the topic stays the
same.

After a failed connection AWTRIX tries again after **5 s**, then 10, 20, 40, and then every
**60 s**. Each wait is up to 20 % shorter at random. After a successful connection the waits start
again at 5 s.

Whether AWTRIX is connected, and why not, is reported at `GET /api/v1/device` under
[`mqtt`](device.md#connection-status) and in the web UI under **System → MQTT**.

## Home Assistant discovery

Published when `haDiscovery` is on. **There are no extra topics for Home Assistant** – every
entity uses the `<P>/cmd/...` and `<P>/state/...` topics above, with the same checks and the same
`/result` answers. Automations you built with plain MQTT keep working when you turn discovery on.

### One document, one topic

| | Scheme | Example |
|---|---|---|
| Discovery config | `<haPrefix>/device/<uid>/config` | `homeassistant/device/a4cf12ab34cd/config` |
| Shared availability | `<P>/availability` | `awtrixNG/availability` |

This is Home Assistant's **device discovery** format: one retained message with `dev` (the
device), `o` (the origin) and `cmps` (all entities). It needs **Home Assistant 2024.11 or newer**.

- **`<uid>`** – the 12-character lowercase MAC address, also the Home Assistant device ID.
- Each entity's `uniq_id` is `<uid>_<key>`, where `<key>` is its `cmps` key (`mat`, `ind1`,
  `rssi`, …).
- Every entity in `cmps` has its own `"~":"<P>"`, so its topics are written `~/cmd/display`,
  `~/state/device` and so on.
- Keys use the standard Home Assistant short forms: `p`, `stat_t`, `cmd_t`, `val_tpl`, `cmd_tpl`,
  `bri_cmd_t`, `rgb_stat_t`, `avty_t`, `uniq_id`.
- Entity types used: `light`, `select`, `button`, `switch`, `sensor`, `binary_sensor`.

When you turn `haDiscovery` off, AWTRIX publishes an **empty retained message** to the same topic.
This tells Home Assistant to remove the device.

### Device block

Under the `dev` key.

| Key | Value |
|---|---|
| `ids` | the uid (12-hex MAC) |
| `name` | `hostname`, or the literal `AWTRIX NG` when `hostname` is empty |
| `sw` | the firmware version |
| `mf` | `Blueforcer` |
| `mdl` | `AWTRIX NG` |

### Entity set

**19 entities are always created**, plus more for the hardware your clock has. The `cmps` key is
also the end of the `uniq_id`. The **Reads / writes** column shows the topic each entity uses; all
of them are described above.

| Entity | Component | `cmps` key | Reads / writes |
|---|---|---|---|
| Matrix | `light` | `mat` | `cmd/display` (power), `cmd/settings` (brightness, `textColor`) |
| Indicator 1 | `light` | `ind1` | `cmd/indicators/1` |
| Indicator 2 | `light` | `ind2` | `cmd/indicators/2` |
| Indicator 3 | `light` | `ind3` | `cmd/indicators/3` |
| Transition effect | `select` | `transeff` | `cmd/settings` → `transitionEffect`; options are the transition **names** |
| Transition | `switch` | `trans` | `cmd/settings` → `autoTransition` |
| Next app | `button` | `next` | `cmd/apps/next` |
| Previous app | `button` | `prev` | `cmd/apps/previous` |
| Dismiss notification | `button` | `dismiss` | `cmd/notify/dismiss` |
| Current app | `sensor` | `app` | `state/apps/active` (raw payload) |
| Version | `sensor` | `ver` | `state/device` → `version` |
| IP address | `sensor` | `ip` | `state/device` → `ipAddress` |
| MQTT prefix | `sensor` | `prefix` | `state/prefix` (raw payload) |
| WiFi strength | `sensor` | `rssi` | `state/device` → `wifiRssi`; `dBm`, `device_class: signal_strength` |
| Uptime | `sensor` | `uptime` | `state/device` → `uptimeSeconds`; `s`, `device_class: duration` |
| Free RAM | `sensor` | `ram` | `state/device` → `freeHeapBytes`; `B`, `device_class: data_size` |
| Button left | `binary_sensor` | `btnl` | `state/buttons/left` |
| Button select | `binary_sensor` | `btnm` | `state/buttons/select` |
| Button right | `binary_sensor` | `btnr` | `state/buttons/right` |

These entities are created only when the hardware is there, so none of them stays at
`unknown` forever. `<P>/state/device` follows the same rule and leaves those fields out.

| Entity | Component | `cmps` key | Reads | Announced when |
|---|---|---|---|---|
<!-- only esp32 esp32-s3 -->
| Brightness mode | `select` | `brimode` | `cmd/settings` → `autoBrightness`; options `Manual` / `Auto` | a light-sensor pin is set (`pinLdr`) |
| Light level | `sensor` | `light` | `state/device` → `lightLevel`; `%`, no `device_class` | a light-sensor pin is set (`pinLdr`) |
| Temperature | `sensor` | `temp` | `state/device` → `temperature`; `°C` | a sensor is detected |
| Humidity | `sensor` | `hum` | `state/device` → `humidity`; `%` | the sensor is humidity-capable |
| Pressure | `sensor` | `press` | `state/device` → `pressureHpa`; `hPa` | the sensor is a BME280 / BMP280 barometer |
| Battery | `sensor` | `bat` | `state/device` → `batteryPercent`; `%` | a battery pin is set (`pinBattery`) |
| Battery voltage | `sensor` | `batv` | `state/device` → `batteryVoltage`; `V` | a battery pin is set (`pinBattery`) |
| Low battery | `binary_sensor` | `lowbat` | `state/device` → `lowBattery` | a battery pin is set (`pinBattery`) |
<!-- /only -->
<!-- only tc002 -->
| Battery | `sensor` | `bat` | `state/device` → `batteryPercent`; `%` | the clock has reported its battery |
| Battery voltage | `sensor` | `batv` | `state/device` → `batteryVoltage`; `V` | the clock has reported its battery |
| Low battery | `binary_sensor` | `lowbat` | `state/device` → `lowBattery` | the clock has reported its battery |
| Charging | `binary_sensor` | `chg` | `state/device` → `usbPower`; `device_class: battery_charging` | always |
| Knob | `binary_sensor` | `btnk` | `state/buttons/knob` | always |
| Knob turn | `event` | `knob` | `event/knob`; event types `clockwise` / `counterclockwise`, attribute `steps` | always |
| Assist | `button` | `assist` | `cmd/voice/start` | Home Assistant Voice is available |
<!-- /only -->
<!-- only esp32 -->
| Volume | `number` | `vol` | `cmd/settings` → `volume`; 0–100, step 5, `%` | the clock has a sound output (buzzer or DFPlayer) |
<!-- /only -->
<!-- only esp32-s3 -->
| Volume | `number` | `vol` | `cmd/settings` → `volume`; 0–100, step 5, `%` | the clock has a sound output (buzzer, speaker or DFPlayer) |
<!-- /only -->
<!-- only tc002 -->
| Volume | `number` | `vol` | `cmd/settings` → `volume`; 0–100, step 5, `%` | always |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| Radio volume | `number` | `radvol` | `cmd/settings` → `radioVolume`; 0–100, step 5, `%` | the clock has a sound output and plays internet radio |
<!-- /only -->
| App volume | `number` | `appvol` | `cmd/settings` → `appVolume`; 0–100, step 5, `%` | the clock has a sound output |
| Alert volume | `number` | `alrtvol` | `cmd/settings` → `alertVolume`; 0–100, step 5, `%` | the clock has a sound output |
| Stop sound | `button` | `stopsnd` | `cmd/audio/stop` with `{}` | the clock has a sound output |

<!-- only esp32 esp32-s3 -->
A clock with a battery pin, a light sensor and a temperature/humidity sensor has **26** entities
without the sound entities. A clock with none of this hardware has only the 19 base entities.
<!-- /only -->

Version, IP address, MQTT prefix, WiFi strength, Uptime, Free RAM and Battery voltage are marked
`entity_category: diagnostic`, so Home Assistant shows them under diagnostics, not on the main
device card.

The button `binary_sensor`s use the `<P>/state/buttons/...` topics. They are `1` while a
button is held and `0` when it is released, so you can use them as automation triggers. After
Home Assistant restarts they show `unknown` until the next press or until AWTRIX reconnects.<!-- only tc002 -->
**Knob turn** fires once per `event/knob` message: `clockwise` or `counterclockwise`, with
the number of clicks in `steps`.<!-- /only -->

### What the entities write

- **Matrix – state** writes `power`: turns the display on and off. (Shown as `matrixPower` in
  device state.)
- **Matrix – brightness** writes `brightness`.<!-- only esp32 esp32-s3 --> This does nothing while `autoBrightness` is on and
  the clock has a light sensor. The slider shows the brightness the display really uses, so with
  auto-brightness on it moves with the room light. Set **Brightness mode** to `Manual` to control
  the display with the slider.<!-- /only -->
- **Matrix – RGB** writes `textColor`, the **default text color** – it does not tint the display.
- **Indicator *n*** writes `indicators[n]`. The switch turns it on in white; use the color picker
  for other colors. There is no "on with the last color" command. Both stop blinking and fading.
  `blinkMs` and `fadeMs` can only be set with `<P>/cmd/indicators/<id>`.

Entity states come from the state topics. A change made over HTTP or `<P>/cmd/...` reaches Home
Assistant the next time that state topic is published:

- **Display power and the three indicators** are in `state/device`, which is then published within
  250 ms – faster changes are combined into one message.
- **Everything else in `state/device`**, brightness included, waits for the next `statsInterval`.
- **Settings entities** (<!-- only esp32 esp32-s3 -->Brightness mode, <!-- /only -->Transition effect, Transition, the <!-- only esp32 -->three<!-- /only --><!-- only esp32-s3 tc002 -->four<!-- /only --> volumes and the
  Matrix RGB color) are in `state/settings`, which is published right after every change.
- **Current app** is in `state/apps/active`, published as soon as the app changes.

## Related

- [HTTP API](http.md) – the same commands over HTTP
- [App & notification payload](payload.md) – what goes into `notify` and `apps/pushed`
- [Device state](device.md) – the content of `state/device`
- [Home Assistant](../guides/home-assistant.md)
- [Errors](errors.md#errors-over-mqtt)
