# Your first notification

A notification is a page that jumps the queue: it interrupts whatever AWTRIX is showing, says its piece, and disappears. It is the fastest way to get something onto the panel - no app to create, no state to clean up.

## Send one now

Point this at AWTRIX and press enter:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Hello"}'
```

The panel shows `HELLO` for seven seconds, then goes back to the clock. You get back:

```json
{"ok":true}
```

That is the whole of it. Everything from here is one more key in that JSON object.

`<awtrix-ip>` is your device's IP address, or its hostname - `awtrixng-xxxxxx.local` by default - see [Finding AWTRIX](../getting-started/discovery.md). Send the `Content-Type` header on every request: it is [mandatory on the other routes](../reference/conventions.md#content-type-is-mandatory).

### Why is it uppercase?

Because the global `uppercase` setting defaults to on, not because notifications are special. Send `"textCase": "asTyped"` to leave your text exactly as you typed it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Hello","textCase":"asTyped"}'
```

## Color

Add `textColor`:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Disk full","textColor":"#FF0000"}'
```

`"#FF0000"` is one of several accepted spellings - `"FF0000"`, `"F00"`, `[255,0,0]`, `["HSV",0,100,100]` and the packed integer `16711680` all mean the same red. The full table is at [Colors](../reference/payload.md#colors).

For differently coloured runs inside one string, rainbow, gradients, blinking and fading, see the [Text & colors](text.md) guide.

## Icons

`icon` takes an icon ID that lives on the AWTRIX filesystem:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Doorbell","icon":"1234","textColor":"#00AAFF"}'
```

AWTRIX looks for `/ICONS/1234.gif` first, then `/ICONS/1234.jpg`. Only GIF and JPEG work - no PNG. If nothing matches, the notification still shows, just without the icon and without the reserved icon column.

You can also inline the image as base64 instead of uploading it first: any `icon` string longer than 64 characters is treated as inline data rather than an ID. Getting icons onto AWTRIX, and the base64 form, are covered in [Icons & assets](icons.md); the layout rules are at [Icon](../reference/payload.md#icon).

## Sound

Two ways, depending on where the melody lives.

**Inline** - pass an RTTTL string in `soundRtttl` and nothing needs to be on the filesystem:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Doorbell","icon":"1234","soundRtttl":"bell:d=4,o=5,b=120:c,e,g"}'
```

**From a file** - `sound` names a stored sound:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Doorbell","sound":"chime"}'
```

`sound` is a name, and AWTRIX picks the output: the uploaded MP3 `/MP3/chime.mp3` if there is one,
else the melody `/MELODIES/chime.txt` on the buzzer, else a DFPlayer track when the name is a plain
number. Uploading MP3s: [MP3s](sounds.md#mp3s).

The melody plays once, when the notification first appears. Add `"soundLoop": true` to re-trigger it each time it finishes, for as long as the notification is on screen.

Both keys need the global `soundEnabled` setting on. A name nothing is stored under plays nothing and still returns `200`; an RTTTL string that does not parse is rejected outright with `422 validationFailed` and the notification is not pushed. If you send both, only the RTTTL plays. More on melody format, uploading files and DFPlayer wiring: [MP3s & melodies](sounds.md) and [Sound](../reference/payload.md#sound).

## How long it stays

By default a notification shows for the global `appDurationMs` setting - **7000 ms** out of the box. Override it per notification with `durationMs`:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Quick","durationMs":2000}'
```

Durations are integer **milliseconds**. `0` or negative falls back to the global setting.

The duration decides on its own, however much text is left to scroll. Send `"repeat": 1` to keep the notification up until the text has run across the screen once instead - no shorter, and no longer either - or a higher number for more runs. With `durationMs` set it stays at least that long. See [`repeat`](../reference/payload.md#repeat).

To make it stay until you say otherwise, use `hold`:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALARM","textColor":"#FF0000","hold":true}'
```

`hold: true` ignores `durationMs` entirely. The notification stays on the panel until it is [dismissed](#dismissing) - nothing times it out. Combine it with `soundLoop` for an alarm that will not stop on its own.

### Waking a dark panel

If the matrix has been powered off (`PATCH /api/v1/display {"power":false}`) a notification is not drawn - unless it asks:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Motion","wakeup":true}'
```

`wakeup: true` skips display blanking for as long as that notification is the active one. When it ends, the panel goes dark again.

## Stacking and interrupting

Notifications queue. Send three and they play back to back, in order - that is `stack: true`, the default.

Set `stack: false` when the new message makes the old one irrelevant. It **replaces** whatever is on screen right now, keeping anything queued behind it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"URGENT","stack":false,"textColor":"#FF0000"}'
```

Replacing restarts scroll, icon and sound from the top. On an empty queue, `stack: false` behaves like a normal push.

Two things to keep in mind when you stack:

- `hold` pins only the front notification, and the queue does not advance until it is dismissed. Do not combine `hold` with a stream of stacked notifications.
- The queue holds 32 notifications, counting the one on screen. A stacked push into a full queue is rejected with `507 insufficientStorage`, so a dropped message is reported rather than lost - treat it as "slow down". See [Limits](../reference/limits.md#apps-and-notifications).

Post notifications one at a time: an array body carrying more than one object is rejected with `422 validationFailed`.

## Dismissing

Drop the notification currently on screen:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/active
```

This is how you end a `hold`. It always returns `200 {"ok":true}`, even when nothing is showing. The next queued notification, if any, takes over immediately.

### Dismissing a specific notification by name

Give a notification a `name` when you push it, and you can retract *that* one later - wherever it sits in the queue, even if it is not the one on screen:

```bash
# push one that can be retracted later
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"name":"backup-job","text":"Backup running","hold":true}'

# retract it, whatever else has arrived since
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/backup-job
```

Removing one that was still waiting is invisible and leaves the current notification running. You get `200 {"ok":true}` when a match was removed, or `404 notFound` when nothing in the queue carries that name.

`/api/v1/notifications/active` always means "whichever notification is on screen", so a notification named literally `active` cannot be addressed by name. Pick any other name. Full rules: [`DELETE /api/v1/notifications/{name}`](../reference/http.md#delete-apiv1notificationsname).

## Over MQTT

Every notification above works unchanged over MQTT - same JSON, different transport. Publish the payload to `<prefix>/cmd/notify`, `<prefix>/cmd/notify/dismiss` to drop the current one, and `<prefix>/cmd/notify/dismiss/<name>` to retract one by name:

```bash
mosquitto_pub -h broker.local -t 'awtrix_ab12cd/cmd/notify' \
  -m '{"text":"Hello","icon":"1234"}'

# retract the "backup-job" notification
mosquitto_pub -h broker.local -t 'awtrix_ab12cd/cmd/notify/dismiss/backup-job' -m ''
```

`<prefix>` defaults to the device UID unless you set `mqttPrefix`. Topics, the `/result` reply and broker setup: [MQTT topics](../reference/mqtt.md#notify) and the [MQTT automation](mqtt.md) guide.

## When something looks wrong

The whole payload is applied or the whole payload is rejected - nothing is queued when a request trips. There are four outcomes:

| Response | When |
|---|---|
| `200 {"ok":true}` | accepted |
| `400 invalidJson` | the body is not valid JSON |
| `413 payloadTooLarge` | the body is over 8192 bytes - easy to hit with inline base64 icons |
| `422 validationFailed` | with the offending key in `field` |

A `422` means an unknown top-level key, an unreadable colour, a mode word AWTRIX does not know, an `effect` or `overlay` name AWTRIX does not know, or a malformed `draw` or `scroll` value. Spelling matters for `effect` and `overlay` names; casing does not - `"plasma"`, `"Plasma"` and `"PLASMA"` all resolve.

Everything else is only read when the JSON type matches. `{"durationMs":"5000"}` or `{"textCenter":"yes"}` returns `200` and the key is simply skipped, so that field keeps its default; numbers outside their range are clamped rather than rejected. `lifetimeMs` and `lifetimeExpiry` are accepted and never read - they only mean something for pushed apps. The [error reference](../reference/payload.md#errors) lists every case.

## Related

- **[App & notification payload](../reference/payload.md)** - the exhaustive field table: every key, type, range, default and interaction. Start at [Notification-only keys](../reference/payload.md#notification-only-keys).
- **[Pushed apps](pushed-apps.md)** - for a page that lives in the rotation instead of interrupting it.
- **[Charts & drawing](graphics.md)** - bars, line charts, progress bars and draw primitives, all of which work on notifications too.
- **[Effects & overlays](effects.md)** - animated backgrounds and weather overlays.
- **[HTTP API v1](../reference/http.md#post-apiv1notifications)** - the endpoint's own reference entry.
