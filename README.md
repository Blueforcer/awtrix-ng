<div align="center">

<img src="docs/assets/hero.webp" alt="AWTRIX NG" width="800">

**256-1024 pixels on your desk, and you decide what they say.**

*The direct successor to the well-known AWTRIX 3 - rewritten from scratch.*

Push a number from Home Assistant, a shell script or anything that speaks HTTP and MQTT -
or skip the middleman entirely and **run your app on the device itself.**

[![CI](https://github.com/Blueforcer/awtrix-ng/actions/workflows/ci.yml/badge.svg)](https://github.com/Blueforcer/awtrix-ng/actions/workflows/ci.yml)
[![Docs](https://github.com/Blueforcer/awtrix-ng/actions/workflows/docs.yml/badge.svg)](https://github.com/Blueforcer/awtrix-ng/actions/workflows/docs.yml)
![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3-blue)
[![License](https://img.shields.io/badge/license-PolyForm%20Noncommercial%201.0.0-lightgrey)](LICENSE.md)

📖 **[Documentation](https://blueforcer.github.io/awtrix-ng/)** &nbsp;·&nbsp;
🚀 [Quick start](#-quick-start) &nbsp;·&nbsp;
⚡ [On-device scripting](#-run-your-app-on-the-device) &nbsp;·&nbsp;
🔌 [Integrations](#-control-it-from-anything)

</div>

---

AWTRIX NG turns an inexpensive LED-matrix clock into a small networked screen for your home. It
shows time, date, temperature, humidity and battery out of the box, and then does whatever you
tell it - scroll a message, draw a chart, play a melody, flash an alert when the doorbell rings.

### Tell it something

```bash
curl -X POST http://awtrix-ng.local/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"Hello world","textColor":"#FF0000"}'
```

**Hello world** scrolls across the panel in red. That is the whole learning curve. Anything that
speaks HTTP or MQTT can do the same - Home Assistant, Node-RED, a shell script, a cron job.

### Or let it think for itself

Paste a program into the web UI and it runs **on the clock**. No compiler, no re-flash, no server
on your LAN keeping it alive.

```berry
class Dashboard
  var level

  def loop()                     # keeps working, even off screen
    self.level = (self.level + 7) % 101
  end

  def draw()                     # ~40×/s while you are on screen
    effect("PlasmaCloud", {"speed": 0.3, "palette": "Ocean"})
    progress(self.level, 0x00FF00, 0x101010)
    ramp_text(1, 6, "NET", [0x00FFAA, 0x0066FF])
  end
end

return Dashboard()
```

Save it, and it joins the rotation like any other app. Scripts fetch over HTTP, talk MQTT, keep
state across reboots and paint with the firmware's full visual vocabulary. A crashing script goes
red and stays in its own frame - everything else keeps running.

**→ [Scripting guide](https://blueforcer.github.io/awtrix-ng/guides/scripting/)**

## ✨ What you get

- ⚡ **Apps that run on the device** - edited in the browser, surviving reboots
- 🎨 **Real graphics** - scrolling colored text, icons, charts, background effects, weather overlays
- 📥 **Push from anywhere** - screens into the rotation, or a one-shot alert that cuts in front
- 🏠 **Home Assistant** - MQTT auto-discovery, no YAML
- 🌡️ **Sensors** - auto-detected temperature/humidity, auto-brightness, battery
- 🔊 **Sound** - RTTTL melodies through the buzzer or a DFPlayer
- 🖥️ **Web UI on the device** - live matrix preview, script editor, every setting. No app, no cloud
- ⚙️ **One image, any board** - pins are settings, not a build flavour

## 🚀 Get going

1. **Flash** - open the [browser flasher](https://blueforcer.github.io/awtrix-ng/getting-started/flashing/) in Chrome, Edge or Opera. Nothing to install.
2. **Wi-Fi** - the device opens its own access point on first boot and the setup page pops up by itself.
3. **Say hello** - send the `curl` above to `awtrix-ng.local`, then open `http://awtrix-ng.local/` and look around.

> [!IMPORTANT]
> Always send `Content-Type: application/json` on requests with a body - `curl -d` defaults to form
> encoding, which arrives empty.

> **Coming from AWTRIX 3?** This is a from-scratch rewrite with its own **API v1**. Nothing carries
> over and v3 integrations must be reworked. Set the device up fresh.

## 🧩 Hardware & source

Any 32(-128)×8 WS2812-style panel: a commercial clock, an AWTRIX 2 conversion or your own build - same
image, pins set in the web UI. ESP32 and ESP32-S3.

```bash
pio run  -e awtrix        # build + flash
pio run  -e native_sim    # the whole firmware on your computer, no hardware needed
```

The simulator runs the real thing, script engine and editor included - the fastest way to try this
without buying anything.

**Everything else** - every endpoint, setting and payload field - lives in the
**[documentation](https://blueforcer.github.io/awtrix-ng/)**.

## 🤝 Contributing

Issues and PRs welcome - start with **[CONTRIBUTING.md](CONTRIBUTING.md)**. Reports from boards
beyond the stock clocks are especially valuable, as are scripts worth shipping as examples.
Security problems go to **[SECURITY.md](SECURITY.md)**, not the issue tracker.

## 📄 License

**[PolyForm Noncommercial 1.0.0](LICENSE.md)** · © Stephan Mühl ([Blueforcer](https://github.com/Blueforcer))

Free to use, modify and share for any noncommercial purpose - hobby, private, schools, research,
charities, public safety. Source-available, not OSI open source: selling it or running it as part
of a business needs a separate agreement. Third-party licenses are in
**[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)**.
