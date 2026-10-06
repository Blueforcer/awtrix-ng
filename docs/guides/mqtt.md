# MQTT

This page shows how to control AWTRIX through an MQTT broker. For example, show a doorbell alert, a build status or a power reading on the display. MQTT is a messaging system: your automation publishes a message to the broker, and the broker passes it on to AWTRIX. Your automation never has to reach AWTRIX directly.

## How it behaves

AWTRIX connects to one broker and listens on `<prefix>/cmd/...`. The message you publish there is the same JSON you would send over HTTP, so anything you can `curl` you can publish. AWTRIX answers every command it knows on the same topic with `/result` added. A topic it does not know gets no answer at all. AWTRIX also publishes its state by itself, on retained topics under `<prefix>/state/`, so a new subscriber gets the current values at once.

## Point AWTRIX at your broker

MQTT is off until you switch it on. You can do this in the web UI (**System** tab, **MQTT** section), or with one API call:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H 'Content-Type: application/json' \
  -d '{"mqttEnabled":true,"mqttHost":"192.168.1.10","mqttPort":1883,"mqttPrefix":"awtrixNG"}'
```

Then restart AWTRIX, because the MQTT settings are read once, at start-up:

```bash
curl -X POST http://<awtrix-ip>/api/v1/device/reboot
```

<!-- only tc002 -->
AWTRIX can also connect over TLS. See [Connect over TLS](#connect-over-tls-tc002).
<!-- /only -->

If your broker needs a login, add `mqttUser` and `mqttPass`. With both empty, AWTRIX connects without a login. Every key, its default and its type is in [System configuration → MQTT and Home Assistant](../reference/system.md#mqtt-and-home-assistant).

To see whether it worked, read `GET /api/v1/device`: the [`mqtt`](../reference/device.md#connection-status) object says whether AWTRIX is connected and, if not, why. The web UI shows the same thing as **Connection** in the **MQTT** section of the **System** tab.

To turn MQTT off again, send `{"mqttEnabled":false}` and restart. AWTRIX stays connected to the broker until the restart. The host and login are kept.

## Publish your first command

```bash
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/notify' \
  -m '{"text":"Doorbell","textColor":"#FF0000","durationMs":10000}'
```

The display interrupts what it was showing and shows `DOORBELL` in red for ten seconds. This is the same object you would send to `POST /api/v1/notifications`: same keys, same defaults, same result.

Watch the answer come back on a second terminal:

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/cmd/#' -v
```

```
awtrixNG/cmd/notify {"text":"Doorbell","textColor":"#FF0000","durationMs":10000}
awtrixNG/cmd/notify/result {"ok":true}
```

MQTT messages have no `Content-Type` header, so there is nothing to forget. The `curl` calls on this page always send `Content-Type: application/json`. Without it, `curl -d` marks the body as a form, and AWTRIX refuses a `PUT` or `PATCH` with that ([Content-Type](../reference/conventions.md#content-type-is-mandatory)).

## The prefix

`<prefix>` is the value of `mqttPrefix`. If you leave it empty, AWTRIX uses its own **uid** (the twelve-character MAC address) so topics look like `a4cf12ab34cd/cmd/notify`. That works, but a readable prefix such as `awtrixNG` is easier to use.

AWTRIX ignores every topic outside `<prefix>/`, and only topics under `<prefix>/cmd/` are commands. State topics only go out from AWTRIX: publishing to one does nothing.

Give every AWTRIX on your broker its own prefix. If two share one, both act on every command.

## Anything you can curl, you can publish

To turn an HTTP request into an MQTT command, take the path after `/api/v1/`, put `cmd/` in front of it, and publish the body you would have sent. A few topics have shorter names: notifications go to `cmd/notify`, and switching to an app is `cmd/apps/switch`. Both commands below show the same notification:

<!-- panel motion=4 -->
```bash
# HTTP
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Build failed","textColor":"#FF0000"}'

# MQTT - same body, byte for byte
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/notify' \
  -m '{"text":"Build failed","textColor":"#FF0000"}'
```

More examples:

```bash
# a pushed app that stays in the rotation
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/apps/pushed/power' \
  -m '{"text":"432W","icon":"1234","textColor":"#FFAA00"}'

# an empty payload deletes it again
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/apps/pushed/power' -m ''

# settings, validated exactly as PATCH /api/v1/settings validates them
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/settings' \
  -m '{"brightness":120,"autoBrightness":false}'

# jump to an app - a bare name works, no JSON needed
mosquitto_pub -h 192.168.1.10 -t 'awtrixNG/cmd/apps/switch' -m 'Time'
```

Every command topic, with its HTTP route and accepted payload, is listed in [MQTT topics → Command topics](../reference/mqtt.md#command-topics). The payload keys (text, icons, colors, effects) are described in [App & notification payload](../reference/payload.md).

Four differences from HTTP:

- **Reading is HTTP-only.** There is no `cmd/device/get`. Instead, AWTRIX publishes its state to
  retained topics. See [Subscribe to state](#subscribe-to-state) below. The one exception is
  `cmd/screen/get`, which asks for a single `state/screen` message.
- **Factory reset is HTTP-only.** Publishing to `cmd/device/factory-reset` does nothing and answers
  nothing. The route is `POST /api/v1/device/factory-reset`.
- **MQTT reaches pushed apps only.** `cmd/apps/pushed/<name>` creates, replaces and deletes a
  pushed app. Scripts have no topic at all, and removing one is `DELETE /api/v1/apps/{name}`
  over HTTP.
- **An empty message deletes.** An empty payload (or `{}`) deletes a pushed app, turns the
  mood light off or turns an indicator off, like the HTTP `DELETE`. Never publish an empty message
  just to test a topic.

## Read the answer

Every command AWTRIX recognizes is answered on the same topic with `/result` added (not retained):

```
awtrixNG/cmd/settings       ->  awtrixNG/cmd/settings/result
awtrixNG/cmd/apps/pushed/x  ->  awtrixNG/cmd/apps/pushed/x/result
```

Success is exactly:

```json
{"ok":true}
```

Failure is the HTTP error body wrapped in `ok:false`:

```json
{"ok":false,"error":{"code":"validationFailed","message":"out of range","field":"brightness"}}
```

The codes are the same ones HTTP returns for the same mistake, listed with the messages they carry over MQTT in [Errors → Errors over MQTT](../reference/errors.md#errors-over-mqtt).

**A topic AWTRIX does not recognize gets no answer at all**: no error, nothing. A typo such as `awtrixNG/cmd/notfiy` is simply ignored, and so is `awtrixNG/cmd/indicators/9`, where HTTP would answer 404. If a command seems to do nothing, check the topic spelling first.

Commands that restart AWTRIX (`cmd/device/reboot`, `cmd/settings/reset`) may restart before the answer goes out. Do not wait for it.

## Subscribe to state

AWTRIX publishes its state by itself. These topics are **retained**: the broker keeps the last value, so a new subscriber gets it the moment it connects.

| Topic | What |
|---|---|
| `<prefix>/state/device` | the `GET /api/v1/device` object |
| `<prefix>/state/settings` | the `GET /api/v1/settings` object |
| `<prefix>/state/apps/active` | the current app name, as a plain string |
| `<prefix>/state/capabilities` | available effects, transitions, overlays, palettes |

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/state/#' -v
```

`state/device` is the one most automations want: uptime, free memory, Wi-Fi signal, <!-- only esp32 esp32-s3 -->light level, <!-- /only -->battery and the current app. Its fields are documented at [Device state](../reference/device.md). The full topic list, including the radio and screen topics and every retain flag, is [MQTT topics → State topics](../reference/mqtt.md#state-topics).

`state/settings` and `state/apps/active` are published as soon as the value changes, so a change made over HTTP or in the web UI shows up on MQTT at once. `state/device` goes out every `statsInterval` milliseconds (10 000 by default) and sooner when the display power or an indicator changes.

Base your automations on these state topics, not on the command you published. A lost message is lost without a warning. The state topics show what AWTRIX is really doing.

### Button presses

Each button publishes `"1"` when pressed and `"0"` when released. The messages are not retained, so only a client that is subscribed at that moment sees a press:

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/state/buttons/+' -v
```

```
awtrixNG/state/buttons/select 1
awtrixNG/state/buttons/select 0
```

The three buttons are `left`, `select` and `right`.<!-- only tc002 --> The knob publishes its presses the same way, as `knob`. Each turn of the knob goes to `<prefix>/event/knob`.<!-- /only --> Use these topics to trigger automations. In Home Assistant the button `binary_sensor` entities follow them too. See [Home Assistant](home-assistant.md).

### Is it alive?

AWTRIX publishes `online` to `<prefix>/availability` (retained) when it connects. If AWTRIX drops off, the broker publishes `offline` there for it (MQTT "last will"):

```bash
mosquitto_sub -h 192.168.1.10 -t 'awtrixNG/availability' -v
```

Home Assistant discovery uses the same `<prefix>/availability` topic, so an automation that watches it keeps working when you switch discovery on. See [Availability and LWT](../reference/mqtt.md#availability-and-lwt).

<!-- only tc002 -->
## Connect over TLS {#connect-over-tls-tc002}

TLS encrypts the connection between AWTRIX and the broker. Nobody else on your
network can read the messages or your MQTT password.

Some words used below:

- **Certificate:** the broker's ID card. It proves that the broker is really your broker.
- **Certificate authority (CA):** someone who issues certificates, for example Let's Encrypt.
- **Fingerprint:** a 64-character code that identifies one certificate exactly, like a checksum.

### Switch it on

1. Make sure your broker accepts TLS. Most use port `8883` for it.
2. In the web UI, open **System** and go to the **MQTT** section.
3. Switch on **TLS**. The port changes to `8883` by itself. Change it if your broker uses another
   port.
4. Save and restart AWTRIX.

AWTRIX sends nothing to the broker, not even the login, until it knows the broker is the right one.
It accepts the broker when:

- the broker's certificate comes from a public certificate authority and carries the name in
  **Broker host** (typical for cloud brokers), or
- you told AWTRIX to trust the broker's certificate (next section).

This check cannot be switched off.

### Trust your broker's certificate

Brokers at home, such as the Home Assistant Mosquitto add-on or Mosquitto on a Raspberry Pi,
usually have a certificate of their own. AWTRIX refuses such a broker the first time.

1. In the **MQTT** section, **Broker certificate** appears, with the certificate's fingerprint.
2. Compare it with the fingerprint of your broker's certificate. On the broker's computer (for Home
   Assistant: in the **Terminal** add-on):

    ```bash
    openssl x509 -in /ssl/fullchain.pem -noout -fingerprint -sha256
    ```

    Replace `/ssl/fullchain.pem` with the certificate file your broker uses. openssl prints the
    fingerprint in capitals with colons. It is the same code.

3. If both match, press **Trust**. AWTRIX connects within a few seconds. No restart needed.

If the broker later shows a different certificate, AWTRIX refuses it and shows the new
fingerprint. That happens when the broker got a new certificate, but also when another device
pretends to be your broker. Compare again before you press **Trust**.

The trusted fingerprint is saved as `mqttTlsPin` and is part of a backup.

### Use your own certificate authority

Only needed if you run your own certificate authority.

1. In the **MQTT** section, go to **Broker CA** and press **Upload**.
2. Pick your authority's certificate file (`.pem` or `.crt`, at most 64 KB).

From then on, AWTRIX accepts only brokers whose certificate comes from this authority and carries
the name in **Broker host**. Public authorities and trusted fingerprints do not count while your
authority is uploaded. Press **Delete** to go back.

If the uploaded file cannot be read after a restart, AWTRIX connects to no broker at all.
**Broker CA** then shows **Unusable**. Upload the file again, or delete it.

### Use it over the API

Everything above also works with `curl`. Switch TLS on (then restart):

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttTls":true,"mqttPort":8883}'
```

Read the fingerprint that waits for your decision from
[`GET /api/v1/mqtt/tls`](../reference/http.md#get-apiv1mqtttls), and trust it:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"mqttTlsPin":"<the 64-character fingerprint>"}'
```

Upload your own certificate authority:

```bash
jq -n --rawfile certificate ca.crt '{$certificate}' |
  curl -X PUT http://<awtrix-ip>/api/v1/mqtt/tls/ca -H "Content-Type: application/json" -d @-
```

### When the broker ends the connection

- **The log says `mqtt: broker requires a client certificate`.** The broker wants a certificate
  from AWTRIX as well. AWTRIX logs in with username and password only. Switch this off on the
  broker: in the Home Assistant Mosquitto add-on, turn off **Require Client Certificate**. In
  Mosquitto itself, set `require_certificate false`.
- **The log says `mqtt: broker closed TLS: ...`.** The broker ended the encrypted connection. The
  rest of the line says why, for example `handshake failure` when AWTRIX and the broker have no
  encryption in common.
<!-- /only -->

## Good to know {#when-it-goes-wrong}

- **Nothing arrives, and a yellow dot pulses in the bottom-left corner of the display.** AWTRIX cannot
  reach the broker. See [MQTT never connects](../troubleshooting/troubleshooting.md#mqtt-never-connects).
- **A command does nothing and no `/result` comes back.** The topic is misspelled or does not exist.
- **A large command does nothing and gets no answer.** A command over 8192 bytes is dropped,
  most often a notification that carries a big icon. See [Limits](../reference/limits.md).
- **The `/result` says `ok:false`.** The `error` names the problem and usually the `field`. See
  [Errors over MQTT](../reference/errors.md#errors-over-mqtt).
- **You are not sure which command failed.** Subscribe to `awtrixNG/event/error`. Every rejected
  command appears there, also the ones sent over HTTP. See
  [event/error](../reference/mqtt.md#eventerror).

## Related

- [MQTT topics](../reference/mqtt.md): every topic, payload and retain flag.
- [Home Assistant](home-assistant.md): discovery and entities.
- [App & notification payload](../reference/payload.md): the keys that go inside those payloads.
- [Errors](../reference/errors.md#errors-over-mqtt): the same error body, over HTTP and MQTT.
