# FAQ

Short answers to the questions new users ask first. Each links into the fuller
docs.

## What is AWTRIX NG?

LED-matrix clock firmware for 32×8 WS2812 panels on ESP32. You flash it once,
then drive AWTRIX - clock, your own apps, notifications, effects, sounds - over
an HTTP and MQTT API. Start on the [Home page](../index.md).

## Which hardware does it run on?

Any ESP32 or ESP32-S3 board driving an 8 px high WS2812 panel. The shipped
defaults match the common 32×8 clock wiring; every pin is configurable - see
[GPIO & boards](../reference/gpio.md).

## How do I flash it?

Over USB. See [Flashing](../getting-started/flashing.md).

## How do I get it onto my Wi-Fi?

On first boot, and whenever it cannot reach a known network, AWTRIX opens its
own provisioning access point - open, no password - named after its hostname.
That is `awtrixng-` plus the last 6 hex digits of its MAC address by default,
e.g. `awtrixng-a1b2c3`. Join it, and the captive portal opens the Wi-Fi setup
page. Full walkthrough: [First boot](../getting-started/first-boot.md).

## How do I find the address of my AWTRIX?

By default it is reachable at **`http://awtrixng-xxxxxx.local`** (mDNS), where
`xxxxxx` is the last 6 hex digits of its MAC address. If `.local` doesn't
resolve on your network, use the IP address - it scrolls across the panel once
at boot, and it appears in your router's DHCP list. See
[Finding AWTRIX](../getting-started/discovery.md); if it won't show up at
all, see [Troubleshooting](troubleshooting.md#finding-awtrix-on-the-network).

## How do I send my first notification?

`POST` a JSON body to `/api/v1/notifications`, with an explicit
`Content-Type: application/json` header:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"hello","textColor":"#00FF00"}'
```

Walkthrough: [Your first notification](../guides/notifications.md).

## Why do I get `400 invalidJson` when my JSON is fine?

Because the body arrived empty. `curl -d` declares a form type unless you
override it, a form-encoded body never reaches the JSON parser, and an empty
body is not valid JSON. Send `Content-Type: application/json`. This is the
single most common pitfall: see
[the Content-Type trap](troubleshooting.md#the-content-type-trap-read-this-first).

## How do I change the brightness?

`PATCH` `/api/v1/settings` with `brightness` (0–255). If it seems to have no
effect, `autoBrightness` is on and the panel is following the light sensor
instead. See [Brightness & sensors](../guides/brightness.md) and the
[brightness troubleshooting entry](troubleshooting.md#my-fixed-brightness-has-no-effect).

## Why does a setting I changed do nothing?

Most `/api/v1/system` fields are read once at boot, so a change only takes effect
after `POST /api/v1/device/reboot`. The "reboot" column in
[System configuration](../reference/system.md) says which ones. A few settings are
also conditional - a fixed `brightness` is ignored while `autoBrightness` is on,
for instance.

## Something accepts my command but never appears on the panel

Check what is drawing over it: a powered-off matrix, the mood light,
provisioning mode and an active Art-Net stream all beat the app loop, in that
order. Then walk the [Troubleshooting](troubleshooting.md) symptoms.

## Why did my pushed app vanish when I updated it?

A `PUT` with an empty or `{}` body returns `422` - and a wrong `Content-Type`
makes your body arrive empty, so that is the usual cause. A `PUT` also replaces
the whole app rather than merging into it, so send the full object you want
stored. Removing an app is `DELETE`. Details:
[Troubleshooting](troubleshooting.md#updating-a-pushed-app-returns-422).
Lifecycle: [Pushed apps](../guides/pushed-apps.md).

## How do I connect it to MQTT or Home Assistant?

Set `mqttEnabled: true` and `mqttHost` in `/api/v1/system` and reboot - the broker
connection is read once at boot. `haDiscovery` needs no reboot: it is applied as
soon as it is saved. Which entities appear depends on your hardware. See the
[MQTT guide](../guides/mqtt.md) and the
[Home Assistant guide](../guides/home-assistant.md).

## Does the API need a username and password?

Not by default - authentication is **off** until you switch it on. Set
`authEnabled: true` together with `authUser` and `authPass` via `/api/v1/system`;
once on, it applies to every route, including provisioning (AP) mode. To turn it
back off, set `authEnabled: false` - the stored credentials are kept. See
[Troubleshooting](troubleshooting.md#enabling-mqtt-or-login-was-rejected-with-422).

## I rebooted AWTRIX over the API and got no response - did it fail?

No. It answers `200 {"ok":true}` first and reboots a fraction of a second later,
so the connection can drop before you finish reading the body even though the
command already succeeded. Sleep, factory reset and settings reset behave the
same way. Don't retry. See
[Troubleshooting](troubleshooting.md#reboot-sleep-and-reset-stop-answering-afterwards).

## How do I check the firmware version?

`GET /api/v1/version` returns `{"version":"..."}`; `GET /version` returns the same
string as plain text.

```bash
curl http://<awtrix-ip>/api/v1/version
```
