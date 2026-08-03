# Pushed apps

AWTRIX shows a **loop** of apps: it displays one, waits, slides to the next, and wraps around
forever. Out of the box the loop holds only the built-in apps. This guide is about putting *your*
apps into it - a stock ticker, a train departure, the state of your washing machine - and then
deciding what the loop looks like: what is in it, in what order, and for how long.

A **pushed app** is content something outside AWTRIX sends in and keeps refreshing; a
**[script](scripting.md)** is a program that lives on it and works out its own content. If
the number on screen comes from Home Assistant, a cron job or a NAS, you want a pushed app - this
page. If it should keep going with nothing else on the network, you want a script.

Paste this and you have a pushed app:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H "Content-Type: application/json" \
  -d '{"text":"21.5C","icon":"2422","textColor":"#00AAFF"}'
```

`weather` is the last stop in the rotation. Wait for the loop to come around to it, or jump
straight there:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H "Content-Type: application/json" -d '{"name":"weather"}'
```

---

## Push, update, delete

A pushed app is a **name** plus a **payload**. The name is the path tail of the URL, the payload is
the JSON body. There is no "create" versus "update": a `PUT` on a name that already exists replaces
it wholesale.

=== "Create or replace"

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
      -H "Content-Type: application/json" \
      -d '{"text":"18.0C","icon":"2422"}'
    ```

=== "Delete"

    ```bash
    curl -X DELETE http://<awtrix-ip>/api/v1/apps/weather
    ```

Send the body as `Content-Type: application/json` - anything else is rejected with `415`, leaving
your app as it was ([Conventions - Content-Type](../reference/conventions.md#content-type-is-mandatory)).

Deleting is addressed to the app itself, not to the sub-collection: `DELETE /api/v1/apps/{name}`
removes whatever that name is - a pushed app or a script. Deleting a name you never created answers
`200` just the same, as does naming a built-in, which stays put.

Replacing an app that is on screen takes effect at once: text that only now overflows starts
scrolling and a changed `icon` is reloaded. Pushing the same text again leaves the scroll where it
is, so an app you refresh on a timer keeps advancing instead of restarting on every update, and an
unchanged animated icon keeps playing rather than jumping back to its first frame.

An empty body or the literal `{}` on `PUT` is **not** a delete - it is a validation error
([HTTP API](../reference/http.md#put-apiv1appspushedname)). Over MQTT there is no `DELETE` verb, so there publishing an empty payload to
`<prefix>/cmd/apps/pushed/weather` *is* the way to remove an app
([MQTT command topics](../reference/mqtt.md#command-topics)).

### Names

A name must match `[A-Za-z0-9_-]{1,32}`; the same rule applies to scripts. See
[Names](../reference/http.md#names) for the exact error.

### What goes in the payload

Everything: text and colored fragments, icons, background effects, bar and line charts, a progress
bar, raw draw commands, scroll behaviour. Pushed apps and notifications take the same keys - the
complete field table is in
[App & notification payload](../reference/payload.md), and the visual names (colors, effects,
palettes, overlays) in [Visual reference](../reference/visuals.md).

---

## A pushed app lasts until AWTRIX restarts

A pushed app lives in RAM. It stays there until you replace it, you delete it, its `lifetimeMs`
runs out, or AWTRIX reboots - a power cut, a firmware update, `POST /api/v1/device/reboot`.

After a reboot the sender pushes again and the app is back, showing the *current* number rather
than the one it had before the outage. Push on a schedule, or key your automation to the MQTT
availability topic ([MQTT automation](mqtt.md)), and the loop repairs itself.

The one thing that *is* remembered is the **order** you arranged - see
[The order is remembered across reboots](#the-order-is-remembered-across-reboots). A pushed app that
reappears every morning returns to the slot you gave it.

For content that should come back by itself - a static label, a drawn logo, anything that needs no
outside data - write a [script](scripting.md) instead. A script's source is stored on AWTRIX and it
works out what to show rather than waiting to be told.

---

## Apps that expire by themselves

`lifetimeMs` gives an app an expiry date, and `lifetimeExpiry` decides what expiry means - the
exact semantics of both keys live under [payload - `lifetimeMs` and
`lifetimeExpiry`](../reference/payload.md#lifetimems-and-lifetimeexpiry).

```bash
# Vanish 5 minutes after it was pushed
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/doorbell \
  -H "Content-Type: application/json" \
  -d '{"text":"DING","lifetimeMs":300000,"lifetimeExpiry":"remove"}'
```

That makes `mark` a dead-man's switch: push every minute with `lifetimeMs: 180000`, and if your
automation dies the app grows a red frame instead of showing a three-hour-old number as if it were
current.

---

## One request, many apps

If the body is an **array** of objects, each element becomes its own app, named after the base name
plus an index - `stocks0`, `stocks1`, `stocks2`, …

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/stocks \
  -H "Content-Type: application/json" \
  -d '[{"text":"AAPL 189"},{"text":"MSFT 412"},{"text":"NVDA 903"}]'
```

Those three names are what appear in the loop and in `GET /api/v1/apps` - `stocks` itself never
exists as an app. The exact rules - skipped elements, all-or-nothing validation, deleting the whole
family via the base name - are under [Array payloads](../reference/payload.md#array-payloads).

---

## The 50-app cap

AWTRIX holds at most **50** pushed apps at a time. Updating an app you already pushed never counts
against the cap - see [Limits](../reference/limits.md#apps-and-notifications) for exactly what
happens at the edge.

Scripts are counted separately, against [`scriptLimit`](../reference/system.md#miscellaneous).

---

## The app loop

Every app in the loop - built-in, pushed or script - is just a name. The loop is an ordered list of
those names, and AWTRIX walks it forever.

Ask what it looks like right now:

```bash
curl http://<awtrix-ip>/api/v1/apps
```

```json
[
  {"name":"Time","inLoop":true,"slot":0,"present":true,"origin":"builtin"},
  {"name":"Temperature","inLoop":true,"slot":1,"present":true,"origin":"builtin"},
  {"name":"weather","inLoop":true,"slot":2,"present":true,"origin":"pushed","icon":"2422"},
  {"name":"clock","inLoop":true,"slot":3,"present":true,"origin":"script","skipped":false,
   "error":null,"meta":{"name":"Wall Clock","desc":"","author":"me","version":"1.2"}},
  {"name":"co2","enabled":true,"inLoop":false,"slot":4,"present":false,"origin":null},
  {"name":"Date","enabled":false,"inLoop":false,"slot":null,"present":true,"origin":"builtin"}
]
```

Apps you arranged come first, in that order, each with a 0-based `slot`; everything else follows with
`slot: null`. `enabled` says whether an app runs at all, which for most apps is the same answer as
`inLoop`. `present` says whether the app is there right now - `co2` above is switched on and keeping
its place while nothing is sending it, which is what a pushed app looks like between a reboot and the
next push. `origin` says where the content comes from - `builtin`, `pushed` or `script`, and `null`
while there is nothing there to ask. Full field table:
[HTTP API - GET /api/v1/apps](../reference/http.md#get-apiv1apps).

### Built-in apps

Five apps ship with AWTRIX, and they are ordinary members of the rotation - no setting turns one
on or off, you order it in or leave it out like any other app.

| App | Shows | Needs |
|---|---|---|
| `Time` | the clock, in one of the `timeMode` styles | - |
| `Date` | the current date | - |
| `Temperature` | thermometer icon + the measured temperature | a detected I²C sensor |
| `Humidity` | droplet icon + the measured humidity | a sensor with a humidity element |
| `Battery` | battery icon + charge percent | a board with a battery pin |

The last three are the only builtins that can be missing. Without the hardware there is no reading to
show, so the app is not created: it appears neither in the loop nor in `GET /api/v1/apps`, and
naming it in an order call does nothing. See [Power & battery](power.md).

When no explicit order has ever been set, the loop runs **Time, Date, Temperature, Humidity,
Battery**, then your pushed apps in alphabetical order by name (uppercase before lowercase, so
`Zebra` sorts before `apple`), then your scripts in the order they were installed. To keep the date
out of the rotation, name it in `hidden`:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","Temperature","Humidity","Battery"],"hidden":["Date"]}'
```

That takes effect immediately - no reboot - and survives one.

The per-app colors and the clock/date styles are settings, not payloads:
[Settings - Sensor apps](../reference/settings.md#sensor-apps) and
[Settings - Clock app](../reference/settings.md#clock-app).

### The weekday bar is not an app

The row of seven dashes under the clock is a **decoration**, drawn by the Time and Date apps
themselves. It has no entry in the loop and cannot be reordered or given a position. One global
setting controls it everywhere it appears:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" -d '{"weekdayBar":{"show":false}}'
```

The same object decides where the week starts (`startOnMonday`), which days count as weekend
(`weekendDays`) and the four segment colors:
[Settings - Weekday bar](../reference/settings.md#weekday-bar).

---

## Reordering, switching off and duplicating

One call arranges the loop. `order` is what runs, in the order it draws:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","weather","Time","Date"],"hidden":[]}'
```

That gives you: clock → weather → clock again → date → back to the start.

Things that follow from that:

* **Switching off has its own list.** Send `hidden` beside `order` and it names exactly what is off:
  `{"order":["Time","Date"],"hidden":["Battery"]}`. A switched-off app stays installed and keeps
  appearing in `GET /api/v1/apps` with `enabled: false`.
* **An app in neither list keeps what it had.** Name only what you want to change.
* **`hidden` is always required**, `order` is optional. `{"hidden":["Battery"]}` switches Battery off
  and leaves your arrangement as it was - you do not have to resend the whole loop to flip one app.
  An `order` without `hidden` beside it is refused.
* **A headless script is named like anything else.** Put it in `order` to keep it running. It never
  takes a place, because it never draws.
* **Duplicates are kept.** Naming an app twice makes it rotate twice per cycle, each
  instance with its own `slot`. This is how you give the clock more screen time than the rest.
* **Later arrivals join automatically.** An app that turns up *after* the order call is switched on
  unless `hidden` names it. If your list already named it, it takes that place the moment it appears;
  otherwise it joins after your ordered entries.
* **Naming an app that is not there is not an error** - it is how you hold a place for one that
  arrives later.

The full rules are under [HTTP API - PUT
/api/v1/apps/order](../reference/http.md#put-apiv1appsorder). Over MQTT it is `cmd/apps/order`, with
the same body.

### The order is remembered across reboots

The arrangement you send is written to flash and restored at boot. You set the loop once and it
survives power cuts - there is no separate "save the order" call, and no setting to enable.

What is saved is two lists of **names**: what runs and in what order, and what is switched off. A
name whose app is not there right now is simply waiting - when that app turns up, a script restored
during the boot or a pushed app your automation sends at 7am, it lands in the place the list gave it.
That is what keeps a daily pushed app in its spot even though the app itself does not survive the
reboot.

**Switching off is remembered the same way, pushed apps included.** The name stays on the off-list,
so the app stays off when your automation pushes it again.

While an app is away its name is still listed by `GET /api/v1/apps`, marked `present: false`, and you
can arrange it and switch it before it comes back. An app that was **never** named in an order call
has nothing holding a place for it: it lives only in RAM, so after a reboot it is gone until the next
push, and then it joins at the end of the loop. Name it in one order call and it keeps its place from
then on.

To go back to the default sequence, send an order call that lists exactly what you want, with nothing
switched off:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","Date","Temperature","Humidity","Battery"],"hidden":[]}'
```

---

## Timing and switching

Four settings govern the rhythm of the loop. `autoTransition` turns automatic advance on and off -
with `false` the loop freezes until something moves it by hand. `appDurationMs` is how long each app
is shown, 7000 ms out of the box. `transitionDurationMs` and `transitionEffect` are the length and
the style of the animation between two apps. Ranges and defaults:
[Settings - App rotation](../reference/settings.md#app-rotation).

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"appDurationMs":4000,"transitionEffect":"Ripple","transitionDurationMs":800}'
```

Effect names are matched case-insensitively; the full list is in
[Visual reference - Transitions](../reference/visuals.md#transitions).

A single pushed app can override the global dwell for itself with a `durationMs` of its own. Two
further behaviours are worth knowing:

* **A one-app loop never transitions.** Rotation needs at least two entries. With a single app the
  AWTRIX sits on it regardless of `autoTransition`, and `appDurationMs` appears to do nothing.
* **`appDurationMs: 0` is accepted.** The dwell then elapses on every tick - continuous transitions.
  It is also the fallback duration for any pushed app or notification that sets no `durationMs` of
  its own, so `0` affects both.

### Holding an app until its text has finished

Long text scrolls, but the loop does not wait for it: the app hands over when its dwell is up,
wherever the text has got to. `repeat` asks for whole passes instead:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H "Content-Type: application/json" \
  -d '{"text":"A rather long headline that will not fit on the matrix","repeat":1}'
```

The app then stays exactly as long as those passes take: one whose text is read before the normal
app time is up hands over there and then. Give it a `durationMs` if it should stay longer, or a
higher `repeat` for more passes.

The full `repeat` contract - the default, text that does not move, a notification releasing the
hold - is under [payload - `repeat`](../reference/payload.md#repeat).

### Driving the loop by hand

```bash
# Next / previous, with the transition animation
curl -X POST http://<awtrix-ip>/api/v1/apps/next
curl -X POST http://<awtrix-ip>/api/v1/apps/previous

# Go straight to an app, animated
curl -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H "Content-Type: application/json" -d '{"name":"weather"}'

# Go straight to an app, instantly - no animation, and the dwell timer restarts
curl -X PUT http://<awtrix-ip>/api/v1/apps/active \
  -H "Content-Type: application/json" -d '{"name":"weather","fast":true}'
```

`next` and `previous` always answer `200`, even when there are fewer than two apps and nothing
happens. `apps/active` answers `404 app not found` for a name that is not **in the loop** - an app
that does not draw cannot be switched to, whether it is disabled or headless. `fast: false` (the
default) plays the transition;
`fast: true` jumps and restarts the dwell, so the app gets a full `appDurationMs` from that moment.

A `previous` runs one transition backwards; forward rotation resumes afterwards. All of these have
MQTT equivalents (`cmd/apps/next`, `cmd/apps/previous`, `cmd/apps/switch`): [MQTT command
topics](../reference/mqtt.md#command-topics).

---

## Buttons

The three physical buttons drive the loop directly:

| Button | Action |
|---|---|
| left | previous app |
| right | next app |
| select | dismiss the showing notification |
| select, twice within 300 ms | toggle the matrix on/off |

If the screen is rotated (or `swapButtons` is set), left and right swap - but both together cancel
out. There is no on-device menu: every setting lives in the web UI or the API.

### Locking the buttons

`blockNavigation` (boolean, default `false`) is for kiosk-style installs:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" -d '{"blockNavigation":true}'
```

It blocks left/right navigation and the double-press power toggle. A single press of select still
dismisses a showing notification, so a passer-by can clear one. HTTP and MQTT are unaffected:
`apps/next`, `apps/previous` and `apps/active` keep working.

---

## The loop keeps turning behind a notification

A notification paints over the loop; it does not pause it. Rotation keeps advancing invisibly for as
long as the notification is up, so when it clears you are usually looking at a *different* app than
the one you left.

See [Your first notification](notifications.md) and
[payload - notification-only keys](../reference/payload.md#notification-only-keys).

---

## Related

* [Migrating from AWTRIX 3](migrating-from-awtrix3.md) - map your old custom-app payloads onto
  pushed apps, key by key
* [AWTRIX scripting](scripting.md) - the other way to own an app, for content AWTRIX works out
  by itself
* [App & notification payload](../reference/payload.md) - every field a pushed app accepts
* [Visual reference](../reference/visuals.md) - colors, effects, palettes, overlays, transitions
* [HTTP API - Apps](../reference/http.md#apps) - exhaustive endpoint reference
* [MQTT command topics](../reference/mqtt.md#command-topics) - the same commands over MQTT
* [Settings - App rotation](../reference/settings.md#app-rotation) - dwell time, transitions, clock
  and date styling
