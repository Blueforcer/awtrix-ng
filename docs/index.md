![AWTRIX NG](assets/hero.webp)

**256-1024 pixels on your desk, and you decide what they say.**  
Push text and numbers from Home Assistant, a shell script, anything that speaks HTTP or MQTT.
Or write the app yourself, directly on AWTRIX, and let it run on the clock.

AWTRIX NG is a complete new development - firmware written from scratch, the successor to the well-known AWTRIX 3.

* :material-github: **[Source code](https://github.com/Blueforcer/awtrix-ng)** - the firmware, the web UI and these pages
* :material-sitemap: **[AWTRIX Flows](https://flows.blueforcer.de/)** - user-made scripts and automations for your AWTRIX
* :material-forum: **[Discord](https://discord.gg/5pbmeCrs3a)** - ask, report, show what you built
* :material-heart: **[Support](https://ko-fi.com/blueforcer)** - support the development

## Two ways to make it yours

### 1 · Push to it from outside

Home Assistant, a shell script, Node-RED, or your own code - if it speaks HTTP or MQTT, it can
drive AWTRIX with a single request. There are two things you can send it.

A **[pushed app](guides/pushed-apps.md)** is content that stays. You `PUT` it under a name of your
choosing, it takes a slot in the app rotation, and you send the same request again whenever the
number changes:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"21.5C","icon":"2422","textColor":"#00AAFF"}'
```

A **[notification](guides/notifications.md)** is a one-shot interruption. You `POST` it, it paints
over whatever the rotation was showing, and when its time is up it is gone:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"Hello world","textColor":"#FF0000"}'
```

Both take the same payload keys - text, colors, icons, charts, effects
([App & notification payload](reference/payload.md)). The difference is lifetime: an app is a place
in the rotation you own and refresh, a notification is a single message that comes and goes.

### 2 · Run your app on AWTRIX

Write it in **[Berry](https://berry-lang.github.io/)** - a small, Python-shaped language - paste
it into the web UI's editor, and it joins the app rotation. It is stored on AWTRIX and keeps
running even when nothing else on your network is:

```berry
class Hello
  def draw()
    text(1, 6, "HELLO", 0x00FFAA)
  end
end
return Hello()
```

## What it does

| | |
|---|---|
| **Apps** | Five built-in apps (time, date, temperature, humidity, battery), plus your own - [pushed apps](guides/pushed-apps.md) fed from outside and [scripts](guides/scripting.md) running on AWTRIX - all in one rotation. |
| **Notifications** | Interrupt the rotation with a one-shot message. See [Your first notification](guides/notifications.md). |
| **Visuals** | Colored text fragments, scrolling, JPEG and base64 icons, bar/line/progress charts, draw primitives, background effects and weather overlays. See [Visual reference](reference/visuals.md). |
| **Sound** | RTTTL melodies on the buzzer, your own MP3s on a speaker, numbered tracks on a DFPlayer - each with its own volume. See [the sound guide](guides/sounds.md). |
| **Sensors** | Auto-detected I²C temperature/humidity sensors, LDR auto-brightness, battery monitoring. See [Brightness & sensors](guides/brightness.md). |
| **Integrations** | HTTP, MQTT (with Home Assistant auto-discovery), Art-Net, mDNS + UDP discovery. |

## Supported hardware

One firmware image per chip, not per board: **commercial 32×8 clocks, AWTRIX 2 mainboard
conversions and DIY panels** all flash the same file. The GPIO map is a setting, so you set your
pins in the web UI under **System → GPIO**.

| | |
|---|---|
| **Panel** | WS2812-style, 8 pixels high, **32 to 128 pixels wide** - one panel or several chained, described by panel width and panel count |
| **Chips** | ESP32 and ESP32-S3 |
| **Default pin map (ESP32)** | matrix 32 · buttons 26/27/14 · battery 34 · LDR 35 · buzzer 15 · I²C 21/22 |

Which pins are usable and how to describe your panel are in [GPIO & boards](reference/gpio.md).

If you are building your own, an **ESP32-S3 with PSRAM** has the most room: dozens of
[scripts](guides/scripting.md) instead of a careful handful, and it is the only board that can play
[internet radio](guides/radio.md). Everything else works the same on a plain ESP32.

## Driving the API

Building an integration? A handful of rules - camelCase keys, millisecond durations, the color
forms, the mandatory `Content-Type`, the shape of an error - hold across every route and payload.
They are stated once in **[Conventions](reference/conventions.md)** and assumed everywhere else.

From there, the [HTTP API](reference/http.md) and [MQTT topics](reference/mqtt.md) document every
route and topic, and [Limits](reference/limits.md) collects every cap AWTRIX enforces - body
sizes, app and notification counts, script memory - with what it answers when you reach one.


## Start here

<div class="grid cards" markdown>

* :material-flash: **[Flashing](getting-started/flashing.md)** - get AWTRIX onto an ESP32
* :material-power-plug: **[First boot](getting-started/first-boot.md)** - join it to your Wi-Fi
* :material-magnify: **[Finding AWTRIX](getting-started/discovery.md)** - mDNS and UDP discovery
* :material-monitor-dashboard: **[Web UI tour](getting-started/web-ui.md)** - the control panel built into AWTRIX
* :material-bell: **[Your first notification](guides/notifications.md)** - put a message on the panel
* :material-view-carousel: **[Pushed apps](guides/pushed-apps.md)** - your own slot in the rotation
* :material-script-text: **[Your first script](guides/scripting.md)** - run an app on AWTRIX

</div>

## License

AWTRIX NG is licensed under the
[PolyForm Noncommercial License 1.0.0](https://github.com/Blueforcer/awtrix-ng/blob/main/LICENSE.md):
free for any noncommercial purpose, including schools, public research and government, but not
for commercial use without a separate agreement.
Copyright © Stephan Mühl (Blueforcer).
