# Your first notification

This page shows you how to send a short message that interrupts the clock, and how to change its
look, its sound and how long it stays.

!!! tip "New here?"
    [How the display works](display.md) shows where things sit on the display and which text
    moves by itself.

## What you get {#send-one-now}

Replace `<awtrix-ip>` with the IP address of your clock and run:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Hello"}'
```

The display shows `HELLO` for seven seconds, then the rotation goes on. The answer is:

```json
{"ok":true}
```

Everything else on this page is one more key in that JSON object. `<awtrix-ip>` can also be the
hostname, `awtrixng-xxxxxx.local` by default. See [Find your clock](../getting-started/discovery.md).

## How it behaves

A notification is shown once, on top of the [rotation](display.md#apps-take-turns), for seven
seconds by default. The rotation
[keeps turning behind it](pushed-apps.md#the-loop-keeps-turning-behind-a-notification), so
afterwards you often see another app than before. Notifications that arrive meanwhile wait in a
queue and are shown one after the other. The text moves only when it does not fit, and text you
draw with `draw` never moves ([When text moves](display.md#when-text-moves)). The notification
ends when its time is up, even in the middle of moving text. To keep it until the text has been
read, add [`repeat`](#wait-until-long-text-has-been-read) next to `text`.
<!-- only tc002 -->

**A notification is drawn at double size,** like a pushed app. So positions in `draw` count on a
grid of 26 × 8: the last column is 25, the last row 7. See [The display](display.md#the-display).
<!-- /only -->

## Send it from Home Assistant {#from-home-assistant}

Let Home Assistant send a notification from any automation. Add a REST command to your
`configuration.yaml`:

```yaml
rest_command:
  awtrix_notify:
    url: "http://<awtrix-ip>/api/v1/notifications"
    method: POST
    content_type: "application/json"
    payload: '{"text":"{{ message }}"}'
```

Restart Home Assistant. Then use it in any automation or script:

```yaml
action: rest_command.awtrix_notify
data:
  message: "Someone is at the door"
```

To send over MQTT instead, see [Over MQTT](#over-mqtt).

## Keep upper and lower case {#why-is-it-uppercase}

Text is shown in capitals, because the `uppercase` setting is on by default. Send
`"textCase": "asTyped"` to keep your text exactly as you typed it:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Hello","textCase":"asTyped"}'
```

## Show it in a color {#color}

Give the text a color with `textColor`:

<!-- panel motion=4 -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Disk full","textColor":"#FF0000"}'
```

These all mean the same red: `"#FF0000"`, `"FF0000"`, `"F00"`, `[255,0,0]`, `["HSV",0,100,100]`
and `16711680`. For several colors in one string, gradients, blinking and fading, see
[Text & colors](text.md).

## Show an icon next to the text {#icons}

`icon` takes the ID of an icon stored on the clock:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"29°C","icon":"sun","textColor":"#FFAA00"}'
```

`sun` is an icon from the [AWTRIX Hub](icons.md#install-from-the-awtrix-hub). AWTRIX looks for
`/ICONS/sun.gif` first, then `/ICONS/sun.jpg`. Only GIF and JPEG work, not PNG. If no file matches,
the notification still shows, without the icon and without the space for it.

You can also send the image itself instead of an ID, as a data URL:
`data:image/gif;base64,…` or `data:image/jpeg;base64,…`.
<!-- only tc002 -->
A web address works too: [Pictures from the internet](icons.md#pictures-from-the-internet).
<!-- /only -->
How to get icons onto the clock: [Icons](icons.md).

## Play a sound with it {#sound}

`sound` plays a sound when the notification appears. It takes the same sound as
[`/api/v1/audio/play`](sounds.md#play-a-sound).

**A stored sound** by its name:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Doorbell","sound":"chime"}'
```

<!-- only esp32 -->
AWTRIX plays the melody `/MELODIES/chime.txt`.
<!-- /only -->
<!-- only esp32-s3 tc002 -->
AWTRIX looks for the name in this order:

1. the MP3 file `/MP3/chime.mp3`,
2. the melody `/MELODIES/chime.txt`.

A notification sent by a script looks in the script's [own sounds](scripting/sound.md) first. How
to upload MP3 files: [MP3s](sounds.md#mp3s).
<!-- /only -->

**A melody in the request**, in RTTTL. Nothing needs to be stored on the clock:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Doorbell","sound":{"rtttl":"bell:d=4,o=5,b=120:c,e,g"}}'
```

RTTTL is a short text format for ringtones: a name, the tempo, then the notes.

<!-- only tc002 -->
**Speech or a sound**, when your clock has a voice. The clock plays the first entry it can play:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Door","sound":[{"speech":"The front door is open."},"ding"]}'
```
<!-- /only -->

The sound plays once, when the notification appears, at the alert volume
([Volume](sounds.md#volume)). Add `"loop": true` inside the sound to repeat it for as long as the
notification is shown. It stops as soon as the notification goes.

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALARM","hold":true,"sound":{"file":"siren","loop":true}}'
```

More about sounds: [Sound](sounds.md).

## Show it longer or shorter {#how-long-it-stays}

A notification shows for the `appDurationMs` setting, **7000 ms** by default. Set a different time
for one notification with `durationMs`, in milliseconds:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Quick","durationMs":2000}'
```

## Keep it until the text has been read {#wait-until-long-text-has-been-read}

The notification ends when its time is up, even if the text is still moving. Add `repeat` to keep
it until the text has run through:

<!-- panel motion=4 -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"The washing machine has finished","repeat":1}'
```

`repeat` stands at the top level of the JSON, next to `text`. It does not go inside `scroll`.

The notification then stays exactly as long as the text needs. A higher number means more passes.
If you also set `durationMs`, it stays at least that long.

## Keep it until you remove it

Use `hold`:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALARM","textColor":"#FF0000","hold":true}'
```

With `hold: true` the notification ignores `durationMs` and stays until you
[dismiss](#dismissing) it. Add a sound with `"loop": true` for an alarm that does not stop by
itself.

## Wake a dark display {#waking-a-dark-panel}

When the display is switched off (`PATCH /api/v1/display {"power":false}`), notifications are not
shown. Add `wakeup` to show one anyway:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Motion","wakeup":true}'
```

The display lights up while this notification is shown and goes dark again when it ends.

## Interrupt the notification shown {#stacking-and-interrupting}

Notifications wait in a queue. Send three and they play one after the other, in order. This is
`stack: true`, the default.

Send `stack: false` when a new message makes the current one pointless. It **replaces** the
notification shown. Notifications waiting behind it stay in the queue:

<!-- panel -->
```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"URGENT","stack":false,"textColor":"#FF0000"}'
```

The replacement starts from the beginning: scrolling, icon and sound. If nothing is showing,
`stack: false` works like a normal notification.

## Dismiss a notification {#dismissing}

Remove the notification shown before its time is up. On the clock, press select. In the web UI,
press the **Bell** under the live picture on the **Dashboard**. Over the API:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/active
```

This is how you end a `hold`. The answer is always `200 {"ok":true}`, even when nothing is showing.
The next notification in the queue appears at once.

### Dismiss one by name {#dismissing-a-specific-notification-by-name}

Give a notification a `name`. You can then remove exactly that one later, even if it is still
waiting in the queue:

```bash
# send one with a name
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"name":"backup-job","text":"Backup running","hold":true}'

# remove it later
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/backup-job
```

The answer is `200 {"ok":true}` when it was removed, and `404 notFound` when no notification with
that name is in the queue. Removing a waiting notification does not disturb the one shown.

`active` always means "the notification that is shown", so you cannot use `active` as a name.

## Over MQTT

Every example on this page works over MQTT with the same JSON. Use these topics:

| Topic | Does |
|---|---|
| `<prefix>/cmd/notify` | send a notification |
| `<prefix>/cmd/notify/dismiss` | remove the notification shown |
| `<prefix>/cmd/notify/dismiss/<name>` | remove the notification with that name |

```bash
mosquitto_pub -h broker.local -t 'a4cf12ab34cd/cmd/notify' \
  -m '{"text":"Hello","icon":"sun"}'

# remove the "backup-job" notification
mosquitto_pub -h broker.local -t 'a4cf12ab34cd/cmd/notify/dismiss/backup-job' -m ''
```

`<prefix>` is the device ID (the 12-character MAC address, like `a4cf12ab34cd` above) unless you
set `mqttPrefix`. From Home Assistant, use the `mqtt.publish` action. See
[Sending notifications from Home Assistant](home-assistant.md#sending-notifications-from-home-assistant)
and the [MQTT](mqtt.md) guide.

## Good to know

- **`repeat` inside `scroll` is refused** with `422` and `"field":"scroll.repeat"`. Put it next to
  `text`.
- **`hold` stops the queue.** The notifications behind it wait until you dismiss it, so do not mix
  `hold` with a stream of stacked notifications.
- **The queue holds 32 notifications,** counting the one shown. One more is refused with
  `507 insufficientStorage`: send less often.
- **A value of the wrong type is skipped, not refused.** `{"durationMs":"5000"}` answers `200` and
  keeps the default time. Send numbers without quotes.
- **A sound name that is not stored plays nothing.** The request still answers `200`, so check the
  spelling of the name.

## Details

- Every key, type, range and default: [App & notification payload](../reference/payload.md).
  Start at [Notification-only keys](../reference/payload.md#notification-only-keys).
- [`repeat`](../reference/payload.md#repeat), [Icon](../reference/payload.md#icon),
  [Colors](../reference/payload.md#colors) and [Sound](../reference/payload.md#sound)
- What is refused, and with which answer: [Errors](../reference/payload.md#errors)
- The endpoints: [POST /api/v1/notifications](../reference/http.md#post-apiv1notifications) and
  [DELETE /api/v1/notifications/{name}](../reference/http.md#delete-apiv1notificationsname)
- Queue and request size: [Limits](../reference/limits.md#apps-and-notifications)
- All topics: [MQTT topics](../reference/mqtt.md#notify)

## Related

- [Pushed apps](pushed-apps.md): an app that stays in the rotation instead of interrupting it
- [Text & colors](text.md): fonts, colors and moving text
- [Charts & drawing](graphics.md): bars, lines, progress bars and drawings, also on notifications
- [Effects & overlays](effects.md): animated backgrounds and weather effects
- [Sound](sounds.md): what your clock can play, and how loud
