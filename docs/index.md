---
only: [root, esp32, esp32-s3, tc002]
hide:
  - toc
---

<!-- only root -->
<div class="home-hero" markdown>

<img src="assets/hero.webp" alt="" class="home-hero__img">

<div class="home-hero__text" markdown>
<!-- /only -->

# A pixel clock that shows what you want

AWTRIX NG is free firmware for LED matrix clocks: the time, your messages, your smart home and
small apps you write yourself.

<!-- only esp32 esp32-s3 -->
[Install AWTRIX NG](getting-started/flashing.md){ .md-button .md-button--primary }
<!-- /only -->
<!-- only tc002 -->
[Install AWTRIX NG](getting-started/tc002.md){ .md-button .md-button--primary }
<!-- /only -->

<!-- only root -->
</div>
</div>

## Choose your clock

Each clock has its own documentation. It shows only what AWTRIX NG can do on that clock.

<!-- clocks -->

No clock yet? [Get a clock](getting-started/buy.md) lists where to buy the TC001 and TC002.
<!-- /only -->

<div class="home-hubs" markdown>

<div class="home-hub" markdown>

<div class="home-hub__text" markdown>

## Ready-made on the AWTRIX Hub

Hundreds of apps and automations and thousands of icons from the AWTRIX community. Pick one,
click **Send to AWTRIX**, and it is on your clock.

[:material-store: Open the AWTRIX Hub](https://awtrix.de){ .md-button .md-button--primary }
<!-- only esp32 esp32-s3 tc002 -->
[How it works](guides/hub.md){ .md-button }
<!-- /only -->

</div>

<div class="home-hub__tiles">
<a class="home-hub__tile" href="https://awtrix.de/scripts" title="Games, clock faces, live data and animations that run on the clock."><strong>Scripts</strong><span>Games, clock faces, live data and animations that run on the clock.</span></a>
<a class="home-hub__tile" href="https://awtrix.de/flows" title="Automations for Home Assistant, Node-RED, n8n, Domoticz and FHEM."><strong>Flows</strong><span>Automations for Home Assistant, Node-RED, n8n, Domoticz and FHEM.</span></a>
<a class="home-hub__tile" href="https://awtrix.de/icons" title="Images and animations for your apps and notifications."><strong>Icons</strong><span>Images and animations for your apps and notifications.</span></a>
<a class="home-hub__tile" href="https://awtrix.de/editor" title="Draw your own icons and animations in the browser."><strong>Studio</strong><span>Draw your own icons and animations in the browser.</span></a>
</div>

</div>

<div class="home-hub" markdown>

<div class="home-hub__text" markdown>

## Your clock in your pocket

The AWTRIX NG app finds your clocks, shows live what they display and brings the Hub to your
phone.<!-- only tc002 --> On the TC002 your phone also becomes a gamepad.<!-- /only -->

*:material-apple: App Store*{ .md-button .md-button--disabled title="In review at Apple" }
[:material-google-play: Google Play](https://play.google.com/store/apps/details?id=de.awtrix.ng){ .md-button .md-button--primary }
[More about the app](guides/app.md){ .md-button }

The iPhone app is in review at Apple and waits for its release. The App Store button works once
Apple has approved it.

</div>

<div class="home-hub__tiles">
<a class="home-hub__tile" href="guides/app/#see-the-clock-live" title="See what your clock shows right now, pixel for pixel."><strong>Live view</strong><span>See what your clock shows right now, pixel for pixel.</span></a>
<a class="home-hub__tile" href="guides/app/#scripts-and-icons-from-the-hub" title="Send scripts to your clock with one tap, no address to type."><strong>Hub in your pocket</strong><span>Send scripts to your clock with one tap, no address to type.</span></a>
<a class="home-hub__tile" href="guides/app/#notifications-from-your-phone" title="The apps you choose show their notifications on your clock."><strong>Notifications</strong><span>The apps you choose show their notifications on your clock.</span></a>
<!-- only root tc002 -->
<a class="home-hub__tile" href="guides/app/#play-with-your-phone" title="Play the games on your TC002 with your phone, alone or with a friend."><strong>Gamepad</strong><span>Play the games on your TC002 with your phone, alone or with a friend.</span></a>
<!-- /only -->
<!-- only esp32 esp32-s3 -->
<a class="home-hub__tile" href="guides/app/#send-a-picture" title="Share a photo to your clock or save it as an icon."><strong>Pictures</strong><span>Share a photo to your clock or save it as an icon.</span></a>
<!-- /only -->
</div>

</div>

</div>
<!-- only root -->
<div class="awtrix-live" markdown>

<div class="awtrix-live__device">
  <canvas width="640" height="160" role="img" aria-label="Simulated 32 by 8 pixel AWTRIX display showing the clock, the weather and your message"></canvas>
</div>

<div class="awtrix-live__side" markdown>

### Try it: send a notification

Type a message and send it to the clock above. On your own clock, the command below does the
same.

<form class="awtrix-live__form" autocomplete="off">
  <input type="text" maxlength="40" value="Hello world" aria-label="Message">
  <fieldset aria-label="Text color">
    <label style="--c:#FFFFFF"><input type="radio" name="color" value="#FFFFFF"><span>White</span></label>
    <label style="--c:#FF3B30"><input type="radio" name="color" value="#FF3B30" checked><span>Red</span></label>
    <label style="--c:#FF9F0A"><input type="radio" name="color" value="#FF9F0A"><span>Orange</span></label>
    <label style="--c:#30D158"><input type="radio" name="color" value="#30D158"><span>Green</span></label>
    <label style="--c:#40C8FF"><input type="radio" name="color" value="#40C8FF"><span>Blue</span></label>
    <label style="--c:#BF5AF2"><input type="radio" name="color" value="#BF5AF2"><span>Purple</span></label>
  </fieldset>
  <button type="submit" class="md-button md-button--primary">Send to the clock</button>
</form>

<pre class="awtrix-live__curl"><code data-role="curl"></code></pre>

</div>
</div>
<!-- /only -->

<!-- only esp32 esp32-s3 tc002 -->
## Up and running in four steps

<!-- only esp32 esp32-s3 -->
1. **Install** the firmware: [Install AWTRIX NG](getting-started/flashing.md).
<!-- /only -->
<!-- only tc002 -->
1. **Install** the firmware: [Install AWTRIX NG](getting-started/tc002.md).
<!-- /only -->
2. **Connect to Wi-Fi** through the clock's setup hotspot: [Connect to Wi-Fi](getting-started/first-boot.md).
3. **Find your clock** and open its address in a browser: [Find your clock](getting-started/discovery.md).
4. **Look around** the built-in [web UI](getting-started/web-ui.md).

## Two ways to show your own content

<div class="grid home-ways" markdown>

<div markdown>

### Send it from outside

<div markdown>

Home Assistant, Node-RED, a shell script or your own code sends content over HTTP or MQTT.

- A **[notification](guides/notifications.md)** appears once, over the current app.
- A **[pushed app](guides/pushed-apps.md)** stays in the app rotation, and you update it whenever
  the value changes.

</div>

<!-- panel -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"21°C","icon":"sun","textColor":"#00AAFF"}'
```

`sun` is an icon from the [AWTRIX Hub](guides/icons.md#install-from-the-awtrix-hub).

</div>

<div markdown>

### Run your own app on the clock

<div markdown>

Write a small app in [Berry](https://berry-lang.github.io/) and paste it into the script editor.
It is stored on the clock and keeps running on its own.

</div>

<!-- panel -->
```berry
class Hello
  def draw()
    text(1, 6, "HELLO", 0x00FFAA)
  end
end
return Hello()
```

Start with the [Scripting guide](guides/scripting/index.md).

</div>

</div>

## Explore the documentation

<div class="grid cards" markdown>

<!-- only esp32 esp32-s3 -->
* :material-flash: **[Getting started](getting-started/flashing.md)** - install, connect to Wi-Fi, find your clock
<!-- /only -->
<!-- only tc002 -->
* :material-flash: **[Getting started](getting-started/tc002.md)** - install, connect to Wi-Fi, find your clock
<!-- /only -->
<!-- only esp32 -->
* :material-gesture-tap-button: **[Using your clock](guides/device-controls.md)** - buttons, brightness, sound, updates
<!-- /only -->
<!-- only esp32-s3 tc002 -->
* :material-gesture-tap-button: **[Using your clock](guides/device-controls.md)** - buttons, brightness, sound, radio, updates
<!-- /only -->
* :material-bell: **[Show your own content](guides/notifications.md)** - notifications, pushed apps, text, icons, effects
<!-- only esp32 esp32-s3 -->
* :material-home-automation: **[Smart home](guides/home-assistant.md)** - Home Assistant, MQTT, Art-Net
<!-- /only -->
<!-- only tc002 -->
* :material-home-automation: **[Smart home](guides/home-assistant.md)** - Home Assistant, voice, MQTT
<!-- /only -->
* :material-script-text: **[Write your own apps](guides/scripting/index.md)** - scripting guide, tutorials and recipes
* :material-store: **[AWTRIX Hub](guides/hub.md)** - ready-made scripts, flows and icons
<!-- only esp32 esp32-s3 -->
* :material-tools: **[Build your own clock](advanced/diy-build.md)** - DIY build, GPIO and boards
<!-- /only -->
* :material-book-open-variant: **[Reference](reference/conventions.md)** - HTTP API, MQTT topics, payloads, settings, limits
* :material-lifebuoy: **[Help](troubleshooting/troubleshooting.md)** - troubleshooting and [FAQ](troubleshooting/faq.md)

</div>

Another clock? [Choose your clock](site:) to switch to its documentation.

<!-- /only -->
## Community

<div class="home-community" markdown>

[:material-forum: Discord](https://discord.gg/5pbmeCrs3a){ .md-button }
[:material-github: Source code](https://github.com/Blueforcer/awtrix-ng){ .md-button }
[:material-heart: Support the project](https://ko-fi.com/blueforcer){ .md-button }

</div>

AWTRIX NG is the successor to AWTRIX 3, written from scratch.

<small>AWTRIX NG is licensed under the
[PolyForm Noncommercial License 1.0.0](https://github.com/Blueforcer/awtrix-ng/blob/main/LICENSE.md):
free for any noncommercial use, including schools, public research and government. Commercial use
needs a separate agreement. Copyright © Stephan Mühl (Blueforcer).</small>
