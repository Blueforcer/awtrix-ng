# Pushed apps

This page shows you how to send your own apps to the clock, keep them up to date and arrange the
rotation they take turns in.

!!! tip "New here?"
    [How the display works](display.md) shows where things sit on the display and which text
    moves by itself.

## What you get {#send-your-first-app}

Replace `<awtrix-ip>` with the IP address of your clock and run:

<!-- panel -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"21.5C","icon":"sun","textColor":"#00AAFF"}'
```

This creates an app called `weather`: the sun on the left, the temperature in blue next to it.
`sun` is an icon from the [AWTRIX Hub](icons.md#install-from-the-awtrix-hub).

The app joins the end of the rotation. To see it at once, press **Show** in its row on the
**Apps** tab of the web UI, or send:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H "Content-Type: application/json" -d '{"name":"weather"}'
```

## How it behaves

A pushed app shows what you send it, for example a stock price or the state of your washing
machine. AWTRIX never fetches anything itself, so the sender keeps the value up to date. The app
takes its turn in the [rotation](display.md#apps-take-turns) until you delete it, its lifetime runs
out or the clock restarts. AWTRIX places the text and the icon for you, and the text moves only
when it does not fit. Text you draw with `draw` never moves. See
[When text moves](display.md#when-text-moves).
<!-- only tc002 -->

**Your app is drawn at double size:** each of its pixels lights a square of 2 × 2. So positions
in `draw` count on a grid of 26 × 8, and the bottom-right corner is `["pixel",25,7]`. A larger
position lies outside the grid and shows nothing. With
[`enlargeApps`](../reference/settings.md#global-text) off, or with an icon bigger than 26 × 8, the
app uses all 52 × 16 pixels. See [The display](display.md#the-display).
<!-- /only -->

## Send it from Home Assistant {#from-home-assistant}

Let Home Assistant send the value whenever it changes. Add a REST command to your
`configuration.yaml`:

```yaml
rest_command:
  awtrix_weather:
    url: "http://<awtrix-ip>/api/v1/apps/pushed/weather"
    method: PUT
    content_type: "application/json"
    payload: '{"text":"{{ states(''sensor.outdoor_temperature'') }}C","icon":"sun"}'
```

Call `rest_command.awtrix_weather` from an automation whenever the sensor changes. Replace
`sensor.outdoor_temperature` with your own sensor.

## Update or remove an app {#push-update-delete}

Send the app again to show a new value. A `PUT` to a name that already exists replaces that app
completely:

<!-- panel -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"18.0C","icon":"sun"}'
```

When the app is shown, you see the change at once. If the text stays the same, the scrolling does
not restart, and an unchanged animated icon keeps playing. So an app that you update every few
seconds does not jump back to the start each time.

To remove the app, open its **⋯** menu on the **Apps** tab of the web UI and press **Delete**
twice. Over the API:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/apps/weather
```

Over MQTT, an empty message to `<prefix>/cmd/apps/pushed/weather` removes it
([MQTT command topics](../reference/mqtt.md#command-topics)).

A name may use `A–Z`, `a–z`, `0–9`, `_` and `-`, and is 1 to 32 characters long. Scripts follow the
same rule.

## Draw your own shapes

The `draw` key adds pixels, lines, rectangles, circles, text and images at positions you choose.
This app draws a blue frame around its text:

<!-- only esp32 esp32-s3 -->
<!-- panel style=diagram -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/frame \
  -H "Content-Type: application/json" \
  -d '{"text":"21°C","draw":[["rect",0,0,32,8,"#00AAFF"]]}'
```

`["rect",0,0,32,8]` starts at column 0 and row 0 and is 32 wide and 8 tall, so on a 32 × 8
display it runs along the edge.
<!-- /only -->
<!-- only tc002 -->
<!-- panel style=diagram -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/frame \
  -H "Content-Type: application/json" \
  -d '{"text":"21°C","draw":[["rect",0,0,26,8,"#00AAFF"]]}'
```

`["rect",0,0,26,8]` starts at column 0 and row 0 and is 26 wide and 8 tall. At double size, that
is the whole display.
<!-- /only -->

Positions count from the top-left corner of the display, also when the app has an icon. The
commands are `pixel`, `pixels`, `line`, `rect`, `rectFill`, `circle`, `circleFill`, `text` and
`bitmap`. What each one takes: [Draw commands](../reference/payload.md#draw-commands). More
examples: [Charts & drawing](graphics.md#draw-commands).

## Keep an app until its text has been read {#holding-an-app-until-its-text-has-finished}

Long text moves through the display, but the rotation does not wait for it. When the time is up,
the next app comes, even in the middle of the text. Add `repeat` to wait for whole passes:

<!-- panel motion=4 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H "Content-Type: application/json" \
  -d '{"text":"A rather long headline that will not fit on the display","repeat":1}'
```

`repeat` stands at the top level of the JSON, next to `text`. It does not go inside `scroll`.

The app then stays exactly as long as the text needs. If it has been read before the normal time is
up, the next app comes right away. Add `durationMs` to keep it longer, or a higher `repeat` for more
passes.

## Let an app run out {#apps-that-expire-by-themselves}

`lifetimeMs` sets how long an app lives after you sent it. `lifetimeExpiry` sets what happens then:

- `"remove"` (default): the app is deleted.
- `"mark"`: the app stays and gets a thin dark-red frame, so you can see its value is old.

```bash
# Delete the app 5 minutes after it was sent
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/doorbell \
  -H "Content-Type: application/json" \
  -d '{"text":"DING","lifetimeMs":300000,"lifetimeExpiry":"remove"}'
```

`mark` works as a warning light. Push the app every minute with `lifetimeMs: 180000` and
`lifetimeExpiry: "mark"`. If your automation stops, the red frame appears after three minutes,
instead of an old number looking current:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/power \
  -H "Content-Type: application/json" \
  -d '{"text":"1.2kW","lifetimeMs":180000,"lifetimeExpiry":"mark"}'
```

## Send several apps in one request {#one-request-many-apps}

Send an **array** of objects, and each object becomes its own app. The apps are named after the URL
name plus a number: `stocks0`, `stocks1`, `stocks2`:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/stocks \
  -H "Content-Type: application/json" \
  -d '[{"text":"AAPL 189"},{"text":"MSFT 412"},{"text":"NVDA 903"}]'
```

These three names appear in the rotation and in `GET /api/v1/apps`. There is no app called `stocks`.

## Bring an app back after a restart {#a-pushed-app-lasts-until-awtrix-restarts}

A pushed app is kept in memory, not saved. It stays until one of these happens:

- you replace or delete it,
- its `lifetimeMs` runs out,
- the clock restarts: after a power cut, a firmware update or `POST /api/v1/device/reboot`.

After a restart, the sender pushes the app again and it is back, with the current value. To make
this automatic, push on a schedule, or react to the MQTT availability topic
([MQTT](mqtt.md)).

The **order** of the rotation is saved, so a pushed app that comes back returns to its place. See
[The order is remembered across reboots](#the-order-is-remembered-across-reboots).

Some content should come back by itself, for example a fixed label or a drawn logo. Write a
[script](scripting/index.md) for it. Scripts are stored on the clock.

## See which apps take turns {#the-app-loop}

Every app in the rotation (built-in, pushed or script) has a name. The rotation is an ordered list
of these names, and the clock shows them one after the other, again and again.

Until you arrange the rotation yourself, it runs in this order:

1. the built-in apps: <!-- only esp32 esp32-s3 -->**Time, Date, Temperature, Humidity, Battery**<!-- /only --><!-- only tc002 -->**Time, Status**<!-- /only -->,
2. your pushed apps, in the order they first arrived,
3. your scripts, in the order you installed them.

Updating a pushed app keeps its place. Deleting it and sending it again puts it at the end. After a
restart, pushed apps come back in the order your automation sends them. So if the order matters,
[arrange the rotation](#reordering-switching-off-and-duplicating) once.

In the web UI, the **Apps** tab lists the rotation under **On the display**, in this order. Over
the API:

```bash
curl http://<awtrix-ip>/api/v1/apps
```

<!-- only esp32 esp32-s3 -->
```json
[
  {"name":"Time","enabled":true,"inLoop":true,"slot":0,"present":true,"origin":"builtin","config":true},
  {"name":"Temperature","enabled":true,"inLoop":true,"slot":1,"present":true,"origin":"builtin","config":true},
  {"name":"weather","enabled":true,"inLoop":true,"slot":2,"present":true,"origin":"pushed","icon":"sun"},
  {"name":"co2","enabled":true,"inLoop":false,"slot":3,"present":false,"origin":null},
  {"name":"Date","enabled":false,"inLoop":false,"slot":null,"present":true,"origin":"builtin","config":true}
]
```
<!-- /only -->
<!-- only tc002 -->
```json
[
  {"name":"Time","enabled":true,"inLoop":true,"slot":0,"present":true,"origin":"builtin","config":true},
  {"name":"weather","enabled":true,"inLoop":true,"slot":1,"present":true,"origin":"pushed","icon":"sun"},
  {"name":"co2","enabled":true,"inLoop":false,"slot":2,"present":false,"origin":null},
  {"name":"Status","enabled":false,"inLoop":false,"slot":null,"present":true,"origin":"builtin","config":false}
]
```
<!-- /only -->

What the fields mean:

- **`slot`**: the position in the rotation, starting at 0. Apps you have not arranged come last, with
  `slot: null`.
- **`enabled`**: whether the app is switched on.
- **`inLoop`**: whether the app is shown in the rotation. For most apps this is the same as `enabled`.
- **`present`**: whether the app exists right now. `co2` above is switched on and keeps its place,
  but nothing has sent it yet: typical for a pushed app after a restart.
- **`origin`**: `builtin`, `pushed` or `script`. `null` while the app is not there.

### Built-in apps

These apps come with the clock. You arrange them and switch them off like any other app.

<!-- only esp32 esp32-s3 -->
| App | Shows | Needs |
|---|---|---|
| `Time` | the clock, in one of the `timeMode` styles | – |
| `Date` | the current date | – |
| `Temperature` | thermometer icon and the measured temperature | a temperature sensor |
| `Humidity` | droplet icon and the measured humidity | a sensor that measures humidity |
| `Battery` | battery icon and charge in percent | a board with a battery connection |

Without the needed hardware, `Temperature`, `Humidity` and `Battery` do not exist. They are not in
the rotation, not in `GET /api/v1/apps`, and naming them in an order call does nothing. See
[Power & battery](power.md).
<!-- /only -->
<!-- only tc002 -->
| App | Shows |
|---|---|
| `Time` | a [clock with five faces](device-controls.md#the-clock) that also shows the date |
| `Status` | how the clock is doing. See [The Status app](device-controls.md#the-status-app) |

`Date`, `Temperature`, `Humidity` and `Battery` in an order call are ignored.
<!-- /only -->

<!-- only esp32 esp32-s3 -->
Example: take the Date app out of the rotation:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/Date/enabled \
  -H "Content-Type: application/json" -d 'false'
```
<!-- /only -->
<!-- only tc002 -->
Example: take the Status app out of the rotation:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/Status/enabled \
  -H "Content-Type: application/json" -d 'false'
```
<!-- /only -->

The change is immediate and is kept after a restart.

<!-- only esp32 esp32-s3 -->
Colors of the built-in apps and the clock and date styles are settings:
[Settings – Sensor apps](../reference/settings.md#sensor-apps) and
[Settings – Clock app](../reference/settings.md#clock-app).
<!-- /only -->
<!-- only tc002 -->
The clock's faces and colors are settings: [Settings – Clock app](../reference/settings.md#clock-app).
<!-- /only -->

### The weekday bar is not an app

The row of seven dashes under the clock is part of the clock<!-- only esp32 esp32-s3 --> (and the Date app)<!-- /only -->. It is not in the
rotation and cannot be moved. One setting controls it:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" -d '{"weekdayBar":{"show":false}}'
```

The same object sets the first day of the week (`startOnMonday`), the weekend days (`weekendDays`)
and the colors: [Settings – Weekday bar](../reference/settings.md#weekday-bar).

### The rotation keeps turning behind a notification {#the-loop-keeps-turning-behind-a-notification}

A notification covers the rotation, but does not stop it. The rotation keeps moving in the background.
When the notification ends, you usually see a different app than before. See
[Your first notification](notifications.md).

## Arrange the rotation {#reordering-switching-off-and-duplicating}

Put the apps in your order, switch some off, or show one more often than the rest.

In the web UI, on the **Apps** tab:

1. Drag an app by its **⠿** grip to its place.
2. Use the switch in its row to switch it off or on.
3. To show an app twice per round, pick **Duplicate** in its **⋯** menu.

Every change is saved at once. See [The web UI – Apps](../getting-started/web-ui.md#apps).

Over the API, one call arranges the rotation. `order` lists the apps in the order they are shown:

<!-- only esp32 esp32-s3 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","weather","Time","Date"],"disabled":[]}'
```

The result: clock → weather → clock again → date → back to the start.
<!-- /only -->
<!-- only tc002 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","weather","Time","Status"],"disabled":[]}'
```

The result: clock → weather → clock again → status → back to the start.
<!-- /only -->

The rules:

- **`disabled` is the complete list of switched-off apps.** Every app it does not name runs. For example
  <!-- only esp32 esp32-s3 -->`{"order":["Time","Date"],"disabled":["Battery"]}`<!-- /only --><!-- only tc002 -->`{"order":["Time","weather"],"disabled":["Status"]}`<!-- /only -->.
  A switched-off app stays installed. It still appears in `GET /api/v1/apps`, with `enabled: false`.
- **`disabled` is always required. `order` is optional.**
  Without `order`, the order stays as it is. An `order` without `disabled` is refused.
- **Switch a single app with its own call.** `PUT /api/v1/apps/<name>/enabled` with `false` or
  `true` leaves every other app as it is. A switched-off app keeps its place and returns to it.
- **Show an app more often by naming it twice.** Each copy gets its own `slot`. This gives the
  app more time on the display than the rest.
- **You can name apps that are not there yet.** This is not an error: it reserves a place for an
  app that arrives later.
- **New apps join by themselves.** An app that arrives after the order call is switched on, unless
  `disabled` names it. If `order` already named it, it takes that place. Otherwise it joins after
  the arranged apps.
- **Scripts without a display** (they only run in the background) can be named in `order` to keep
  them running. They never take a turn.

Over MQTT, publish the same body to `cmd/apps/order`, and `true` or `false` to
`cmd/apps/<name>/enabled`.

### The order is remembered across reboots

The order is stored automatically and restored after a restart. You set it once. There is nothing
else to do.

What is saved is two lists of **names**: the order, and the switched-off apps. An app that is not
there yet waits. When the app arrives, it takes its saved place. This works for scripts loaded at
startup and for pushed apps sent later by your automation. This is how a pushed app keeps its
position, even though the app itself is lost at every restart.

**Switched-off apps stay switched off**, pushed apps included. When your automation sends the app
again, it stays off.

While an app is missing, `GET /api/v1/apps` still lists it with `present: false`. You can move it and
switch it on or off before it comes back.

An app that you have **never** named in an order call has no reserved place. After a restart it is
gone until the next push, and then it joins at the end of the rotation. Name it in one order call and it
keeps its place from then on.

To go back to the default order, list the built-in apps with nothing switched off:

<!-- only esp32 esp32-s3 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","Date","Temperature","Humidity","Battery"],"disabled":[]}'
```
<!-- /only -->
<!-- only tc002 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","Status"],"disabled":[]}'
```
<!-- /only -->

## Change how long each app stays {#timing-and-switching}

Each app stays for 7 seconds, then the next one comes. In the web UI, change **Time per app** under
**Display → App rotation** and press **Save**.

Over the API, four settings control the rotation:

| Setting | Does | Default |
|---|---|---|
| `autoTransition` | `false` stops the rotation. It then moves only when you tell it to | `true` |
| `appDurationMs` | how long each app is shown | 7000 ms |
| `transitionDurationMs` | how long the animation between two apps takes | 1000 ms |
| `transitionEffect` | the animation between two apps | `Rain` |

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"appDurationMs":4000,"transitionEffect":"Ripple","transitionDurationMs":800}'
```

Upper and lower case do not matter in effect names. All animations:
[Visual reference – Transitions](../reference/visuals.md#transitions).

A pushed app can set its own time with `durationMs`.

## Switch apps by hand {#driving-the-loop-by-hand}

Jump to the next app, the previous one or a certain one. In the web UI, the **◀** and **▶** buttons
under the live picture on the **Dashboard** show the previous and the next app. Over the API:

```bash
# Next / previous app, with the animation
curl -X POST http://<awtrix-ip>/api/v1/apps/next
curl -X POST http://<awtrix-ip>/api/v1/apps/previous

# Go to an app, with the animation
curl -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H "Content-Type: application/json" -d '{"name":"weather"}'

# Go to an app at once, without animation; its display time starts again
curl -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H "Content-Type: application/json" -d '{"name":"weather","fast":true}'
```

`previous` goes back one app. After that the rotation moves forward again. `apps/active` works only
for an app in the rotation: a switched-off app, or one without a display, answers
`404 app not found`.

Over MQTT: `cmd/apps/next`, `cmd/apps/previous` and `cmd/apps/switch`. See
[MQTT command topics](../reference/mqtt.md#command-topics).

## Switch apps with the buttons {#buttons}

On the clock, left shows the previous app and right the next one. One press on select dismisses the
notification shown. Everything the buttons do: [The buttons](device-controls.md#the-buttons). The
clock has no settings menu: all settings are in the web UI or the API.

### Lock the buttons {#locking-the-buttons}

For public places, lock the buttons. In the web UI, switch on **Block buttons** under
**Display → App rotation** and press **Save**. Over the API, set `blockNavigation` (default
`false`):

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" -d '{"blockNavigation":true}'
```

This blocks left, right, the double press and the [menu](device-controls.md#the-menu)<!-- only tc002 -->,
and everything the knob does on the clock<!-- /only -->. One press on select still dismisses a notification.
HTTP and MQTT keep working: `apps/next`, `apps/previous` and `apps/active` still switch apps.

## Good to know

- **Send `Content-Type: application/json`.** Without it, `curl -d` sends another type, and the
  request is refused with `415`. Your app stays as it was.
- **An empty body does not delete.** `{}` is refused with `422`. Use `DELETE` to remove an app.
- **`repeat` inside `scroll` is refused** with `422` and `"field":"scroll.repeat"`. Put it next to
  `text`.
- **The clock holds up to 50 pushed apps.** A new one past that is refused with
  `507 insufficientStorage`, so delete one first. Updating an app that exists always works, and
  scripts do not count.
- **A built-in app cannot be deleted.** `DELETE` answers `200`, but the app stays: switch it off
  in the rotation instead.

## Details

- Every key a pushed app takes, with types, ranges and defaults:
  [App & notification payload](../reference/payload.md). Pushed apps and notifications take the
  same keys.
- [`repeat`](../reference/payload.md#repeat),
  [`lifetimeMs` and `lifetimeExpiry`](../reference/payload.md#lifetimems-and-lifetimeexpiry) and
  [Array payloads](../reference/payload.md#array-payloads)
- Creating, replacing, deleting and names: [HTTP API – Apps](../reference/http.md#apps)
- Every field of the list: [HTTP API – GET /api/v1/apps](../reference/http.md#get-apiv1apps)
- All rules of the order call: [HTTP API – PUT /api/v1/apps/order](../reference/http.md#put-apiv1appsorder)
- Display time and transitions: [Settings – App rotation](../reference/settings.md#app-rotation)
- How many apps fit: [Limits](../reference/limits.md#apps-and-notifications)
- The same commands over MQTT: [MQTT command topics](../reference/mqtt.md#command-topics)
- Colors, effects, palettes, overlays and transitions: [Visual reference](../reference/visuals.md)

## Related

<!-- only esp32 esp32-s3 -->
- [Coming from AWTRIX 3](migrating-from-awtrix3.md): turn your old custom apps into pushed apps
<!-- /only -->
- [Your first notification](notifications.md): a message on top of the rotation
- [Text & colors](text.md), [Charts & drawing](graphics.md)<!-- only tc002 -->, [Layouts](layouts.md)<!-- /only -->
  and [Effects & overlays](effects.md): what an app can show
- [Scripting guide](scripting/index.md): apps that work out their own content on the clock
- [The web UI – Apps](../getting-started/web-ui.md#apps): the rotation in the web UI
