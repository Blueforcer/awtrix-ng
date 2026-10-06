# FAQ

Short answers to the first questions new users ask. Each answer links to the page with the details.
For problems and error messages, see [Troubleshooting](troubleshooting.md).

## What is AWTRIX NG?

<!-- only esp32 -->
Firmware for LED matrix clocks: the Ulanzi TC001 and clocks built on an ESP32 board with an LED
display.
<!-- /only -->
<!-- only esp32-s3 -->
Firmware for LED matrix clocks built on an ESP32-S3 board with an LED display.
<!-- /only -->
<!-- only tc002 -->
Firmware for the Ulanzi TC002 LED matrix clock.
<!-- /only -->
It shows the time, your own apps, notifications, effects and sounds. You control it from the web
UI, Home Assistant, or over HTTP and MQTT. Start on the [Home page](../index.md).

## Which hardware does it run on?

<!-- only esp32 -->
This documentation covers the **Ulanzi TC001**, the **AWTRIX 2** mainboard and **ESP32** boards
with a WS2812 display. Pins and display size are configurable within the
[display limits](../reference/limits.md#display). See [GPIO & boards](../reference/gpio.md).
<!-- /only -->
<!-- only esp32-s3 -->
This documentation covers **ESP32-S3** boards with a WS2812 display. Pins and display size are
configurable within the [display limits](../reference/limits.md#display). See
[GPIO & boards](../reference/gpio.md).
<!-- /only -->
<!-- only tc002 -->
This documentation covers the **Ulanzi TC002** with its fixed 52×16 display, knob and speaker. See
[download availability](../getting-started/tc002.md#get-the-installer).
<!-- /only -->

AWTRIX NG runs on other clocks too. Each has its own documentation:
[Choose your clock](site:).

## How do I install it?

<!-- only esp32 esp32-s3 -->
Follow [Install AWTRIX NG](../getting-started/flashing.md). You can install from your browser or
with `esptool`.
<!-- /only -->
<!-- only tc002 -->
Follow [Install AWTRIX NG](../getting-started/tc002.md). The installer can also work without
internet ([offline installation](../getting-started/tc002.md#offline-installation)) or without a
desktop ([terminal installation](../getting-started/tc002.md#terminal-installation)).
<!-- /only -->

## How do I connect it to my Wi-Fi?

Without Wi-Fi details, the clock opens its own setup hotspot with no password, named like
`awtrixng-a1b2c3`. Join it with your phone and the setup page opens. See
[Connect to Wi-Fi](../getting-started/first-boot.md).

<!-- only tc002 -->
You can also enter your Wi-Fi in the [installer](../getting-started/tc002.md).
<!-- /only -->

## How do I find its address?

The clock shows its IP address on the display each time it starts.<!-- only tc002 --> The Status app
shows it at any time.<!-- /only --> You can also try `http://awtrixng-xxxxxx.local`, where `xxxxxx`
is the last 6 characters of its MAC address, or look in your router's device list. See
[Find your clock](../getting-started/discovery.md).

## How do I send my first message?

Send a notification from a terminal:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"hello","textColor":"#00FF00"}'
```

The display shows `HELLO` in green, then the rotation goes on. Always include the
`Content-Type: application/json` header. Without it, `curl -d` marks the body as a form, and
AWTRIX refuses a `PUT` or `PATCH` with that
([Content-Type](../reference/conventions.md#content-type-is-mandatory)). See
[Your first notification](../guides/notifications.md).

## What is the difference between a notification and a pushed app?

A [notification](../guides/notifications.md) is a one-time message: it appears and disappears. A
[pushed app](../guides/pushed-apps.md) stays in the app rotation until you delete it, and you
update it whenever your value changes.

## Can I write my own apps?

Yes. Write a small app in Berry and paste it into the script editor in the web UI. Start
with the [Scripting guide](../guides/scripting/index.md) and
[Tutorial 1: Draw something](../tutorials/first-draw.md). Ready-made apps are on the
[AWTRIX Hub](https://awtrix.de).

## How do I change the brightness?

In the web UI, or with `PATCH /api/v1/settings` and `brightness` (0–255).<!-- only esp32 esp32-s3 -->
If automatic brightness is on, the clock follows its light sensor instead.<!-- /only --> See
[Brightness & sensors](../guides/brightness.md).

## How do I connect it to Home Assistant or MQTT?

Turn on MQTT, enter your broker's address and restart the clock. With Home Assistant discovery on,
the clock appears in Home Assistant by itself. See the
[Home Assistant](../guides/home-assistant.md) and [MQTT](../guides/mqtt.md) guides.

## Does it need a username and password?

No, login is off until you turn it on. Once on, it protects the web UI and the whole API.
See [Authentication](../reference/http.md#authentication).

## My tool cannot send `PATCH`, `PUT` or `DELETE`

Send a `POST` and put the real method in the `X-HTTP-Method-Override` header. This helps with
tools like the FRITZ!Box HTTP action. See [Method override](../reference/http.md#method-override).

## How do I update the firmware?

In the web UI, without a cable. See [Updating firmware](../guides/updating.md).

## How do I check the firmware version?

The web UI shows it<!-- only tc002 -->, and so does the display at startup<!-- /only -->. From a
terminal:

```bash
curl http://<awtrix-ip>/api/v1/version
```

<!-- only esp32 esp32-s3 -->
## I used AWTRIX 3 before. What changes?

See [Coming from AWTRIX 3](../guides/migrating-from-awtrix3.md).

<!-- /only -->
## Something does not work

See [Troubleshooting](troubleshooting.md), or ask on [Discord](https://discord.gg/5pbmeCrs3a).
