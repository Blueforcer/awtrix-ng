# MQTT automation

You have a broker, a script, and something you want on the panel - a doorbell, a build status, a power reading. MQTT is the transport for that: the broker relays commands and state, so AWTRIX stays reachable even when your automation host never talks to it directly.

**AWTRIX subscribes to `<prefix>/cmd/#` and answers every command on `<topic>/result`.** Command payloads are byte-identical to the HTTP request bodies, so anything you can `curl` you can publish, unchanged.

## Point AWTRIX at your broker

MQTT is off until you switch it on. Set a host and flip the gate:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H 'Content-Type: application/json' \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,"mqttPrefix":"awtrixNG"}'
```

Then reboot - the MQTT configuration is read once, at startup:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

If your broker wants credentials, add `mqttUser` and `mqttPass`. With both empty AWTRIX connects anonymously. Every key, its default and its type is in [System configuration → MQTT and Home Assistant](../reference/system.md#mqtt-and-home-assistant).

To see whether it worked, read `GET /api/v1/device`: the [`mqtt`](../reference/device.md#connection-status) object says whether AWTRIX is connected and, if not, why. The web UI shows the same thing on its MQTT tab.

To turn MQTT off again, flip the gate: `{"mqttEnabled":false}`, then reboot - the running client keeps talking to the broker until AWTRIX restarts. The host and credentials are kept.

## Publish your first command

```bash
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/notify' \
  -m '{"text":"Doorbell","textColor":"#FF0000","durationMs":10000}'
```

The panel interrupts whatever it was showing and says `DOORBELL` in red for ten seconds. That is the same object you would have sent to `POST /api/v1/notifications` - same keys, same defaults, same behaviour.

Watch the answer come back on a second terminal:

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/cmd/#' -v
```

```
awtrixNG/cmd/notify {"text":"Doorbell","textColor":"#FF0000","durationMs":10000}
awtrixNG/cmd/notify/result {"ok":true}
```

An MQTT payload is the raw bytes you publish - there is no header to forget. The `curl` calls on this page are a different matter: `Content-Type` is [mandatory on every write](../reference/conventions.md#content-type-is-mandatory).

## The prefix

`<prefix>` is whatever you set as `mqttPrefix`. Leave it empty and it defaults to AWTRIX's own **uid** - the twelve-character MAC - so topics read `a4cf12ab34cd/cmd/notify`. That works, but a readable prefix is worth the thirty seconds it costs to set.

The prefix is also the fence: topics outside `<prefix>/` are ignored, and only topics under `<prefix>/cmd/` are treated as commands. State topics are outbound-only - publishing to one does nothing.

Give every AWTRIX on your broker its own prefix. Two sharing one both act on every command sent to it.

## Anything you can curl, you can publish

The translation from an HTTP recipe to an MQTT one is mechanical: take the path after `/api/v1/`, put `cmd/` in front of it, and publish the body you would have sent.

```bash
# HTTP
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Build failed","textColor":"#FF0000"}'

# MQTT - same body, byte for byte
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/notify' \
  -m '{"text":"Build failed","textColor":"#FF0000"}'
```
<!-- shot:begin id=build-failed hash=0c19e8d2 -->
![The panel showing "Build failed" in red](../assets/shots/mqtt/build-failed.gif){ .shot }
<!-- shot:end -->


It holds everywhere:

```bash
# a pushed app that stays in the rotation
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/apps/pushed/power' \
  -m '{"text":"432W","icon":"1234","textColor":"#FFAA00"}'

# an empty payload deletes it again
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/apps/pushed/power' -m ''

# settings, validated exactly as PATCH /api/v1/settings validates them
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/settings' \
  -m '{"brightness":120,"autoBrightness":false}'

# jump to an app - a bare name works, no JSON needed
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/apps/switch' -m 'Time'
```

The full topic list, with the HTTP route each one mirrors and the payload each one accepts, is the table in [MQTT topics → Command topics](../reference/mqtt.md#command-topics). The payload keys themselves - text, icons, colors, effects, fragments - are the same reference the HTTP guides point at: [App & notification payload](../reference/payload.md).

Four places where the mirror is not perfect, all worth knowing before you script against them:

- **Reads are HTTP-only.** There is no `cmd/device/get`. AWTRIX *pushes* its state to retained
  topics instead - see [Subscribe to state](#subscribe-to-state) below. The one exception is
  `cmd/screen/get`, which asks for a one-off `state/screen` publish.
- **Factory reset is HTTP-only.** Publishing to `cmd/device/factory-reset` does nothing and answers
  nothing; the route is `POST /api/v1/device/factory-reset`.
- **MQTT reaches pushed apps only.** `cmd/apps/pushed/<name>` creates, replaces and deletes a
  pushed app. Scripts have no topic at all, and removing one is `DELETE /api/v1/apps/{name}`
  over HTTP.
- **Empty payloads mean something.** An empty payload - or a literal `{}` - is how you delete a
  pushed app, clear the moodlight or turn an indicator off, the equivalent of the HTTP `DELETE`.
  Neither is a way to "poke" a topic.

A command you publish must fit in 8192 bytes. An oversized one - realistically a large notification - is dropped silently; see [Limits](../reference/limits.md).

## Read the answer

Every command that **matches a route** is answered on that command's topic with `/result` appended, non-retained:

```
awtrixNG/cmd/settings       ->  awtrixNG/cmd/settings/result
awtrixNG/cmd/apps/pushed/x  ->  awtrixNG/cmd/apps/pushed/x/result
```

Success is exactly:

```json
{"ok":true}
```

Failure is the HTTP error body wrapped in `ok:false`:

```json
{"ok":false,"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

The codes are the same ones HTTP returns for the same mistake, listed with the messages they carry over MQTT in [Errors → Errors over MQTT](../reference/errors.md#errors-over-mqtt).

**A topic that matches no route is answered with silence:** no error, no acknowledgement, nothing. `awtrixNG/cmd/notfiy` simply vanishes. So does `awtrixNG/cmd/indicators/9`, where the HTTP route would return a 404. If a command seems to do nothing, check the spelling before you check AWTRIX.

Commands that reboot AWTRIX - `cmd/device/reboot`, `cmd/settings/reset` - may never get their result out. Do not wait on it.

## Subscribe to state

AWTRIX pushes. These topics are **retained**, so a subscriber gets the current value the moment it connects, without asking:

| Topic | What |
|---|---|
| `<prefix>/state/device` | the `GET /api/v1/device` object |
| `<prefix>/state/settings` | the `GET /api/v1/settings` object |
| `<prefix>/state/apps/active` | the current app name, as a plain string |
| `<prefix>/state/capabilities` | available effects, transitions, overlays, palettes |

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/state/#' -v
```

`state/device` is the one most automations want - uptime, free heap, Wi-Fi RSSI, light level, battery, and the current app. Its fields are documented at [Device state](../reference/device.md); the full topic list, including the radio and screen topics and every retain flag, is [MQTT topics → State topics](../reference/mqtt.md#state-topics).

`state/settings` and `state/apps/active` are published the moment the value changes, so something you alter over HTTP or in the web UI shows up on MQTT immediately. `state/device` goes out every `statsInterval` milliseconds - 10 000 out of the box - and sooner when the panel power or an indicator changes.

Key your automations on these retained topics rather than on the fact that you published a command: a message lost in transit is lost silently, and the state topics are what AWTRIX is actually doing.

### Button presses

Buttons publish on the edge, retained (and re-sent on connect), as `"1"` / `"0"`:

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/state/buttons/+' -v
```

```
awtrixNG/state/buttons/select 1
awtrixNG/state/buttons/select 0
```

The three are `left`, `select` and `right`. These are the topics to trigger automations on directly. In Home Assistant the button `binary_sensor` entities track these edges too. See [Home Assistant](home-assistant.md).

### Is it alive?

AWTRIX registers a last will at `<prefix>/availability`, retained, and publishes `online` on connect. The broker publishes `offline` for it when it drops:

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/availability' -v
```

Turning on Home Assistant discovery does not move this topic: the discovery document points every entity at the same `<prefix>/availability`, so an automation keyed on it keeps working. See [Availability and LWT](../reference/mqtt.md#availability-and-lwt).

## Related

- [MQTT topics](../reference/mqtt.md) - every topic, payload and retain flag.
- [Home Assistant](home-assistant.md) - discovery, entities, and what the entity path does differently.
- [App & notification payload](../reference/payload.md) - the keys that go inside those payloads.
- [Errors](../reference/errors.md#errors-over-mqtt) - the same error body, over HTTP and MQTT.
