# MQTT topics

AWTRIX connects to one broker. The command, state and availability topics below
are the entire interface - Home Assistant discovery is an optional retained
document layered on top of them, not a second way in.

Command payloads are byte-identical to the HTTP request bodies, so anything you
can `curl` you can also publish. Reads have no MQTT equivalent: `GET` routes are
served over HTTP only, and AWTRIX instead *pushes* its state to retained topics.

The [Conventions](conventions.md) - camelCase keys, integer millisecond
durations with an `...Ms` suffix, the color forms (`"#RRGGBB"` out;
`"RRGGBB"`, `"RGB"`, `[r,g,b]`, `["HSV",h,s,v]` or a packed integer in;
`null` = inherit/off) and the `{"error":{"code","message","field?"}}` error
shape - hold on every topic below.

## Connection

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `mqttEnabled` | bool | - | `false` | - | Master switch. **`true` runs the client** (requires a non-empty `mqttHost`); `false` keeps the settings but never connects. |
| `mqttHost` | string | - | `""` | - | Broker host. |
| `mqttPort` | int | 1–65535 | `1883` | - | Broker port. |
| `mqttUser` | string | - | `""` | - | Username. Empty **and** empty `mqttPass` = anonymous connect. |
| `mqttPass` | string | - | `""` | - | Password. |
| `mqttPrefix` | string | - | `""` | - | Topic prefix `<P>`. Empty → the device uid. |
| `haDiscovery` | bool | - | `false` | - | Publish the Home Assistant discovery document. |
| `haPrefix` | string | - | `"homeassistant"` | - | HA discovery prefix. |

These live in the device configuration, not in settings - see
[System configuration](system.md). The MQTT client id is the device **uid**.

### Topic prefix `<P>`

`<P>` is whatever you put in `mqttPrefix`. Leave it empty and AWTRIX falls back
to its uid - the 12-character MAC, e.g. `a4cf12ab34cd/cmd/notify` - which is
unique but unreadable, so setting a name is worth the two seconds.

**Every example on this page uses `awtrixNG` as the prefix.** Topics outside
`<P>/` are ignored.

## Command topics

Only topics under **`<P>/cmd/`** are read; everything under `<P>/state/` is
outbound-only. `<P>/cmd` and `<P>/cmd/` on their own match nothing.

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
| `<P>/cmd/settings` | partial settings JSON | `PATCH /api/v1/settings` |
| `<P>/cmd/settings/reset` | ignored | `POST /api/v1/settings/reset` |
| `<P>/cmd/display` | `{"power":bool?,"overlay":"rain"\|null?}` | `PATCH /api/v1/display` |
| `<P>/cmd/display/moodlight` | moodlight JSON; empty = off | `PUT` / `DELETE /api/v1/display/moodlight` |
| `<P>/cmd/indicators/1` \| `/2` \| `/3` | `{"color","blinkMs","fadeMs"}`; empty or `{}` = off | `PUT` / `DELETE /api/v1/indicators/{id}` |
| `<P>/cmd/audio/play` | `{"sound"}` \| `{"mp3"}` \| `{"melody"}` \| `{"track"}` \| `{"rtttl"}` \| `{"station"}` \| `{"index"}` \| `{"url"}` | `POST /api/v1/audio/play` |
| `<P>/cmd/audio/stop` | ignored | `POST /api/v1/audio/stop` |
| `<P>/cmd/audio/stations` | `{"stations":[…]}` | `PUT /api/v1/audio/stations` |
| `<P>/cmd/device/reboot` | ignored | `POST /api/v1/device/reboot` |
| `<P>/cmd/device/sleep` | `{"durationMs":ms}` | `POST /api/v1/device/sleep` |
| `<P>/cmd/screen/get` | ignored | publishes `<P>/state/screen` |

A topic that matches no route produces **no `/result`** at all - no error, no
acknowledgement, so typos are invisible. This includes
`<P>/cmd/indicators/<bad-id>`, where the HTTP route would return a 404
`indicator id must be 1..3`. If a command seems to vanish, check the topic
spelling first.

**Not available over MQTT:** factory reset is HTTP-only
(`POST /api/v1/device/factory-reset`). Publishing to
`<P>/cmd/device/factory-reset` does nothing and answers nothing.

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

Dismisses the notification pushed with that `name`, wherever it sits in the
queue. Payload is ignored. If nothing in the queue carries the name, the result
is `notFound`.

The name lets several senders sharing one AWTRIX dismiss only their own
messages. It is not access control - any client that knows the name can dismiss
that notification.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/notify'   -m '{"name":"backup-job","text":"Backup running","hold":true}'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/notify/dismiss/backup-job' -m ''
```

### apps/pushed/&lt;name&gt;

`<name>` is the remainder of the topic after `apps/pushed/`, and it must match
`[A-Za-z0-9_-]{1,32}` - the same rule the HTTP route applies. An **empty payload
or the literal `{}`** deletes the app; anything else creates or replaces it.

```bash
# create / replace
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/pushed/weather' \
  -m '{"text":"21C","icon":"2422"}'

# delete (both forms work)
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/pushed/weather' -m ''
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/pushed/weather' -m '{}'
```

`<P>/cmd/apps/pushed/` with an empty name matches nothing. A malformed name is
answered on `<topic>/result` and nothing is stored:

```json
{"ok":false,"error":{"code":"invalidName","message":"name must match [A-Za-z0-9_-]{1,32}","field":"name"}}
```

This topic removes a **pushed** app only. Scripts have no MQTT topic - their
payload is Berry source, not JSON - so removing one is
`DELETE /api/v1/apps/{name}` over HTTP.

### apps/switch

Accepts either a **bare app name** or a JSON object. JSON is only parsed when
the payload starts with `{`.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `name` | string | any app id | - | - | App to switch to. |
| `fast` | bool | - | `false` | - | `true` = switch instantly; `false` = animated transition. |

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/switch' -m 'Time'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/switch' \
  -m '{"name":"Time","fast":true}'
```

An unknown app answers `notFound`. A JSON body that parses but has no `name` is
treated as a literal app name, and therefore also answers `notFound`.

### apps/next, apps/previous

Payload ignored.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/next' -m ''
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/previous' -m ''
```

### apps/order

`order` is what runs, in the order it draws; `disabled` is what is switched off. Duplicates in `order`
are allowed. An app named in neither list keeps what it had. `disabled` is always required, `order` is
optional.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/order' \
  -m '{"order":["Time","weather","Date"],"disabled":["Battery"]}'
```

`disabled` can be sent on its own, so one app can be switched off without resending the arrangement:

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/apps/order' \
  -m '{"disabled":["Battery"]}'
```

### settings

Any subset of the settings JSON, checked as a whole - if one key fails, nothing at
all is applied and the result carries `field`. The accepted keys, ranges and defaults
are the full table in [Settings](settings.md); the payload is identical to
`PATCH /api/v1/settings`.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/settings' \
  -m '{"brightness":120,"autoBrightness":false}'
```

A rejected value answers, for example:

```json
{"ok":false,"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

### settings/reset

Payload ignored. **Clears the stored settings** and reboots - the `/result`
publish may not survive the restart.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/settings/reset' -m ''
```

### display

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/display' -m '{"power":false}'
```

### display/moodlight

An **empty payload turns the moodlight off** - the MQTT clear idiom, equivalent
to `DELETE /api/v1/display/moodlight`.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/display/moodlight' \
  -m '{"color":"#3366FF","brightness":120}'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/display/moodlight' -m ''
```

### indicators/1 … indicators/3

The id must be a **single character `1`, `2` or `3`**. Anything else matches no
route and is silently dropped. The three indicators are the pixels on the
panel's right edge - `1` top, `2` middle, `3` bottom - and their state is
reported in `<P>/state/device`.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `color` | color | - | - | - | Sets the color and turns the indicator **on**. `0` / `null` turns it **off** while keeping the stored color. An unparseable value is rejected. |
| `blinkMs` | int | 0–65535 | `0` | ms | Blink period. `0` = solid. Left unchanged when absent from a partial update; only an empty payload or `{}` resets it to `0`. |
| `fadeMs` | int | 0–65535 | `0` | ms | Fade period. `0` = no fade. Left unchanged when absent from a partial update; only an empty payload or `{}` resets it to `0`. |

```bash
# on, red, blinking
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/indicators/1' \
  -m '{"color":"#FF0000","blinkMs":500}'

# full reset (both forms)
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/indicators/1' -m ''
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/indicators/1' -m '{}'
```

Note the asymmetry: an empty payload or `{}` resets the indicator completely
(off, color black, no blink/fade), while `{"color":0}` only clears `on` and
preserves the stored color. A payload that omits `color`, `blinkMs` or `fadeMs`
leaves that field exactly as it was; only the empty payload or `{}` zeroes
everything at once.

### audio/play

Send **exactly one** key, and the key chooses which output answers. Sending more than one is
refused whole - `field` names the first of them in the order above - and nothing plays. Only `sound` looks at
more than one output; the rest never fall back. Full rules:
[HTTP API - POST /api/v1/audio/play](http.md#post-apiv1audioplay).

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `sound` | string | a name | - | - | A stored MP3, else a melody, else a DFPlayer track if the name is a plain number. |
| `mp3` | string | a stored MP3 | - | - | Play `/MP3/<name>.mp3`. |
| `melody` | string | a stored melody | - | - | Play `/MELODIES/<name>.txt`. |
| `track` | integer | 1-2999 | - | - | Play that track from the DFPlayer's SD card. |
| `rtttl` | string | RTTTL | - | - | Play an inline RTTTL melody on the buzzer. |
| `station` | string | a stored station | - | - | Start the radio on that station. |
| `index` | integer | 0-31 | - | - | Start the radio on that position in the list. |
| `url` | string | http/https | - | - | Start the radio on a stream that is not stored. |

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/audio/play' -m '{"mp3":"beep"}'
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/audio/play' \
  -m '{"rtttl":"two:d=4,o=5,b=200:c,e"}'
```

No recognised key answers:

```json
{"ok":false,"error":{"code":"validationFailed","message":"exactly one of \"sound\", \"mp3\", \"melody\", \"track\", \"rtttl\", \"station\", \"index\" or \"url\" is required"}}
```

When `soundEnabled` is `false`, the one-shot keys answer `{"ok":true}` and nothing
is played. A successful result is not a guarantee that anything was heard. A radio stream is not
muted by that switch.

`cmd/audio/stop` takes an optional `{"scope":"sounds"|"stream"|"all"}`; with no payload it stops
everything.

### device/reboot

Payload ignored. Reboots; the `/result` publish may not survive.

```bash
mosquitto_pub -h broker.local -t 'awtrixNG/cmd/device/reboot' -m ''
```

### device/sleep

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `durationMs` | int | `> 0` | - | ms | Deep-sleep duration. Missing, non-integer or `<= 0` is rejected - `{"ok":false,"error":{"code":"validationFailed","field":"durationMs",...}}` on `.../result`. |

A valid value answers `{"ok":true}` on `.../result`, then AWTRIX enters deep
sleep on the next loop pass; nothing is published after that.

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

## The `/result` reply

Every command that **matches a route** is answered on `<cmd topic>/result`,
non-retained, QoS 0:

```
awtrixNG/cmd/settings        ->  awtrixNG/cmd/settings/result
awtrixNG/cmd/apps/pushed/x   ->  awtrixNG/cmd/apps/pushed/x/result
```

Success is exactly:

```json
{"ok":true}
```

Failure carries the same error body as HTTP, wrapped in `ok:false`
(`field` is omitted when empty):

```json
{"ok":false,"error":{"code":"validationFailed","message":"invalid value","field":"brightness"}}
```

Eight codes can appear here, with the messages they carry:
[Errors - Errors over MQTT](errors.md#errors-over-mqtt). The framing codes HTTP uses
(`methodNotAllowed`, `unauthorized`, `unsupportedMediaType`, …) have no MQTT equivalent.

The `code` values match HTTP exactly; two messages are less specific. `notFound`
collapses to a bare `not found` where HTTP distinguishes `app not found` from
`no MP3 of that name`, and `invalidJson` says `payload` where HTTP says
`request body`.

A `/result` topic is never itself read as a command, so replies published back
to the broker do not loop.

## State topics

| Topic | Payload | Retained | Published |
|---|---|---|---|
| `<P>/state/device` | device JSON (`GET /api/v1/device` shape) | **yes** | every `statsInterval` (default **10 s**), and at once when power or an indicator changes |
| `<P>/state/settings` | settings JSON (`GET /api/v1/settings` shape) | **yes** | on every settings change, and on connect |
| `<P>/state/apps/active` | app name, plain string (not JSON) | **yes** | immediately on change, and on connect |
| `<P>/state/audio` | audio JSON (`GET /api/v1/audio` shape) | **yes** | on play, stop, track change and error, and on connect |
| `<P>/state/capabilities` | `{"effects":[…],"paletteEffects":[…],"transitions":[…],"overlays":[…],"palettes":[…],"radio":bool,"gpio":{…}}` | **yes** | once per connect |
| `<P>/state/prefix` | `<P>` itself, plain string (not JSON) | **yes** | once per connect |
| `<P>/state/buttons/left` \| `/select` \| `/right` | `"1"` / `"0"` | **yes** | on button edge, and on connect |
| `<P>/state/screen` | `{"width":W,"height":H,"pixels":[…]}` | no | only as the reply to `cmd/screen/get` |

`statsInterval` (default 10 000 ms, floored at 1 000) is the slowest that
`state/device` publishes, not the only trigger. It is also published
**immediately** when the matrix power or an indicator changes, so a panel
switched off over HTTP does not read as still on for another ten seconds.
Consecutive change-driven publishes are spaced at least **250 ms** apart, so an
automation blinking an indicator sends one message every 250 ms instead of one
per rendered frame.

Brightness is not a trigger; with auto-brightness on it moves continuously and
still rides the timer. `state/settings` and `state/apps/active` are event-driven
and ignore `statsInterval` entirely.

### state/device

Same shape as `GET /api/v1/device`; see [Device state](device.md) for the full
list of fields that are always present (which fields depend on a battery pin or a
sensor follows the same rules there).

### state/buttons/&lt;button&gt;

Published on every button **edge**, whether or not an app acts on the press, and
**retained** - the value is also re-sent on each connect, so a fresh subscriber
(or a restarted Home Assistant) immediately sees the current level instead of
`unknown`. The buttons are named `left`, `select` and `right`; the separate HTTP
button webhook (`buttonCallback`) calls the middle one `middle`.

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/state/buttons/+' -v
```

### state/capabilities

The `transitions` list is the same 22 names the `transitionEffect` setting
accepts, in index order:

```json
["Random","Slide","Dim","Zoom","Rotate","Pixelate","Curtain","Ripple","Blink","Reload","Fade",
 "Cover","Uncover","Split","Blinds","Blocks","Flash","Diamond","Wave","Rain","Melt","Interlace"]
```

`palettes` is the fixed list of built-ins,
`["Cloud","Lava","Ocean","Forest","Stripe","Party","Heat","Rainbow"]`; palette files are not in it.
`effects` and `overlays` are the names this board actually offers - see
[Visual reference](visuals.md).

### state/prefix

The prefix itself, published under it - `awtrixNG/state/prefix` carries `awtrixNG`.
Retained, so a fresh subscriber sees it without waiting for a reconnect.

It exists for Home Assistant: the **MQTT prefix** sensor reads it, which is what lets a
blueprint start from the device you picked and end up with the topic to publish to. Over
plain MQTT you already know the prefix - you had to, to subscribe.

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/state/prefix' -v
```

### state/screen

`pixels` is a flat array of packed RGB integers (`0xRRGGBB` as unsigned
decimal), `width × height` entries.

## Retain and QoS

**QoS 0 everywhere** - publishes, subscriptions and the last will - so a message
lost in transit is lost silently.

| Topic | Retained |
|---|---|
| `<P>/availability` | **yes** |
| `<P>/state/device` | **yes** |
| `<P>/state/settings` | **yes** |
| `<P>/state/apps/active` | **yes** |
| `<P>/state/audio` | **yes** |
| `<P>/state/capabilities` | **yes** |
| `<P>/state/prefix` | **yes** |
| `<P>/state/buttons/*` | **yes** |
| `<P>/state/screen` | no |
| `<cmd topic>/result` | no |
| every HA discovery config, entity state and availability publish | **yes** |

A command you publish must fit in **8192 bytes**. Anything larger is dropped
before it is parsed, with no error and no `/result` - a big notification is the
realistic way to hit it. What AWTRIX publishes *to you* has no such limit:
`state/device` and `state/screen` go out at whatever size they are. Every other
cap a command can run into is in [Limits](limits.md).

## Availability and LWT

There is one availability topic, `<P>/availability` (`online` / `offline`,
retained, QoS 0). It is registered as the broker-side last will at CONNECT
(`offline`, retained) and published as `online` immediately on a successful
connect, so the broker publishes `offline` on an ungraceful disconnect.

| Topic | Payload | Retained |
|---|---|---|
| `<P>/availability` | `online` / `offline` | **yes** |

```bash
mosquitto_sub -h broker.local -t 'awtrixNG/availability' -v
```

The discovery document declares `avty_t` once at the device level and points it
at this same topic, so it doubles as the availability source for every entity.
An automation keyed on `<P>/availability` keeps working after you enable
`haDiscovery` - the topic does not move.

A failed connection is retried after **5 s**, then 10, 20, 40, and at most every
**60 s**, each delay shortened by up to 20% of jitter. A successful connection
resets the schedule.

Whether AWTRIX is connected, and why not, is reported at `GET /api/v1/device` under
[`mqtt`](device.md#connection-status) and on the web UI's MQTT tab.

## Home Assistant discovery

Published when `haDiscovery` is on. **There is no separate entity topic tree** -
every entity points at the `<P>/cmd/...` and `<P>/state/...` topics documented
above, with the same validation and the same `/result` publish, so an automation
you built against plain MQTT keeps working unchanged once you turn discovery on.

### One document, one topic

| | Scheme | Example |
|---|---|---|
| Discovery config | `<haPrefix>/device/<uid>/config` | `homeassistant/device/a4cf12ab34cd/config` |
| Shared availability | `<P>/availability` | `awtrixNG/availability` |

This is Home Assistant's **device discovery** format: a single retained payload
carrying `dev` (the device), `o` (the origin) and `cmps` (every component).
It requires **Home Assistant 2024.11 or newer**.

- **`<uid>`** - the 12-character lowercase MAC, also the HA device identifier.
- Each component's `uniq_id` is `<uid>_<key>`, where `<key>` is the `cmps` key
  (`mat`, `ind1`, `rssi`, …).
- `~` is declared independently inside every component in `cmps` - each one
  carries its own `"~":"<P>"` - so topics inside a component are still written
  `~/cmd/display`, `~/state/device` and so on.
- Keys use the standard HA abbreviations: `p`, `stat_t`, `cmd_t`, `val_tpl`,
  `cmd_tpl`, `bri_cmd_t`, `rgb_stat_t`, `avty_t`, `uniq_id`.
- Components in use: `light`, `select`, `button`, `switch`, `sensor`,
  `binary_sensor`.

Turning `haDiscovery` off publishes an **empty retained payload** to the same
topic, which is how Home Assistant is told to drop the device.

### Device block

Sent once per message, under the `dev` key.

| Key | Value |
|---|---|
| `ids` | the uid (12-hex MAC) |
| `name` | `hostname`, or the literal `AWTRIX NG` when `hostname` is empty |
| `sw` | the firmware version |
| `mf` | `Blueforcer` |
| `mdl` | `AWTRIX NG` |

### Entity set

**20 entities are always created**, plus one per capability the board actually
has. The `cmps` key doubles as the `uniq_id` suffix; the **Reads / writes**
column is the topic the entity is bound to, all of them documented above.

| Entity | Component | `cmps` key | Reads / writes |
|---|---|---|---|
| Matrix | `light` | `mat` | `cmd/display` (power), `cmd/settings` (brightness, `textColor`) |
| Indicator 1 | `light` | `ind1` | `cmd/indicators/1` |
| Indicator 2 | `light` | `ind2` | `cmd/indicators/2` |
| Indicator 3 | `light` | `ind3` | `cmd/indicators/3` |
| Brightness mode | `select` | `brimode` | `cmd/settings` → `autoBrightness`; options `Manual` / `Auto` |
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

The seven conditional entities are announced only when the hardware is there, so
none of them sits at `unknown` waiting for a value that never arrives - the same
rule `<P>/state/device` follows when it omits those fields.

| Entity | Component | `cmps` key | Reads | Announced when |
|---|---|---|---|---|
| Light level | `sensor` | `light` | `state/device` → `lightLevel`; `%`, no `device_class` | a light-sensor pin is set (`pinLdr`) |
| Temperature | `sensor` | `temp` | `state/device` → `temperature`; `°C` | a sensor is detected |
| Humidity | `sensor` | `hum` | `state/device` → `humidity`; `%` | the sensor is humidity-capable |
| Pressure | `sensor` | `press` | `state/device` → `pressureHpa`; `hPa` | the sensor is a BME280 / BMP280 barometer |
| Battery | `sensor` | `bat` | `state/device` → `batteryPercent`; `%` | a battery pin is set (`pinBattery`) |
| Battery voltage | `sensor` | `batv` | `state/device` → `batteryVoltage`; `V` | a battery pin is set (`pinBattery`) |
| Low battery | `binary_sensor` | `lowbat` | `state/device` → `lowBattery` | a battery pin is set (`pinBattery`) |

A **fully equipped board** - battery pin, light sensor and a
temperature/humidity sensor - registers **26** entities. A board with none of
that hardware registers the 20 base entities and nothing else.

Version, IP address, MQTT prefix, WiFi strength, Uptime, Free RAM and Battery
voltage are marked `entity_category: diagnostic`, so Home Assistant files them
under diagnostics instead of the main device card.

The three button `binary_sensor`s bind to the `<P>/state/buttons/...` topics,
which report `1` while a button is held and `0` on release, so they work as
Home Assistant automation triggers.

### What the entities write

- **Matrix - state** writes `power`: turns the panel on and off. (It reads
  back as `matrixPower` in device state.)
- **Matrix - brightness** writes `brightness`, which does nothing while
  `autoBrightness` is on. The slider reads back the brightness actually in
  force, so with auto-brightness on it drifts to whatever the light sensor is
  driving rather than staying where you left it. Set **Brightness mode** to
  `Manual` for the slider to control and reflect the panel.
- **Matrix - RGB** writes `textColor`, the **global text color**, not a panel
  tint.
- **Indicator *n*** writes `indicators[n]`. The toggle sends white; use the
  colour picker for any other colour. There is no "on with the previous colour"
  command, and `blinkMs` / `fadeMs` are reachable only via
  `<P>/cmd/indicators/<id>`.

Entity states come from the state topics, so a change made over HTTP or
`<P>/cmd/...` reaches Home Assistant on the next publish of the topic that
carries it:

- **Matrix power and the three indicators** ride `state/device`, which publishes
  at most once per 250 ms - faster changes are merged into one message.
- **Everything else on `state/device`**, brightness included, waits for the
  `statsInterval` tick.
- **The settings-backed entities** (Brightness mode, Transition effect,
  Transition and the Matrix RGB colour) ride `state/settings`, which has no such
  floor: every change publishes on the very next tick.
- **Current app** rides `state/apps/active` and publishes as soon as the app
  changes.
