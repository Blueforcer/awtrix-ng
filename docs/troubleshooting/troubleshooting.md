# Troubleshooting

Each entry is a symptom, what to check, and what to do about it.

---

## The Content-Type trap (read this first)

**Symptom.** A write route answers with an error even though the JSON you sent is
valid.

A `Content-Type` header is not required, but if you send one on a `PUT` or
`PATCH` it must be `application/json`. A form-encoded body - curl's default when
you use `-d`, and the default for many HTTP clients and browser forms - is
refused before it is read:

```json
{"error":{"code":"unsupportedMediaType","message":"Content-Type must be application/json"}}
```

A `POST` is not checked this way. The form body simply never reaches the JSON
parser, so it arrives empty - and an empty body is not valid JSON, which is
`400 invalidJson`.

An empty body is never read as "clear". Removing an app, turning the mood light
off and clearing an indicator each have their own `DELETE` route, and the routes
that would otherwise be ambiguous answer `422` and name the one you want.

**Fix - send the header explicitly:**

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"22°C"}'
```

`curl --json '...'` (curl 7.82 and later) sets it for you. In browsers, `fetch`
must set `headers: {"Content-Type": "application/json"}`. The status code each
route answers for an empty or `{}` body is listed in
[the empty-body trap](../reference/errors.md#content-type-the-empty-body-trap).

---

## Nothing appears on the panel

**Symptom.** A command is accepted with `200`, but the panel does not change.

**Check what is drawing over it.** Four things beat the app loop, in this order -
the first one that is active wins:

1. **The panel is off.** `GET /api/v1/display` reports `power`. Turn it on by
   sending `{"power":true}` to `PATCH /api/v1/display`.
2. **The mood light is on.** It fills the panel with one colour. Turn it off with
   `DELETE /api/v1/display/moodlight`.
3. **AWTRIX is in provisioning mode.** The panel shows a rainbow `AP MODE` and
   draws nothing else - see [First boot](../getting-started/first-boot.md).
4. **An Art-Net stream is running.** Incoming frames replace the app loop until
   they stop - see [Art-Net](../guides/artnet.md).

If none of those apply, the app may not be in the rotation. `GET /api/v1/apps`
lists every app AWTRIX knows about: `inLoop: false` means your app order left it
out, and a script that failed carries the error that stopped it.

---

## A pulsing dot sits in the corner of the panel

**Symptom.** A single pixel breathes in and out at the far left of the panel, over
whatever app is showing.

That is AWTRIX telling you a connection it should have is missing. The colour says
which one:

- **Red, top-left corner** - AWTRIX is not on your WiFi network.
- **Yellow, bottom-left corner** - AWTRIX is on the network, but not talking to your
  MQTT broker. See [MQTT never connects](#mqtt-never-connects) below.

Only one dot lights at a time. Without WiFi there is no MQTT either, so a network
outage shows the red dot alone. The dots cannot be switched off, and they disappear on
their own the moment the connection is back.

**What to do about a red dot.** AWTRIX keeps trying by itself, so a dot that clears
after a minute needs nothing from you. If it stays:

1. Check your router is up and the network is reachable from where the device sits.
2. Move the device closer to the router, or the router closer to the device. Weak
   signal is the usual cause of a dot that comes and goes.
3. If you changed your WiFi password, AWTRIX cannot know that. Hold the **select**
   button (the middle one) while powering it on to get its setup screen back, then
   re-enter the network - see [First boot](../getting-started/first-boot.md).

While the WiFi is down, the dot is the only signal you get: with no network, nothing
can reach the device to ask it anything. Once AWTRIX is back on, `GET /api/v1/device`
tells you what happened. In the `wifi` and `mqtt` objects, `lastError` keeps the reason
across the recovery and `connects` counts how often the connection came back. A device
reporting `"state": "connected"` with `"lastError": "lost"` and a `connects` of 12 has
been dropping off your network all day - that is a signal or router problem, not a
device fault. See [Connection status](../reference/device.md#connection-status).

If a red dot is showing and you can still reach the web UI, you are connected to the
device's own setup network rather than to AWTRIX on yours - see
[First boot](../getting-started/first-boot.md).

---

## Finding AWTRIX on the network

### AWTRIX doesn't show up at all

**Symptom.** `<hostname>.local` doesn't resolve and no discovery tool finds it.

**Check, in order:**

1. **It never joined your Wi-Fi.** A rainbow `AP MODE` on the panel means AWTRIX
   fell back to its own access point. Join that network and configure Wi-Fi -
   see [First boot](../getting-started/first-boot.md).
2. **mDNS (`.local`) isn't working on your network.** Some routers, VLANs and
   Android versions don't forward it. Fall back to the raw IP address: AWTRIX
   scrolls it across the panel once at boot, and your router's DHCP client list
   shows it under its hostname - `awtrixng-<last 6 hex of the MAC>` by default,
   e.g. `awtrixng-a1b2c3`.
3. **UDP discovery is blocked.** AWTRIX answers the text `FIND_AWTRIXNG` sent as
   a broadcast to UDP port **4210**, and replies to port **4211** on your
   machine - not the port you sent from - so your client has to bind UDP 4211.
   Client isolation ("AP isolation") on the router blocks this; disable it, or
   put AWTRIX and your client on the same subnet.

The full procedure, with working snippets:
[Finding AWTRIX](../getting-started/discovery.md).

### The provisioning access point never appears

**Symptom.** A freshly flashed AWTRIX, or one that lost its network, shows no
Wi-Fi network to join.

**Check:**

- The access point opens only **after** the join attempt times out
  (`wifiConnectTimeout`, 15 s by default). Wait that long after power-on.
- The network name is the **hostname**, `awtrixng-<last 6 hex of the MAC>` by
  default - e.g. `awtrixng-a1b2c3`, or whatever hostname you set. It is easy to
  miss when you are scanning for something starting with `AWTRIX`.
- The network is **open** - there is no password.

Once you have joined, the captive portal should open by itself; if it doesn't,
browse to `192.168.4.1`. Provisioning is always served on port 80, whatever
`webPort` says, and if `authEnabled` is set the login applies here too. See
[First boot](../getting-started/first-boot.md).

### I need the access point back - new router, new Wi-Fi password

**Symptom.** AWTRIX is configured for a network that no longer exists, so it sits
in its access point or keeps failing to join, and the API is out of reach.

**Fix.** Hold the **select** button (the middle one) while powering AWTRIX on,
and keep holding for a second. The panel shows `SETUP` and it comes up in
provisioning mode whatever is stored. Join the open network named after the
hostname and save the new credentials.

Nothing is erased - an ordinary restart goes back to trying the stored
credentials, so triggering this by accident costs a reboot and no more. To wipe
everything, use `POST /api/v1/device/factory-reset` once you are back on the
network.

### Scripts eat the memory and AWTRIX never comes up

**Symptom.** After installing or editing a script, AWTRIX no longer comes up
properly: the panel stays on the boot logo, restarts by itself, or answers so
slowly that the web UI times out. All scripts share the device's memory, so one
that asks for too much - or simply too many at once - can leave nothing for the
rest, and there is no moment where you could switch it off.

**Fix.** Hold **left and right together** while switching AWTRIX on, and keep
holding for three seconds. The panel shows `NOSCR` and AWTRIX starts without
running any script, so the web UI answers again. This is the same switch as
**System → Run scripts**, and it stays off until you turn it back on.

Nothing is deleted, and the **Scripts tab still works**: your scripts are listed,
open in the editor, and can be saved or deleted as usual. Only nothing runs. Fix
or delete the script that caused it, switch **Run scripts** back on, and restart -
your edit takes effect on that start.

Holding the buttons on a normal start does nothing unless you hold both for the
full three seconds.

### AWTRIX sits on the weakest access point

**Symptom.** Several access points share one network name - a router plus
repeaters, or a mesh - and the reported signal is poor even next to the nearest
one.

**Check.** AWTRIX joins the strongest access point it can see **at boot**, and
stays associated for as long as the link holds. Moving it, or restarting the
nearest radio, leaves it on a distant one.

**Fix.** Enable roaming with a threshold below your normal signal level. Read
`wifiRssi` from `GET /api/v1/device` first and pick something under it; −75 dBm
is a reasonable start.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"wifiRoamRssi":-75}'
```

The link has to stay below the threshold for about 30 seconds before AWTRIX acts,
and it will not try again for five minutes. **Roaming is a reconnect, not a
handover** - the connection drops for a second or two each time, taking MQTT with
it. Set the threshold too close to your normal signal and it reconnects
repeatedly, which leaves you worse off than staying put. `0` turns it off, and is
the default.

### Discovery reports the wrong port

**Symptom.** The `.local` name resolves but the API doesn't answer on the port
you expect.

mDNS announces `webPort` and the UDP discovery reply appends `:port`; both fall
back to 80 when `webPort` is `0`. The two always agree, so a mismatch means
AWTRIX has not been rebooted since you changed the port.

---

## My fixed brightness has no effect

**Symptom.** You `PATCH` `brightness` in `/api/v1/settings` and the panel doesn't
follow it.

**Check.** `autoBrightness`. While it is on, the panel follows the light sensor
between `minBrightness` and `maxBrightness`, and the fixed `brightness` value is
ignored.

**Fix.** Turn auto-brightness off in the same request:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"autoBrightness":false,"brightness":120}'
```

## Auto-brightness is backwards - a bright room dims the panel

**Symptom.** With auto-brightness on, the panel goes *dim* in bright light and
*bright* in the dark.

**Fix.** Set `ldrOnGround` to invert the reading - a light sensor wired against
ground reads high in the dark.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"ldrOnGround":true}'
```

If the range is wrong rather than inverted, check the two limits as well - they
default to `10` and `220`:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"minBrightness":10,"maxBrightness":220}'
```

See [Brightness & sensors](../guides/brightness.md).

## The clock shows the wrong time, or sits at 00:00

Three symptoms with three different answers.

**Stuck at 00:00 on 1 January.** AWTRIX has never reached an NTP server. The sync
is re-armed every time the Wi-Fi comes up, so this normally clears within seconds
of connecting. If it persists, something is in the way - a router that blocks
outbound UDP 123, or a network that runs its own NTP host. Point `ntpServer` at
that host:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"ntpServer":"192.168.1.1"}'
```

**Off by whole hours.** The `tz` string is wrong for your location. Pick your
city again under Settings → System → Time; the picker writes a rule that already
carries the daylight-saving dates. Writing `tz` by hand over the API is **not
validated**, so a malformed string gives you the wrong offset rather than an
error.

**Off by minutes.** NTP never succeeded and the clock is free-running. Check that
the time server is reachable rather than adjusting anything - there is no manual
offset.

Both `tz` and `ntpServer` apply immediately, with no reboot. See
[System configuration → Time](../reference/system.md#time).

---

## Updating a pushed app returns 422

**Symptom.** `PUT /api/v1/apps/pushed/{name}` answers `422` with
`a JSON body is required; use DELETE /api/v1/apps/{name} to remove the app`.

**Check.** The body arrived empty - usually the `Content-Type` trap above - or
you sent literally `{}`. Neither is a valid update, and neither is treated as a
delete.

**Fix.** Send a non-empty JSON object with `Content-Type: application/json`. A
`PUT` replaces the app rather than merging into it, so to change one field send
the full object you want stored:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/clock \
  -H "Content-Type: application/json" \
  -d '{"text":"hi","textColor":"#00FF00"}'
```

Removing an app is `DELETE /api/v1/apps/{name}` - note the path, without
`pushed`. A `DELETE` on the `pushed` path answers `405 allowed method(s): PUT`.

## My notification request returns `400 invalidJson` but my JSON is valid

**Symptom.** `POST /api/v1/notifications` answers
`400 {"error":{"code":"invalidJson","message":"request body is not valid JSON"}}`
even though the JSON you pasted parses fine elsewhere.

**Check.** The body arrived empty - the `Content-Type` trap again. The same 400
appears for `PATCH /api/v1/settings` and `PATCH /api/v1/display`.

**Fix.** Set `Content-Type: application/json`.

An oversized body is a different failure: anything above 8192 bytes is rejected
up front with `413 payloadTooLarge`. If you are pushing a large base64 icon, that
is the limit you are hitting.

## The mood light won't turn on

**Symptom.** `PUT /api/v1/display/moodlight` does not put the panel into
mood-light mode.

**Check.** A `422` means the body arrived empty, which a wrong `Content-Type`
does to it. An empty body is not accepted here; turning the mood light off is
`DELETE /api/v1/display/moodlight`.

**Fix.** Send the mood-light object with the JSON content type:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/display/moodlight \
  -H "Content-Type: application/json" \
  -d '{"brightness":120,"color":"#FF8800"}'
```

---

## Enabling MQTT or login was rejected with 422

**Symptom.** `PUT /api/v1/system` answers `422 validationFailed` when you set
`mqttEnabled` or `authEnabled` to `true`, and nothing was saved.

**Check.** A gate can only be armed once it has what it needs: `mqttEnabled`
needs a non-empty `mqttHost`, and `authEnabled` needs a non-empty `authUser`
**and** `authPass`. The `field` in the error names the missing key.

**Fix.** Send the required fields in the same request:

```bash
# turn HTTP authentication on
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"authEnabled":true,"authUser":"admin","authPass":"s3cret"}'

# turn MQTT on
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883}'
```

To turn either back off, flip its gate on its own - the stored host and
credentials are kept, so re-enabling later needs no retyping:

```bash
# this route is behind the auth check, so it needs the credentials currently set
curl -u admin:s3cret -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" -d '{"authEnabled":false}'
```

Authentication is off by default, so an unprovisioned AWTRIX is open. Once it is
on, it applies in AP (provisioning) mode too.

## MQTT never connects

**Symptom.** No state topics arrive and Home Assistant stays empty, but AWTRIX is
otherwise online and the web UI works.

**Check, in order:**

1. **`mqttEnabled` is `false`.** With the gate off nothing connects or
   subscribes, whatever broker host is stored. Set it - together with a host, if
   none is stored - and reboot, because the broker connection is read once at
   boot:

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/system \
      -H "Content-Type: application/json" \
      -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883}'
    curl -X POST http://<awtrix-ip>/api/v1/device/reboot
    ```

2. **The broker is not reachable.** The connection state says why. The web UI
   shows it as the **Connection** line on the MQTT tab, and the API reports the
   same thing:

    ```bash
    curl -s http://<awtrix-ip>/api/v1/device | jq .mqtt
    ```

    ```json
    {"enabled": true, "state": "offline", "host": "broker.local", "endpoint": "",
     "attempts": 4, "retryInMs": 40000, "connects": 0, "error": "hostNotFound"}
    ```

    The two you are most likely to see:

    - **`hostNotFound`** - the broker name did not resolve. A `.local` name needs
      a responder answering for it on the same network as AWTRIX. If nothing
      answers, enter the broker's IP address instead and reboot.
    - **`refused`** - the address resolved but nothing accepted a connection
      there. Check the port, and that the broker is reachable from the network
      AWTRIX is on.

    Every code and what to do about it:
    [Device state → What each error means](../reference/device.md#what-each-error-means).

`GET /api/v1/logs` carries the same failures with the broker's own status code,
which is worth having when you take the question to your broker's log. Full
setup: [MQTT automation](../guides/mqtt.md).

## Home Assistant shows fewer entities than expected

**Symptom.** Fewer discovered entities than the published entity list leads you
to expect.

**Check.** Entities follow the hardware. Temperature, humidity and pressure are
created only for what the detected sensor actually measures, and the battery
entities only when `pinBattery` is set. `GET /api/v1/device` is the test:
whatever it omits there is also absent from Home Assistant. Which entity needs
which part: [Home Assistant](../guides/home-assistant.md).

---

## A system field I set does nothing

Every field `/api/v1/system` accepts is used, so a change with no visible effect
is almost always one of two things:

- **It needs a reboot.** Most system fields are read once at boot. The "Reboot"
  column in [System configuration](../reference/system.md) says which. Send
  `POST /api/v1/device/reboot` and check again.
- **It was dropped as an unknown key.** `PUT /api/v1/system` is a partial merge
  and does not reject unknown keys, so a typo is accepted with `200` and
  discarded. The response body is the full resulting configuration - a key you
  sent that is missing from it was never a real field.

## Reboot, sleep and reset stop answering afterwards

**Symptom.** `POST /api/v1/device/reboot`, `/api/v1/device/sleep`,
`/api/v1/device/factory-reset` or `/api/v1/settings/reset` answers, and then
every request that follows fails until AWTRIX is back.

**This is expected.** These routes answer `200 {"ok":true}` first and act
immediately afterwards, so AWTRIX is off the network for the next few seconds -
or until the sleep timer expires. A follow-up request sent right away has nothing
to talk to.

**What to do.** Wait, then poll `GET /api/v1/device`, which is the cheapest thing
to ask for. A reboot is answering again within a couple of seconds. Do not
re-send the command because the *next* request failed - the reboot already
happened.

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot   # -> {"ok":true}
sleep 5
curl http://<awtrix-ip>/api/v1/device                  # back up
```

If one of these routes gives you no body at all, the request never reached
AWTRIX - check the address. A wrong or missing credential instead gets you a
`401` with an `unauthorized` error body, not an empty response - check
`authEnabled` and your username and password.

`sleep` needs `durationMs` as a positive integer, and AWTRIX wakes on that timer.
`factory-reset` is available over HTTP only, not over MQTT.

## Related

* [FAQ](faq.md) - the questions new owners ask first
* [Errors](../reference/errors.md) - every error code, status code and validation message
* [Finding AWTRIX](../getting-started/discovery.md) - by name, by discovery, or off the panel
* [First boot](../getting-started/first-boot.md) - joining AWTRIX to your Wi-Fi
