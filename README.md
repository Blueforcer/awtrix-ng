<div align="center">

<img src="docs/assets/hero.webp" alt="AWTRIX NG" width="800">

# AWTRIX NG

**Firmware for LED pixel clocks. Your clock, showing what matters to you.**

[![CI](https://github.com/Blueforcer/awtrix-ng/actions/workflows/ci.yml/badge.svg)](https://github.com/Blueforcer/awtrix-ng/actions/workflows/ci.yml)
[![Docs](https://github.com/Blueforcer/awtrix-ng/actions/workflows/docs.yml/badge.svg)](https://github.com/Blueforcer/awtrix-ng/actions/workflows/docs.yml)
![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3%20%7C%20TC002-blue)
[![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-lightgrey)](LICENSE.md)

**[Documentation](https://blueforcer.github.io/awtrix-ng/)** · **[AWTRIX Hub](https://awtrix.de)** ·
[App](#the-awtrix-ng-app) · [Release notes](https://blueforcer.github.io/awtrix-ng/releases/) ·
[Discord](https://discord.gg/5pbmeCrs3a)

</div>

---

AWTRIX NG turns an affordable LED pixel clock into a small display for your home. It shows the
time and date, the readings of its sensors, and whatever your smart home has to say: the washing
machine is done, someone is at the door, the train is late. Apps you write run on the clock
itself, so it keeps working without a server in the background.

It is the successor to AWTRIX 3, rewritten from scratch. It runs on the **Ulanzi TC001**, the
**Ulanzi TC002** and clocks you build yourself with an **ESP32** or **ESP32-S3**. Every clock runs
the same engine, with the same API, the same MQTT topics and the same scripts, so an automation
written for one clock works on the others.

## Which clock does what

| | ESP32 (Ulanzi TC001, AWTRIX 2, DIY) | ESP32-S3 (DIY) | Ulanzi TC002 |
|---|---|---|---|
| Display | 8 rows, 32 to 128 wide | 8 rows, 32 to 128 wide | built-in 52 × 16 |
| Sound | buzzer melodies, DFPlayer tracks | plus MP3s and internet radio with PSRAM and an I²S amplifier | speaker with MP3s, radio, synthesizer and offline speech |
| Controls | three buttons | three buttons | buttons and a knob |
| Bluetooth | – | – | two gamepads, iPhone notifications |
| Home Assistant | MQTT discovery | MQTT discovery | plus Home Assistant Voice on the knob |
| Scripts | on the clock | on the clock, with more memory | plus Bluetooth LE, TCP, hashing, sign-in to services and layouts |
| Pictures | icons on the clock and from the Hub | icons on the clock and from the Hub | plus GIF, JPEG and PNG from any web address |
| Art-Net | yes | yes | – |
| Install | in the browser | in the browser | USB installer for Windows, macOS and Linux |

## What it does

**Shows what you send it.** Home Assistant, Node-RED, n8n, a shell script or anything else that
speaks HTTP or MQTT can send a notification that appears at once, or a pushed app that takes its
turn in the rotation with the clock, the date and the rest.

**Runs apps on the clock.** Scripts are small programs in [Berry](https://berry-lang.github.io/),
written and saved in the web UI. They fetch data from the internet, talk MQTT and Modbus, keep
their state across restarts and have settings of their own. A script with an error shows a red
frame, and everything else keeps running.

**Looks good doing it.** Colored and scrolling text, animated icons, charts and progress bars,
19 background effects, weather overlays, 22 transitions between apps, and palettes of your own.

**Plays sound.** Melodies on every clock, DFPlayer tracks, and on clocks with a speaker your own
MP3s and internet radio. One mixer keeps radio, apps and alerts at the volume you set.

**Fits into your smart home.** Home Assistant finds the clock through MQTT discovery and gets its
buttons, sensors, brightness and volume as entities. The buttons can also call webhooks.

**Belongs to you.** Everything is set up in the web UI on the clock: a live preview, editors for
scripts, icons and palettes, backups and every setting. No cloud and no account.

**And more.** Hold the middle button for a menu with your games, tools and radio stations. Let one
clock share its display with others. Ask an AI assistant to write a script for you with the
[prompt from the docs](https://blueforcer.github.io/awtrix-ng/guides/ai-prompt/).

## The Ulanzi TC002

The TC002 gets everything above and makes the most of its hardware. Its speaker plays MP3s,
internet radio and songs from a built-in synthesizer, and an offline voice reads notifications
aloud. Hold the knob and it becomes a Home Assistant Voice satellite. Two Bluetooth gamepads turn
it into a games console, your iPhone sends its notifications over Bluetooth, and album covers and
other pictures come straight from the web. Pushed apps written for 32 × 8 clocks are drawn at
double size, so your existing automations fill the larger display.

The USB installer sets it up without any cloud, and later updates come through the web UI. You
can always go back to the Ulanzi firmware.

## The AWTRIX Hub

The **[AWTRIX Hub](https://awtrix.de)** is the community gallery for AWTRIX: hundreds of
[scripts](https://awtrix.de/scripts) and [automations](https://awtrix.de/flows) for Home
Assistant, Node-RED, n8n and more, and thousands of [icons](https://awtrix.de/icons). Choose your
clock at the top and the Hub shows only what runs on it. **Send to AWTRIX** puts a script on your
clock together with everything it needs. The [pixel studio](https://awtrix.de/editor) draws icons
and animations in the browser, and anything you make can be shared back.

## The AWTRIX NG app

The **[AWTRIX NG app](https://blueforcer.github.io/awtrix-ng/guides/app/)** for
iPhone and
[Android](https://play.google.com/store/apps/details?id=de.awtrix.ng) finds the clocks in your
Wi-Fi, shows them live and brings the Hub to your phone. It sends Android notifications and your
photos to the clock, and on the TC002 your phone becomes a gamepad. It is a one-time purchase with
no subscription, no ads and no account, and every purchase supports the development of AWTRIX NG.
The iPhone app is in review at Apple and waits for its release.

## Get started

1. **Install it.** Flash a TC001 or a DIY ESP32 or ESP32-S3 clock
   [from the browser](https://blueforcer.github.io/awtrix-ng/getting-started/flashing/). For the
   TC002, [download the USB installer](https://blueforcer.github.io/awtrix-ng/tc002/getting-started/tc002/#get-the-installer).
2. **Connect it to your Wi-Fi.** Enter your network in the TC002 installer, or join the clock's
   setup hotspot. [Wi-Fi setup](https://blueforcer.github.io/awtrix-ng/getting-started/first-boot/)
   walks you through it.
3. **Open the web UI.** The clock shows its address after it starts. Its name is `awtrixng-`
   followed by the last six characters of its MAC address, for example `http://awtrixng-a1b2c3.local`.
4. **Add something.** Install a script from the [AWTRIX Hub](https://awtrix.de), send your first
   notification, or write an app of your own.

> **Coming from AWTRIX 3?** AWTRIX NG has its own API, so integrations for AWTRIX 3 need an update.
> [Migrating from AWTRIX 3](https://blueforcer.github.io/awtrix-ng/guides/migrating-from-awtrix3/)
> shows what changed and converts many of them for you.

> **Updating from AWTRIX NG 1.1.2?** Read the breaking changes for
> [ESP32](https://blueforcer.github.io/awtrix-ng/esp32/releases/1.2.2/#breaking-changes) or
> [ESP32-S3](https://blueforcer.github.io/awtrix-ng/esp32-s3/releases/1.2.2/#breaking-changes)
> first. Volumes, sound keys and some script calls changed.

## A first taste

Send a notification from any computer in your network:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"Hello world","textColor":"#FF0000"}'
```

**Hello world** scrolls across the display in red.

Or paste this script into the **Scripts** tab of the web UI. It joins the rotation like any other
app:

```berry
class Dashboard
  var level, fx, ramp

  def init()
    self.level = 0
    self.fx = {"speed": 0.3, "palette": "Ocean"}
    self.ramp = [0x00FFAA, 0x0066FF]
  end

  def loop()                     # about once a second, even while hidden
    self.level = (self.level + 7) % 101
  end

  def draw()                     # about 40 times a second while shown
    effect("PlasmaCloud", self.fx)
    progress(self.level, 0x00FF00, 0x101010)
    ramp_text(1, 6, "NET", self.ramp)
  end
end

return Dashboard()
```

The [scripting guide](https://blueforcer.github.io/awtrix-ng/guides/scripting/) starts from zero,
and the [documentation](https://blueforcer.github.io/awtrix-ng/) covers every endpoint, setting
and payload field.

## Build from source

```bash
pio run -e awtrix            # build the ESP32 firmware
pio run -e awtrix -t upload  # build and flash over USB
```

The [developer guide](docs/developers/building.md) covers the ESP32-S3 builds, the TC002 and the
tests.

## Contributing

Issues and pull requests are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md). Reports from
boards beyond the stock clocks help a lot, and so do scripts worth shipping as examples. Please
report security problems as described in [SECURITY.md](SECURITY.md), not in the issue tracker.

## Support

AWTRIX NG is free for noncommercial use. If you would like to support its development, get the
[AWTRIX NG app](https://blueforcer.github.io/awtrix-ng/guides/app/), or chip in through
[GitHub Sponsors](https://github.com/sponsors/Blueforcer), [Ko-fi](https://ko-fi.com/blueforcer)
or [PayPal](https://paypal.me/blueforcer). Thank you!

## License

**[PolyForm Noncommercial 1.0.0](LICENSE.md)** · © Stephan Mühl ([Blueforcer](https://github.com/Blueforcer))

Free to use, change and share for any noncommercial purpose: at home, in schools, in research,
for charities and public safety. The source is available, but it is not OSI open source. Selling
it or using it in a business needs a separate agreement. Third-party licenses are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
