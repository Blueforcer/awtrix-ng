# Home Assistant

Point AWTRIX at your MQTT broker, turn on one flag - and it appears
in Home Assistant as a single device with lights, selects, buttons and sensors
already wired up. No YAML, no manual entity definitions.

## What you need

- **Home Assistant 2024.11 or newer.** Discovery uses Home Assistant's *device
  discovery* format - one document describing the whole device. Older versions
  do not understand that topic and will show nothing.
- Home Assistant with the **MQTT integration** already set up and pointed at a
  broker (Mosquitto or similar).
- The broker's host and port, reachable from the Wi-Fi network AWTRIX is on.
- The IP address of your AWTRIX. If you do not have it, see
  [Finding AWTRIX](../getting-started/discovery.md).

Home Assistant does not need to be reachable from AWTRIX - everything goes
through the broker.

## Enable discovery

One call enables MQTT, sets the broker and turns discovery on:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,"haDiscovery":true}'
```

If your broker requires credentials, add them:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,
       "mqttUser":"awtrix","mqttPass":"secret","haDiscovery":true}'
```

`PUT /api/v1/system` takes a partial object - the keys you omit keep their
stored values. The `Content-Type` header is
[required on every write](../reference/conventions.md#content-type-is-mandatory).
The same fields are on the **MQTT** page of the web UI (*HA discovery* toggle,
*HA prefix* text field) if you would rather click, and every broker and
discovery field - with its default and whether it needs a reboot - is in
[System configuration → MQTT and Home Assistant](../reference/system.md#mqtt-and-home-assistant).

The broker connection is read once at boot, so reboot after setting it the
first time:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

`haDiscovery` itself needs no reboot. If AWTRIX is already connected to the
broker, switching it republishes or retracts the discovery document in the same
request; if it is disconnected, the change takes effect on the next connect.

Within a few seconds of AWTRIX reaching the broker, a device named after your
`hostname` (or **AWTRIX NG** if you never set one) shows up under
*Settings → Devices & Services → MQTT*.

## Verify it worked

The fastest check is the broker, not Home Assistant. Watch the discovery topic:

```bash
mosquitto_sub -h 192.168.1.10 -t 'homeassistant/device/+/config' -v
```

A single retained payload - the whole device, every entity - arrives on connect.
If nothing shows up, check that `mqttHost` is actually set and that AWTRIX
is on the broker at all: `<P>/availability` should read `online`.

`<P>` in the topics on this page is your `mqttPrefix`, or AWTRIX's own uid - the
12-character MAC - if you left the prefix empty.

## What lands in Home Assistant

The entity set is **capability-gated**: a base of **20 entities** every board
gets, plus entities that only appear when the hardware supports them. On a
**fully equipped board** - battery pin, light sensor and a temperature/humidity
sensor - you get **26 entities**.

| Component | Count | Entities |
|---|---|---|
| `light` | 4 | Matrix, Indicator 1–3 |
| `select` | 2 | Brightness mode, Transition effect |
| `button` | 3 | Dismiss notification, Next app, Previous app |
| `switch` | 1 | Transition |
| `sensor` | 7 (+6) | Current app, Version, IP address, MQTT prefix, WiFi strength, Uptime, Free RAM - plus **Light level**, **Temperature**, **Humidity**, **Pressure**, **Battery** and **Battery voltage** |
| `binary_sensor` | 3 (+1) | Button left, Button select, Button right - plus **Low battery** |

Which of the conditional ones appear depends on the board:

- **Light level** needs a light-sensor pin (`pinLdr`).
- **Temperature**, **Humidity** and **Pressure** are created per detected sensor
  capability - temperature on any sensor, humidity on a humidity-capable part,
  pressure only on a BME280 / BMP280. A sensorless board gets none of them.
- **Battery**, **Battery voltage** and **Low battery** need a battery pin
  (`pinBattery`).

Nothing is announced for hardware that is not there, so no entity sits at
`unknown` waiting for a value that never arrives.

Every entity points at the ordinary `<P>/cmd/...` and `<P>/state/...` topics, so
an automation you built against plain MQTT keeps working once you turn discovery
on. The full per-entity list, with the topic each one reads and writes, is
[Entity set](../reference/mqtt.md#entity-set).

## What each entity does

### Matrix (light)

The panel itself, with brightness and RGB.

| Control | Writes | Effect |
|---|---|---|
| State | `power` | Turns the panel on and off. |
| Brightness | `brightness` | 0–255. |
| RGB | `textColor` | The **global text color** - the colour your apps draw text in, not a panel tint, so changing it recolours the clock rather than tinting the background. |

!!! warning "Brightness does nothing while auto-brightness is on"
    The panel keeps following the light sensor, and the slider does not stay
    where you left it: it reads back the brightness actually in force, so it
    drifts to whatever the light sensor is driving. Set **Brightness mode** to
    `Manual` first if you want the slider to control and reflect the panel.

See [Brightness](brightness.md) for how the auto-brightness curve behaves.

### Indicator 1 / 2 / 3 (light)

Three RGB-only lights driving the small pixel clusters on the panel's right edge
- corner wedges for *Indicator 1* (top) and *3* (bottom), a short mid-height bar
for *Indicator 2*. Turning one on from the toggle sends white; use the colour
picker for any other colour.

`blinkMs` and `fadeMs` are not exposed as entities - they are reachable only via
the [`indicators/<id>` command topic](../reference/mqtt.md#command-topics).

### Brightness mode (select)

`Manual` or `Auto`, backing the `autoBrightness` setting. Set it to `Manual`
before expecting the Matrix brightness slider to do anything.

### Transition effect (select)

The 22 transition **names** - `Random`, `Slide`, `Ripple` and the rest, the same
values the REST and MQTT APIs take, not numeric indices - backing the
`transitionEffect` setting. What each one looks like:
[Visual reference - Transitions](../reference/visuals.md#transitions).

### Transition (switch)

Backs `autoTransition` - whether AWTRIX animates between apps at all.

### Buttons (button)

Three stateless buttons. Pressing one dispatches the command immediately:

| Entity | Does |
|---|---|
| Dismiss notification | Clears the notification currently on screen. |
| Next app | Advances the app loop. |
| Previous app | Steps back. |

### Sensors

Read-only values. Everything backed by `<P>/state/device` refreshes on the
**`statsInterval` timer** (default 10 s); *Current app* and the settings-backed
entities update the moment the value changes. Matrix power and the indicator
lights are republished as soon as they change too, so they do not lag the panel.

| Entity | Unit | Notes |
|---|---|---|
| Current app | - | Updates on change. |
| Version | - | The AWTRIX version. |
| IP address | - | |
| MQTT prefix | - | The `<P>` every topic below starts with. Published once per connect. |
| WiFi strength | `dBm` | RSSI. |
| Uptime | `s` | Seconds since the last boot. |
| Free RAM | `B` | |
| Light level | `%` | A percentage (0–100). Light-sensor boards only. |
| Temperature | `°C` | Only when a sensor is detected. |
| Humidity | `%` | Only on a humidity-capable sensor. |
| Pressure | `hPa` | Only on a BME280 / BMP280 barometer. |
| Battery | `%` | Battery boards only. |
| Battery voltage | `V` | Battery boards only. |

### Low battery (binary_sensor)

On a battery board, `ON` once the charge drops below `lowBatteryThreshold`
(a percentage; `0` disables the check). It carries `device_class: battery`, so
Home Assistant shows it as a standard low-battery indicator.

### Button left / select / right (binary_sensor)

Each tracks its physical button: `ON` while the button is held, `OFF` on
release. They update on real button edges, so they work directly as automation
triggers, and they are retained - after a Home Assistant restart they show the
current level rather than `unknown`.

## Sending notifications from Home Assistant

The entity set has no notification entity - notifications go over the command
topics, which are live whether or not discovery is on. The payload is
byte-identical to the one the HTTP API takes:

```yaml
script:
  doorbell:
    sequence:
      - service: mqtt.publish
        data:
          topic: "awtrixNG/cmd/notify"
          payload: '{"text":"Someone is at the door","textColor":"#00FF00","durationMs":8000}'
```

Replace `awtrixNG` with your own prefix. Every command topic, its payload and
its `/result` reply: [Command topics](../reference/mqtt.md#command-topics). The
payload keys themselves:
[App & notification payload](../reference/payload.md).

### From a picked device to its topic

A blueprint that lets the user pick a device rather than type a prefix reads the
**MQTT prefix** sensor, which carries exactly the `<P>` that device answers on:

```jinja
{% set e = device_entities(device_id) | select('search', 'mqtt_prefix') | list %}
{{ states(e[0]) if e else 'unknown' }}
```

`device_id` comes from a `device` selector filtered to
`integration: mqtt`, `manufacturer: Blueforcer`. The match is on the entity id,
so it breaks if someone renames that entity - guard against `unknown` and
`unavailable` before publishing.

## Triggering on button presses

The three button `binary_sensor` entities work as triggers. If you would rather
key on the topic directly, button edges are published - retained, as the raw
string `1` on press and `0` on release - to `<P>/state/buttons/left`, `/select`
and `/right`:

```yaml
automation:
  - alias: "Panel left button pressed"
    trigger:
      - platform: mqtt
        topic: "awtrixNG/state/buttons/left"
        payload: "1"
    action:
      - service: light.toggle
        target:
          entity_id: light.desk_lamp
```

Watch them to confirm:

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/state/buttons/+' -v
```

Details: [state topics](../reference/mqtt.md#state-topics).

## Availability

AWTRIX publishes `<P>/availability` (`online` / `offline`, retained, a real
last will) whether or not discovery is on, and the discovery document points
every entity at that same topic. An automation keyed on `<P>/availability`
therefore keeps working after you enable `haDiscovery`.

Inside Home Assistant availability is handled for you: entities go *unavailable*
on their own when AWTRIX drops.

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/availability' -v
```

See [Availability and LWT](../reference/mqtt.md#availability-and-lwt).

## Changing the discovery prefix

Only needed if your Home Assistant uses a non-default discovery prefix:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"haPrefix":"ha-discovery"}'
```

If AWTRIX is currently connected, this takes effect immediately: the
document under the previous prefix is retracted and the new one published in
the same request. If it is disconnected, the change takes effect the next
time it reaches the broker.

## Turning discovery off

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"haDiscovery":false}'
```

AWTRIX publishes an **empty retained payload** to
`<haPrefix>/device/<uid>/config`, which tells Home Assistant to drop the device
- immediately if it is connected, otherwise the next time it reaches the broker.
Nothing else changes: `<P>/availability` and the `<P>/cmd/...` and
`<P>/state/...` topics behave exactly as before.

To disable MQTT altogether, flip the gate off and reboot:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":false}'
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

This flag is only read at boot, so AWTRIX keeps talking to the broker until
you restart it. The host, username, password, `mqttPort`, `mqttPrefix` and
`haDiscovery` all survive, so re-enabling later needs no retyping.

## Related

- [MQTT topics](../reference/mqtt.md) - every topic and entity, and what a reply
  looks like.
- [System configuration](../reference/system.md#mqtt-and-home-assistant) - every
  broker and discovery field.
- [Device state](../reference/device.md) - what `<P>/state/device` contains.
