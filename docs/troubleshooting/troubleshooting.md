# Troubleshooting

Find the symptom you see, read the cause, then follow the fix. The entries are grouped by what is
going wrong.

---

## Setup & network

### A pulsing dot sits in the corner of the display {#a-pulsing-dot-sits-in-the-corner-of-the-panel}

A single pixel fades in and out at the left edge of the display, over whatever app is showing.

**Cause.** A connection AWTRIX should have is missing. The color tells you which one:

- **Red, top-left corner**: AWTRIX is not on your Wi-Fi network.
- **Yellow, bottom-left corner**: AWTRIX is on the network but cannot reach your MQTT broker.
  See [MQTT never connects](#mqtt-never-connects).

Only one dot lights at a time: without Wi-Fi there is no MQTT either, so a network outage shows
the red dot alone. You cannot switch the dots off. They go away by themselves once the connection
is back.

**Fix for a red dot.** AWTRIX keeps trying on its own, so a dot that clears after a minute needs
nothing from you. If it stays:

1. Check that your router is on and the network reaches the place where the clock stands.
2. Move the clock closer to the router. A weak signal is the usual cause of a dot that comes and
   goes.
3. If you changed your Wi-Fi password, give AWTRIX the new one. See
   [I need the setup hotspot back](#i-need-the-access-point-back-new-router-new-wi-fi-password).

Once AWTRIX is back online, `GET /api/v1/device` shows what happened. In the `wifi` and `mqtt`
objects, `lastError` keeps the last reason and `connects` counts how often the connection came
back. `"state": "connected"` with `"lastError": "lost"` and `connects` at 12 means the clock has
been dropping off your network all day: a signal or router problem, not a faulty clock. See
[Connection status](../reference/device.md#connection-status).

If a red dot shows and you can still open the web UI, you are connected to the clock's own setup
hotspot, not to your network. See [Connect to Wi-Fi](../getting-started/first-boot.md).

### AWTRIX doesn't show up on the network {#finding-awtrix-on-the-network}

`<hostname>.local` does not open and no discovery tool finds the clock.

**Cause.** The clock never joined your Wi-Fi, your network blocks `.local` names, or it blocks
discovery.

**Fix. Check in this order:**

1. **It never joined your Wi-Fi.** `AP MODE` on the display means it opened its setup hotspot.
   Follow [Connect to Wi-Fi](../getting-started/first-boot.md).
2. **`.local` names don't work on your network.** mDNS (the feature that lets you open
   `http://awtrix.local` instead of an IP address) is blocked by some routers, VLANs and Android
   versions. Use the IP address instead: the display shows it each time the clock starts<!-- only tc002 -->,
   and the Status app shows it at any time<!-- /only -->. Your router's list of connected devices shows it under its hostname:
   `awtrixng-<last 6 hex digits of the MAC>` by default, for example `awtrixng-a1b2c3`.
3. **Discovery is blocked.** AWTRIX answers the text `FIND_AWTRIXNG` sent as a broadcast to UDP
   port **4210**, and replies to port **4211** on your computer. Your tool must listen on UDP 4211 even when it sends from another port. "AP isolation" (client isolation) on the router blocks
   this. Turn it off, or put AWTRIX and your computer on the same subnet.

The full procedure, with working examples: [Find your clock](../getting-started/discovery.md).

### The setup hotspot never appears {#the-provisioning-access-point-never-appears}

A new or reset AWTRIX, or one that lost its network, shows no Wi-Fi network to join.

**Cause.** The hotspot opens only after the clock gave up joining your network, and its name is
easy to miss.

**Fix:**

1. Wait about 15 seconds after power-on. The hotspot opens only after the join attempt times out.<!-- only esp32 esp32-s3 -->
   You can change this time with `wifiConnectTimeout`.<!-- /only -->
2. Look for the **hostname**, not for "AWTRIX": `awtrixng-<last 6 hex digits of the MAC>` by
   default, for example `awtrixng-a1b2c3`, or the hostname you set.
3. Join it. The network is **open**: there is no password.
4. The setup page should open by itself. If it doesn't, browse to `192.168.4.1`.

<!-- only esp32 esp32-s3 -->Setup always runs on port 80, whatever `webPort` says. <!-- /only -->If you switched on
login (`authEnabled`), it applies here too. See [Connect to Wi-Fi](../getting-started/first-boot.md).

### I need the setup hotspot back: new router, new Wi-Fi password {#i-need-the-access-point-back-new-router-new-wi-fi-password}

AWTRIX is set up for a network that is gone or has a new password, so it cannot join and you cannot reach it.

**Cause.** The stored Wi-Fi name or password is wrong.

**Fix:**

<!-- only tc002 -->
1. Wait until the connection attempt fails and the setup hotspot opens.
2. Join the hotspot and enter the new network name and password. You do not need USB.

Nothing is erased.
<!-- /only -->
<!-- only esp32 esp32-s3 -->
1. Hold the **select** (middle) button while you power AWTRIX on, and keep holding for a
   second. The display shows `SETUP`.
2. Join the open network named after the hostname.
3. Save the new network name and password.

Nothing is erased. A normal restart goes back to the stored network, so pressing the button by
accident only costs a restart.
<!-- /only -->
To erase everything, use `POST /api/v1/device/factory-reset` once the clock is back on your
network. Setup step by step: [Connect to Wi-Fi](../getting-started/first-boot.md).

### AWTRIX hangs or restarts after you install a script {#scripts-eat-the-memory-and-awtrix-never-comes-up}

After you install or edit a script, the display stays on the boot logo, the clock restarts by itself,
or the web UI times out.

**Cause.** All scripts share the clock's memory. One script that needs too much, or too many
scripts at once, leave nothing for the rest.

<!-- only esp32 esp32-s3 -->
**Fix:**

1. Hold **left and right together** while you switch AWTRIX on, and keep holding for three
   seconds. The display shows `NOSCR` and AWTRIX starts without running any script.
2. Open the web UI. The **Scripts** tab still works: you can open, edit, save and delete scripts.
3. Fix or delete the script that caused the problem.
4. Switch **System → Run scripts** back on and restart. Your change takes effect on that start.

This is the same switch as **System → Run scripts**, and it stays off until you turn it back on.
Nothing is deleted. Holding the buttons for less than three seconds does nothing.
<!-- /only -->
<!-- only tc002 -->
**Fix:** a script cannot take the memory the rest of the clock needs, so the web UI usually stays
reachable:

1. Open the web UI. In the **Scripts** tab, fix or delete the script that caused the problem.
2. If that is not enough, switch off **System → Run scripts** and restart the clock. Your scripts
   stay installed and editable. Switch it back on once the script is fixed.

If the web UI does not open at all, the only way back is the reset button. It erases all settings,
Wi-Fi and uploaded files. See
[When the clock does not start](../guides/reset-recovery.md#when-awtrix-ng-cannot-start).
<!-- /only -->

<!-- only esp32 esp32-s3 -->
### AWTRIX sits on the weakest access point {#awtrix-sits-on-the-weakest-access-point}

Several access points share one network name (a router plus repeaters, or a mesh) and the signal
is poor even next to the nearest one.

**Cause.** AWTRIX joins the strongest access point it sees **at boot** and stays with it as long
as the link holds, even after you move it.

**Fix:**

1. Read `wifiRssi` from `GET /api/v1/device`. That is your normal signal level.
2. Set a roaming threshold below it. −75 dBm is a good start:

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/system \
      -H "Content-Type: application/json" -d '{"wifiRoamRssi":-75}'
    ```

The signal has to stay below the threshold for about 30 seconds before AWTRIX looks for a better
access point, and then it waits five minutes before it tries again. **Roaming is a reconnect**: the
connection drops for a second or two each time, and MQTT with it. If you set the threshold too
close to your normal signal, the clock reconnects again and again. `0` turns roaming off and is the
default. See [System configuration → Wi-Fi](../reference/system.md#wi-fi).

### Discovery reports the wrong port {#discovery-reports-the-wrong-port}

The `.local` name resolves, but the API does not answer on the port you expect.

**Cause.** AWTRIX has not been restarted since you changed `webPort`.

**Fix.** Restart AWTRIX. mDNS and UDP discovery both announce `webPort` (discovery appends
`:port` to its reply), and both use 80 when `webPort` is `0`. After a restart the two always
agree.
<!-- /only -->

---

## Display {#panel-display}

### Nothing appears on the display {#nothing-appears-on-the-panel}

A command is accepted with `200`, but the display does not change.

**Cause.** Something else is drawing over your apps, or your app is not in the rotation.

**Fix. Check in this order.** <!-- only esp32 esp32-s3 -->Four<!-- /only --><!-- only tc002 -->Three<!-- /only --> things beat the rotation. The first one that is active wins:

1. **The display is off.** `GET /api/v1/display` reports `power`. Send `{"power":true}` to
   `PATCH /api/v1/display`.
2. **The mood light is on.** It fills the display with one color. Turn it off with
   `DELETE /api/v1/display/moodlight`.
<!-- only esp32 esp32-s3 -->
3. **The clock is in setup mode.** The display shows a rainbow `AP MODE` and nothing else. See
   [Connect to Wi-Fi](../getting-started/first-boot.md).
4. **An Art-Net stream is running.** Incoming frames replace the rotation until they stop. See
   [Art-Net](../guides/artnet.md).
<!-- /only -->
<!-- only tc002 -->
3. **The clock is in setup mode.** The display shows `AP MODE` with the hotspot name and address,
   and nothing else. See [Connect to Wi-Fi](../getting-started/first-boot.md).
<!-- /only -->

If none of these apply, check the app itself. `GET /api/v1/apps` lists every app: `inLoop: false`
means your app order leaves it out, and a script that failed shows the error that stopped it. See
[Arrange the rotation](../guides/pushed-apps.md#reordering-switching-off-and-duplicating).

<!-- only esp32 esp32-s3 -->
### My fixed brightness has no effect {#my-fixed-brightness-has-no-effect}

You `PATCH` `brightness` in `/api/v1/settings` and the display does not follow it.

**Cause.** Auto-brightness is on. The display follows the light sensor between `minBrightness` and
`maxBrightness` and ignores the fixed `brightness` value.

**Fix.** Turn auto-brightness off in the same request:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness":false,"brightness":120}'
```

See [Brightness & sensors](../guides/brightness.md).

### Auto-brightness is backwards: a bright room dims the display {#auto-brightness-is-backwards-a-bright-room-dims-the-panel}

With auto-brightness on, the display gets *dim* in bright light and *bright* in the dark.

**Cause.** Your light sensor is wired against ground, so it reads high in the dark.

**Fix.**

1. Set `ldrOnGround` to invert the reading:

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/system \
      -H "Content-Type: application/json" -d '{"ldrOnGround":true}'
    ```

2. If the range is wrong rather than inverted, check the two limits. They default to `10` and
   `220`:

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/system \
      -H "Content-Type: application/json" \
      -d '{"minBrightness":10,"maxBrightness":220}'
    ```

See [Brightness & sensors](../guides/brightness.md).

<!-- /only -->
---

## Time

### The clock shows the wrong time, or sits at 00:00 {#the-clock-shows-the-wrong-time-or-sits-at-0000}

There are three different symptoms, each with its own cause.

**Stuck at 00:00 on 1 January.**
Cause: AWTRIX has never reached a time server (NTP). This normally clears within seconds of
joining the Wi-Fi. If it stays, something blocks it: a router that blocks outgoing UDP port 123,
or a network with its own time server.
Fix: set `ntpServer` to the time server of your network:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"ntpServer":"192.168.1.1"}'
```

**Off by whole hours.**
Cause: the time zone (`tz`) is wrong for your location.
Fix: pick your city again under **System → Time** in the web UI. The picker sets a rule that already
includes the daylight-saving dates. A `tz` string you write by hand over the API is **not
checked**, so a malformed one gives you a wrong offset instead of an error.

**Off by a few minutes.**
Cause: the clock never reached a time server and runs on its own.
Fix: make sure the time server is reachable. There is no manual offset.

`tz` and `ntpServer` take effect at once, without a restart. See
[System configuration → Time](../reference/system.md#time).

---

## Sound

### No sound at all {#no-sound-at-all}

Notifications<!-- only esp32 --> or melodies<!-- /only --><!-- only esp32-s3 tc002 -->, melodies or MP3s<!-- /only --> play silently.

**Cause.** The master volume or the volume of the sound's group is `0`. What you hear is the
master volume times the group's share, so either one at `0` is silence.

**Fix:**

1. Open the **Audio** tab and look at the **Mixer** section. **Master** must be above 0.
2. Check the slider of the group you use: **Alerts** for notifications and
   `/api/v1/audio/play`, **Apps** for scripts<!-- only esp32-s3 tc002 -->, **Radio** for the radio<!-- /only -->.

Or set both with one request:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" -d '{"volume":60,"alertVolume":100}'
```

<!-- only tc002 -->
[The knob](../guides/device-controls.md#the-knob) sets the master volume. An X next to the
speaker means it is at 0.
<!-- /only -->

A script's sound is also not played while an alert plays. See
[What plays over what](../guides/sounds.md#what-plays-over-what).

### A sound request answers 503 {#a-sound-request-answers-503}

A request to play a melody, MP3, track or song answers `503 unavailable`.

**Cause.** Your clock cannot play that kind of sound<!-- only esp32 esp32-s3 -->, for example a melody on a board without a buzzer<!-- /only -->.
For a list of sounds, it can play none of the entries.

**Fix.** Check which outputs your clock has under `audio` in `GET /api/v1/capabilities`, and use
one of those. See [Sound](../guides/sounds.md).

### A sound request answers 404 {#a-sound-request-answers-404}

**Cause.** No melody or MP3 with that name is stored on the clock. The message is
`nothing called "x"`, or `no file "Racer/x"` for a script's sound.

**Fix.** Check the spelling, or upload the file first. The **Audio** tab of the web UI lists what
is stored. See [Sound](../guides/sounds.md).

---

## MQTT & Home Assistant

### MQTT never connects {#mqtt-never-connects}

No state topics arrive and Home Assistant stays empty, but AWTRIX is online and the web UI works.

**Cause.** MQTT is switched off, or AWTRIX cannot reach the broker.

**Fix. Check in this order:**

1. **MQTT is switched off (`mqttEnabled` is `false`).** Switch it on together with a broker
   address, then restart, because the broker connection is read once at boot:

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/system \
      -H "Content-Type: application/json" \
      -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883}'
    curl -X POST http://<awtrix-ip>/api/v1/device/reboot
    ```

2. **The broker is not reachable.** The web UI shows the reason in the **Connection** line under
   **System → MQTT**. The API reports the same:

    ```bash
    curl -s http://<awtrix-ip>/api/v1/device | jq .mqtt
    ```

    ```json
    {"enabled": true, "state": "offline", "host": "broker.local", "endpoint": "",
     "attempts": 4, "retryInMs": 40000, "connects": 0, "error": "hostNotFound"}
    ```

    The two errors you are most likely to see:

    - **`hostNotFound`**: the broker name could not be found. A `.local` name only works if
      something on the same network answers for it. If not, enter the broker's IP address and
      restart.
    - **`refused`**: the address was found, but nothing accepted the connection. Check the port,
      and that the broker is reachable from the network AWTRIX is on.

    Every error and what to do about it:
    [Device state → What each error means](../reference/device.md#what-each-error-means).

`GET /api/v1/logs` shows the same failures with the broker's own status code, which helps when you
compare with your broker's log. Full setup: [MQTT](../guides/mqtt.md).

### Home Assistant shows no AWTRIX device {#home-assistant-shows-no-awtrix-device}

MQTT works, but no AWTRIX device appears in Home Assistant.

**Cause.** Discovery is off, or your Home Assistant is older than 2024.11 and does not understand
the discovery format AWTRIX uses.

**Fix:**

1. Switch discovery on: `{"haDiscovery":true}` on `PUT /api/v1/system`, or the *HA discovery*
   toggle under **System → MQTT** in the web UI.
2. Update Home Assistant to 2024.11 or newer.
3. If your Home Assistant uses a different discovery prefix, set the same one in `haPrefix`.

See [Home Assistant](../guides/home-assistant.md).

### Home Assistant shows fewer entities than expected {#home-assistant-shows-fewer-entities-than-expected}

**Cause.** Entities follow the hardware. Temperature, humidity and pressure appear only for what
your sensor actually measures, the battery entities only when <!-- only esp32 esp32-s3 -->`pinBattery` is set<!-- /only --><!-- only tc002 -->a battery is fitted<!-- /only -->.

**Fix.** Nothing to fix. `GET /api/v1/device` shows the same: whatever is missing there is also
missing in Home Assistant. Which entity needs which part:
[Home Assistant](../guides/home-assistant.md#what-lands-in-home-assistant).

---

## API requests

### The Content-Type trap {#the-content-type-trap-read-this-first}

A `PUT` or `PATCH` answers `415` although the JSON you sent is valid:

```json
{"error":{"code":"unsupportedMediaType","message":"expected application/json"}}
```

**Cause.** The request carried a `Content-Type` header other than `application/json`. `curl -d`
sends a form header unless you set another one, and so do many HTTP clients. AWTRIX refuses a
`PUT` or `PATCH` with any other type before it reads the body. A request without a `Content-Type`
header is read as JSON, and a `POST` is not checked. Script source uploads take the script text
as it is.

**Fix.** Always send the JSON header:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"22°C"}'
```

- `curl --json '...'` (curl 7.82 and later) sets the header for you.
- In a browser, `fetch` needs `headers: {"Content-Type": "application/json"}`.
- A Home Assistant `rest_command` needs `content_type: "application/json"`.

The rules for every route: [Content-Type](../reference/conventions.md#content-type-is-mandatory).

### Updating a pushed app returns 422 {#updating-a-pushed-app-returns-422}

`PUT /api/v1/apps/pushed/{name}` answers `422` with
`body required`.

**Cause.** The body arrived empty, or you sent `{}`. Neither is a valid update, and neither
deletes the app.

**Fix.** Send a non-empty JSON object with `Content-Type: application/json`. A `PUT` replaces the
app, it does not merge. To change one field, send the full object you want stored:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/clock \
  -H "Content-Type: application/json" \
  -d '{"text":"hi","textColor":"#00FF00"}'
```

To remove an app, use `DELETE /api/v1/apps/{name}`. Leave `pushed` out of the path. A `DELETE` on
the `pushed` path answers `405 allowed: PUT`.

An empty body never means "clear". Removing an app, turning the mood light off and clearing an
indicator each have their own `DELETE` route. What each route answers for an empty or `{}` body
is listed in [the empty-body trap](../reference/errors.md#content-type-the-empty-body-trap).

### My notification returns `400 invalidJson` but my JSON is valid {#my-notification-request-returns-400-invalidjson-but-my-json-is-valid}

`POST /api/v1/notifications` answers
`400 {"error":{"code":"invalidJson","message":"invalid JSON"}}`, although your
JSON is fine.

**Cause.** What arrived is not the JSON you typed. On Windows, the shell changes the JSON before
`curl` sees it: the command prompt and Windows PowerShell both drop the `"` inside it, and the
command prompt also keeps the `'` around it. An empty body gets the same `400`, here and on
`PATCH /api/v1/settings` and `PATCH /api/v1/display`.

**Fix.** On Windows, save the JSON in a file and send the file with `-d @notification.json`, or use
a shell that keeps the quotes, such as Git Bash or WSL. Elsewhere, check that your tool sends the
body at all.

A body over <!-- only esp32 esp32-s3 -->8192 bytes<!-- /only --><!-- only tc002 -->2 MiB<!-- /only --> is a different error: `413 payloadTooLarge`. If you send a
large inline icon, that is the limit you hit.

### The mood light won't turn on {#the-mood-light-wont-turn-on}

`PUT /api/v1/display/moodlight` answers `422` with `body required`, and the display does not switch
to the mood light.

**Cause.** The body arrived empty, or you sent `{}`. Neither is accepted here. Turning the mood
light off is `DELETE /api/v1/display/moodlight`.

**Fix.** Send the mood-light object with the JSON header:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/display/moodlight \
  -H "Content-Type: application/json" \
  -d '{"brightness":120,"color":"#FF8800"}'
```

### Switching on MQTT or login returns 422 {#enabling-mqtt-or-login-was-rejected-with-422}

`PUT /api/v1/system` answers `422 validationFailed` when you set `mqttEnabled` or `authEnabled` to
`true`, and nothing is saved.

**Cause.** A required field is missing. `mqttEnabled` needs a non-empty `mqttHost`. `authEnabled`
needs a non-empty `authUser` **and** `authPass`. The `field` in the error names the missing key.

**Fix.** Send the required fields in the same request:

```bash
# switch login on
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"authEnabled":true,"authUser":"admin","authPass":"s3cret"}'

# switch MQTT on
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883}'
```

To switch either off, send only its switch. The host and credentials stay stored, so you do not
have to type them again later:

```bash
# once login is on, this request needs the current credentials too
curl -u admin:s3cret -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"authEnabled":false}'
```

Login is off by default, so a new AWTRIX is open to everyone on your network. Once login is on, it
also applies to the setup hotspot. See [Authentication](../reference/http.md#authentication) and
[MQTT](../guides/mqtt.md).

### A system setting I changed does nothing {#a-system-field-i-set-does-nothing}

You changed a field with `PUT /api/v1/system`, got `200`, and nothing changed.

**Cause.** The field needs a restart<!-- only esp32 esp32-s3 --> or the key was misspelled<!-- /only --><!-- only tc002 -->, the key was misspelled, or the field does not apply to this clock<!-- /only -->.

**Fix:**

1. **Restart.** Most system fields are read once at boot. The "Reboot" column in
   [System configuration](../reference/system.md) shows which. Send
   `POST /api/v1/device/reboot` and check again.
2. **Check the spelling.** `PUT /api/v1/system` does not reject unknown keys: a typo is accepted
   with `200` and thrown away. The answer contains the full configuration: a key you sent that is
   missing there does not exist.
<!-- only tc002 -->
3. **The field does nothing on this clock.** `webPort` and `artnet` are accepted but not used.
   The web server always uses port 80.
<!-- /only -->

### Reboot, sleep and reset stop answering afterwards {#reboot-sleep-and-reset-stop-answering-afterwards}

`POST /api/v1/device/reboot`, `/api/v1/device/sleep`, `/api/v1/device/factory-reset` or
`/api/v1/settings/reset` answers, and then every following request fails until AWTRIX is back.

**Cause.** This is expected. These routes answer `200 {"ok":true}` first and then act at once, so
AWTRIX is off the network for a few seconds after a restart.<!-- only esp32 esp32-s3 --> During sleep it stays offline
until the sleep time is over.<!-- /only -->

**Fix.** Wait, then ask `GET /api/v1/device` until it answers. After a reboot that takes a couple
of seconds. Do not send the command again because the *next* request failed: it already
happened.

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot   # -> {"ok":true}
sleep 5
curl http://<awtrix-ip>/api/v1/device                  # back up
```

If one of these routes gives you no answer at all, the request never reached AWTRIX. Check the
address. Wrong or missing login details give you `401` with an `unauthorized` error instead. Check
`authEnabled` and your username and password.

<!-- only esp32 esp32-s3 -->
- **Sleep** needs a positive whole number `durationMs` and wakes up after that time.
<!-- /only -->
<!-- only tc002 -->
- **Sleep** has no timed wake-up: the request stops the clock's software. To turn the display
  off, use [display power](../guides/power.md#turn-the-matrix-off-and-on).
<!-- /only -->
- **Factory reset** works over HTTP only, not over MQTT.

## Related

* [FAQ](faq.md): the questions new owners ask first
* [Errors](../reference/errors.md): every error code, status code and validation message
* [Find your clock](../getting-started/discovery.md): by name, by discovery, or from the display
* [Connect to Wi-Fi](../getting-started/first-boot.md): joining AWTRIX to your Wi-Fi
