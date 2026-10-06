# Home Assistant

This page lets you control AWTRIX from Home Assistant. Once you connect AWTRIX to your MQTT broker
and switch on discovery, it appears as one device with lights, selects, buttons and sensors. You
do not write any YAML.

## How it behaves

AWTRIX never talks to Home Assistant directly. Everything goes through your MQTT broker. With
discovery on, AWTRIX announces itself each time it connects to the broker, as one device whose
entities match its hardware. Every entity reads and writes the ordinary MQTT topics of AWTRIX, so
automations you built on plain MQTT keep working. AWTRIX reads the broker settings once, at
start-up, so a change to them needs a restart. Switching discovery on or off works at once.

## What you need

- **Home Assistant 2024.11 or newer.** Older versions do not understand the discovery format
  AWTRIX uses and show nothing.
- The **MQTT integration** set up in Home Assistant and connected to a broker (Mosquitto or
  similar).
- The broker's address and port, reachable from the Wi-Fi network AWTRIX is on.
- The IP address of your AWTRIX. If you do not have it, see
  [Find your clock](../getting-started/discovery.md).

## Enable discovery

**In the web UI:**

1. Open the **System** tab and go to the **MQTT** section.
2. Switch on **Enable MQTT** and enter the broker's address and port. Add a username and password
   if your broker needs them.
3. Switch on **HA discovery**. Leave **HA prefix** empty unless your Home Assistant uses a
   different discovery prefix.
4. Save, then press **Reboot now** in the reminder. The broker connection is read once at
   start-up.

**Over the API**, one call switches MQTT on, sets the broker and turns discovery on:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,"haDiscovery":true}'
```

If your broker needs a login, add it:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,
       "mqttUser":"awtrix","mqttPass":"secret","haDiscovery":true}'
```

Then restart, because the broker connection is read once at start-up:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

`PUT /api/v1/system` changes only the keys you send. Everything else stays as it is. Always send
`Content-Type: application/json`. Without it, `curl -d` marks the body as a form, and AWTRIX
refuses the `PUT` ([Content-Type](../reference/conventions.md#content-type-is-mandatory)). Every
broker and discovery field, with its default and whether it needs a restart, is in
[System configuration → MQTT and Home Assistant](../reference/system.md#mqtt-and-home-assistant).

A few seconds after AWTRIX reaches the broker, a device named after your `hostname` (or
**AWTRIX NG** if you never set one) appears under *Settings → Devices & Services → MQTT*.

Switching `haDiscovery` on or off later needs no restart. If AWTRIX is connected, the change
takes effect at once. Otherwise it takes effect the next time AWTRIX connects.

## Verify it worked

The fastest check is on the broker. Watch the discovery topic:

```bash
mosquitto_sub -h 192.168.1.10 -t 'homeassistant/device/+/config' -v
```

One retained message arrives when AWTRIX connects. It describes the whole device with every
entity. If nothing shows up, check that `mqttHost` is set and that AWTRIX is connected:
`<P>/availability` should read `online`.

`<P>` in the topics on this page is your `mqttPrefix`. If you left it empty, it is AWTRIX's own
uid, the 12-character MAC address.

## What lands in Home Assistant

<!-- only esp32 esp32-s3 -->
Every board gets **19 entities**. More appear only when the hardware is there. A board with a
battery pin, a light sensor and a temperature/humidity sensor gets **26 entities**. A BME280 or
BMP280 adds **Pressure** on top. A board that can make sound adds four more<!-- only esp32-s3 -->, and a fifth when it
plays radio<!-- /only -->.

| Component | Count | Entities |
|---|---|---|
| `light` | 4 | Matrix, Indicator 1–3 |
| `select` | 1 (+1) | Transition effect, plus **Brightness mode** |
| `button` | 3 (+1) | Dismiss notification, Next app, Previous app, plus **Stop sound** |
<!-- only esp32 -->
| `number` | 0 (+3) | **Volume**, **App volume**, **Alert volume** |
<!-- /only -->
<!-- only esp32-s3 -->
| `number` | 0 (+4) | **Volume**, **Radio volume**, **App volume**, **Alert volume** |
<!-- /only -->
| `switch` | 1 | Transition |
| `sensor` | 7 (+6) | Current app, Version, IP address, MQTT prefix, WiFi strength, Uptime, Free RAM, plus **Light level**, **Temperature**, **Humidity**, **Pressure**, **Battery** and **Battery voltage** |
| `binary_sensor` | 3 (+1) | Button left, Button select, Button right, plus **Low battery** |

Which of the conditional ones appear depends on the board:

- **Brightness mode** and **Light level** need a light-sensor pin (`pinLdr`).
- **Temperature**, **Humidity** and **Pressure** appear for what the detected sensor measures:
  temperature on any sensor, humidity on a humidity-capable part, pressure only on a BME280 or
  BMP280. A board without a sensor gets none of them.
- **Battery**, **Battery voltage** and **Low battery** need a battery pin
  (`pinBattery`).
<!-- only esp32 -->
- **Volume**, **App volume**, **Alert volume** and **Stop sound** need a sound output: a buzzer
  or a DFPlayer.
<!-- /only -->
<!-- only esp32-s3 -->
- **Volume**, **App volume**, **Alert volume** and **Stop sound** need a sound output: a buzzer,
  a speaker or a DFPlayer. **Radio volume** also needs internet radio.
<!-- /only -->
<!-- /only -->
<!-- only tc002 -->
Your clock gets **28 entities**, and three more when it reports a battery.

| Component | Count | Entities |
|---|---|---|
| `light` | 4 | Matrix, Indicator 1–3 |
| `select` | 1 | Transition effect |
| `button` | 5 | Dismiss notification, Next app, Previous app, Stop sound, Assist |
| `number` | 4 | Volume, Radio volume, App volume, Alert volume |
| `switch` | 1 | Transition |
| `sensor` | 7 (+2) | Current app, Version, IP address, MQTT prefix, WiFi strength, Uptime, Free RAM, plus **Battery** and **Battery voltage** |
| `binary_sensor` | 5 (+1) | Button left, Button select, Button right, Knob, Charging, plus **Low battery** |
| `event` | 1 | Knob turn |

**Battery**, **Battery voltage** and **Low battery** appear when the clock reports a battery.
<!-- /only -->

Nothing is announced for hardware that is not there, so no entity sits at
`unknown` waiting for a value that never arrives.

Every entity points at the ordinary `<P>/cmd/...` and `<P>/state/...` topics, so
an automation you built against plain MQTT keeps working once you turn discovery
on. The full per-entity list, with the topic each one reads and writes, is
[Entity set](../reference/mqtt.md#entity-set).

## What each entity does

### Matrix (light)

The display itself, with brightness and RGB.

| Control | Writes | Effect |
|---|---|---|
| State | `power` | Turns the display on and off. |
| Brightness | `brightness` | 0–255. |
| RGB | `textColor` | The **global text color**: the color apps draw their text in. It does not tint the display. |

<!-- only esp32 esp32-s3 -->
While auto-brightness is on, the display follows the light sensor. See
[Brightness mode](#brightness-mode-select) and [Brightness & sensors](brightness.md).
<!-- /only -->

### Indicator 1 / 2 / 3 (light)

Three RGB-only lights for the small pixel groups on the display's right edge: a corner for
*Indicator 1* (top) and *Indicator 3* (bottom), a short bar in the middle for *Indicator 2*.
Turning one on with the toggle makes it white. Use the color picker for any other color.

Switching an indicator or changing its color in Home Assistant gives a steady light. `blinkMs` and
`fadeMs` have no entity. Set them with the
[`indicators/<id>` command topic](../reference/mqtt.md#command-topics).

<!-- only esp32 esp32-s3 -->
### Brightness mode (select)

`Manual` or `Auto`. It sets `autoBrightness`. Set it to `Manual` before you expect the Matrix
brightness slider to do anything. It appears only on a board with a light sensor. Without one, the
slider always sets the display.
<!-- /only -->

### Transition effect (select)

The 22 transition **names**, for example `Random`, `Slide` and `Ripple`. It sets
`transitionEffect` and takes the same names as the HTTP and MQTT APIs. What each one looks like:
[Visual reference: Transitions](../reference/visuals.md#transitions).

### Transition (switch)

It sets `autoTransition`. On, the apps take turns by themselves. Off, the rotation stops and the
app changes only when you press a button or send a command.

### Volume, Radio volume, App volume, Alert volume (number)

<!-- only esp32 -->
Three sliders from 0 to 100 %, in steps of 5. They set the [mixer](sounds.md#volume) of the clock:
<!-- /only -->
<!-- only esp32-s3 tc002 -->
Four sliders from 0 to 100 %, in steps of 5. They set the [mixer](sounds.md#volume) of the clock:
<!-- /only -->

| Entity | Sets | Volume of |
|---|---|---|
<!-- only esp32 -->
| Volume | `volume` | the whole clock. The two below are shares of it |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| Volume | `volume` | the whole clock. The three below are shares of it |
<!-- /only -->
<!-- only esp32-s3 -->
| Radio volume | `radioVolume` | internet radio. Only on a clock that plays radio |
<!-- /only -->
<!-- only tc002 -->
| Radio volume | `radioVolume` | internet radio |
<!-- /only -->
| App volume | `appVolume` | what scripts play |
| Alert volume | `alertVolume` | notification sounds and other alerts |

A change from the web UI<!-- only tc002 --> or the knob<!-- /only --> shows here at once.

### Buttons (button)

Pressing one acts at once:

| Entity | Does |
|---|---|
| Dismiss notification | Clears the notification currently shown. |
| Next app | Advances the rotation. |
| Previous app | Steps back. |
<!-- only esp32 -->
| Stop sound | Stops every sound. Only on a clock that can make sound. |
<!-- /only -->
<!-- only esp32-s3 -->
| Stop sound | Stops every sound, the radio included. Only on a clock that can make sound. |
<!-- /only -->
<!-- only tc002 -->
| Stop sound | Stops every sound, the radio included. |
| Assist | Starts [Home Assistant Voice](voice.md#start-it-from-home-assistant), as if you held the knob. |
<!-- /only -->

### Sensors

Read-only values. Everything that comes from `<P>/state/device` refreshes every `statsInterval`,
10 seconds by default. *Current app* and the entities that show a setting update the moment the
value changes. Matrix power and the indicator lights are published again as soon as they change
too, so they do not lag behind the display.

| Entity | Unit | Notes |
|---|---|---|
| Current app | - | Updates on change. |
| Version | - | The AWTRIX version. |
| IP address | - | |
| MQTT prefix | - | The `<P>` every topic below starts with. Published once per connect. |
| WiFi strength | `dBm` | The signal strength. |
| Uptime | `s` | Seconds since the last start. |
| Free RAM | `B` | |
<!-- only esp32 esp32-s3 -->
| Light level | `%` | A percentage (0–100). Light-sensor boards only. |
| Temperature | `°C` | Only when a sensor is detected. |
| Humidity | `%` | Only on a humidity-capable sensor. |
| Pressure | `hPa` | Only on a BME280 or BMP280. |
<!-- /only -->
| Battery | `%` | Battery boards only. |
| Battery voltage | `V` | Battery boards only. |

### Low battery (binary_sensor)

On a battery board, `ON` once the charge drops below `lowBatteryThreshold`, a percentage. `0`
switches the check off. It carries `device_class: battery`, so Home Assistant shows it as a
standard low-battery indicator.

### Button left / select / right (binary_sensor)

Each follows its physical button: `ON` while the button is held, `OFF` on release. They change
with every real press and release, so they work directly as automation triggers. They are not
retained. AWTRIX sends the current state on every connect.
<!-- only tc002 -->

### Charging (binary_sensor)

`ON` while the clock is on USB power and charges its battery, `OFF` on battery. It carries
`device_class: battery_charging`, so Home Assistant shows it as *Charging* / *Not charging*.
It changes right away when you plug the cable in or out.

### Knob (binary_sensor) and Knob turn (event)

**Knob** is `ON` while the knob is pressed and `OFF` on release. **Knob turn** fires an event
for every turn: `clockwise` or `counterclockwise`, with the number of steps in `steps`. Both work
as automation triggers.
<!-- /only -->

## Sending notifications from Home Assistant

There is no notification entity. Send notifications to the command topics instead. They work
whether or not discovery is on, and take exactly the same JSON as the HTTP API:

```yaml
script:
  doorbell:
    sequence:
      - action: mqtt.publish
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

`device_id` comes from a `device` selector filtered to `integration: mqtt` and
`manufacturer: Blueforcer`. The match is on the entity ID, so it breaks if someone renames that
entity. Check for `unknown` and `unavailable` before you publish.

## Switching an app on or off

Publish `true` or `false` to `<P>/cmd/apps/<name>/enabled`. Only that app changes. Every other app
stays on or off as it is. This automation lets a toggle helper switch one app:

<!-- only esp32 esp32-s3 -->
```yaml
automation:
  - alias: Date app follows its toggle
    triggers:
      - trigger: state
        entity_id: input_boolean.awtrix_date
        to: ["on", "off"]
    actions:
      - action: mqtt.publish
        data:
          topic: "awtrixNG/cmd/apps/Date/enabled"
          payload: "{{ 'true' if trigger.to_state.state == 'on' else 'false' }}"
```
<!-- /only -->
<!-- only tc002 -->
```yaml
automation:
  - alias: Status app follows its toggle
    triggers:
      - trigger: state
        entity_id: input_boolean.awtrix_status
        to: ["on", "off"]
    actions:
      - action: mqtt.publish
        data:
          topic: "awtrixNG/cmd/apps/Status/enabled"
          payload: "{{ 'true' if trigger.to_state.state == 'on' else 'false' }}"
```
<!-- /only -->

Use this call, not `cmd/apps/order`, to switch single apps. The order call sets the complete list
of switched-off apps, so every app it does not name is switched on, scripts included. See
[apps/order](../reference/mqtt.md#appsorder).

<!-- only tc002 -->
## Showing album covers

The clock can show the cover of the song that a media player in Home Assistant is
playing. Home Assistant names the cover in the media player's `entity_picture` attribute. The clock
downloads it from there.

1. In Home Assistant, go to **Settings → Automations & scenes**, create a new automation and
   switch to **Edit in YAML**.
2. Paste this automation:

    ```yaml
    alias: "AWTRIX: now playing"
    triggers:
      - trigger: state
        entity_id: media_player.kitchen
    conditions:
      - condition: state
        entity_id: media_player.kitchen
        state: playing
    actions:
      - action: mqtt.publish
        data:
          topic: "awtrixNG/cmd/apps/pushed/music"
          payload: >-
            {% set p = state_attr('media_player.kitchen', 'entity_picture') %}
            {% set icon = '' if not p else (p if p.startswith('http') else 'http://192.168.1.5:8123' ~ p) %}
            {{ {"text": state_attr('media_player.kitchen', 'media_title') or '', "icon": icon} | to_json }}
    ```

3. Replace three values with your own:
    - `media_player.kitchen`: your media player, in all four places
    - `awtrixNG`: the MQTT prefix of your clock
    - `http://192.168.1.5:8123`: the address of your Home Assistant
4. Save the automation and play a song.

The clock now shows an app called `music` with the song title and the cover. Each time the song
changes, the cover changes too.

Why the Home Assistant address is needed: most media players give only the second half of the
cover's address, for example `/api/media_player_proxy/…`. The automation puts your Home Assistant
address in front of it. A media player that already gives a full address starting with `http` is
used as it is.

More about pictures from web addresses: [Icons → Pictures from the internet](icons.md#pictures-from-the-internet).
<!-- /only -->

## Triggering on button presses

The three button `binary_sensor` entities work as triggers. To use the topics directly instead:
each press is published as the plain string `1`, and each release as `0`, to
`<P>/state/buttons/left`, `/select`
<!-- only esp32 esp32-s3 -->and `/right`.<!-- /only --><!-- only tc002 -->, `/right` and `/knob`.<!-- /only -->
These messages are not retained.

```yaml
automation:
  - alias: "Panel left button pressed"
    trigger:
      - platform: mqtt
        topic: "awtrixNG/state/buttons/left"
        payload: "1"
    action:
      - action: light.toggle
        target:
          entity_id: light.desk_lamp
```

Watch them to confirm:

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/state/buttons/+' -v
```

Details: [state topics](../reference/mqtt.md#state-topics).

## Availability

AWTRIX publishes `<P>/availability` (`online` / `offline`, retained) whether or not discovery is
on. If AWTRIX drops off the network, the broker sets it to `offline` by itself. Every Home
Assistant entity uses this same topic. An automation keyed on `<P>/availability`
therefore keeps working after you enable `haDiscovery`.

Inside Home Assistant availability is handled for you: entities go *unavailable*
on their own when AWTRIX drops.

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/availability' -v
```

See [Availability and LWT](../reference/mqtt.md#availability-and-lwt).

## Changing the discovery prefix

Only needed if your Home Assistant uses a discovery prefix other than `homeassistant`:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"haPrefix":"ha-discovery"}'
```

If AWTRIX is connected, this takes effect at once: the device is removed under the old prefix and
announced under the new one. If it is not connected, the change takes effect the next time it
reaches the broker.

## Turning discovery off

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"haDiscovery":false}'
```

AWTRIX publishes an **empty retained payload** to
`<haPrefix>/device/<uid>/config`, which tells Home Assistant to remove the device. This happens at
once if AWTRIX is connected, otherwise the next time it reaches the broker.
Nothing else changes: `<P>/availability` and the `<P>/cmd/...` and
`<P>/state/...` topics behave exactly as before.

To switch MQTT off completely, set `mqttEnabled` to `false` and restart:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":false}'
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

AWTRIX reads `mqttEnabled` only at start-up, so it keeps talking to the broker until you restart
it. The host, username, password, `mqttPort`, `mqttPrefix` and `haDiscovery` all stay saved, so
you do not have to type them again when you switch MQTT back on.

## Good to know {#when-it-goes-wrong}

- **No device appears.** Check that Home Assistant is 2024.11 or newer, that `haDiscovery` is on,
  and that `haPrefix` matches your Home Assistant discovery prefix.
- **A yellow dot pulses on the display, or `<P>/availability` stays empty.** AWTRIX cannot reach
  the broker. See [MQTT never connects](../troubleshooting/troubleshooting.md#mqtt-never-connects).
- **A change to the broker settings does nothing.** AWTRIX reads them only at start-up: restart it.
- **Entities are missing.** They follow the hardware. See
  [What lands in Home Assistant](#what-lands-in-home-assistant).
<!-- only esp32 esp32-s3 -->
- **The brightness slider does not stay where you left it.** Auto-brightness is on, and the slider
  shows the brightness the light sensor sets. Set **Brightness mode** to `Manual` first.
<!-- /only -->

## Related

- [MQTT topics](../reference/mqtt.md): every topic and entity, and what a reply
  looks like.
- [System configuration](../reference/system.md#mqtt-and-home-assistant): every
  broker and discovery field.
- [Device state](../reference/device.md): what `<P>/state/device` contains.
<!-- only tc002 -->
- [Home Assistant Voice](voice.md): talk to Assist through your clock.
<!-- /only -->
