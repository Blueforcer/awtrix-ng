# Migrating from AWTRIX 3

On AWTRIX 3 you pushed **custom apps**; here the same thing is called a **[pushed
app](pushed-apps.md)**. The idea is unchanged - your automation sends a JSON object, AWTRIX puts a
page for it into the app loop and keeps showing it until you update or remove it. What changed is
the address you send to, the names of the keys, and how strictly the payload is checked.

This page walks you through converting an existing custom app. Three changes cover almost
everything:

1. **New endpoint.** `POST /api/custom?name=x` became `PUT /api/v1/apps/pushed/x`.
2. **New key names.** Everything is `camelCase`, durations are **milliseconds** with an `...Ms`
   suffix, and numeric mode switches became words - `lifetimeMode: 1` is now
   `lifetimeExpiry: "mark"`.
3. **Strict validation.** A key AWTRIX does not know is no longer ignored - the whole payload is
   rejected with `422` and the name of the offending key. Your old payload will not half-work; it
   will tell you exactly what still needs renaming.

!!! tip "Migrate by error message"
    Point your old payload at the new endpoint and read the `field` in each `422` response. Rename
    that key using the [table below](#the-key-map), send again, repeat. When the device answers
    `200`, the payload is fully migrated - nothing is ever silently dropped.

---

## Where to send

=== "HTTP"

    | | AWTRIX 3 | AWTRIX NG |
    |---|---|---|
    | Create / update | `POST /api/custom?name=weather` | `PUT /api/v1/apps/pushed/weather` |
    | Delete | `POST /api/custom?name=weather` with empty body | `DELETE /api/v1/apps/weather` |
    | Notification | `POST /api/notify` | `POST /api/v1/notifications` |
    | Dismiss notification | `POST /api/notify/dismiss` | `DELETE /api/v1/notifications/active` |
    | Next / previous app | `POST /api/nextapp` · `/api/previousapp` | `POST /api/v1/apps/next` · `/api/v1/apps/previous` |
    | Switch to an app | `POST /api/switch` | `PUT /api/v1/apps/active` with `{"name":"weather"}` |

    The app name moved from the query string into the URL path, and it must match
    `[A-Za-z0-9_-]{1,32}`.

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
      -H "Content-Type: application/json" \
      -d '{"text":"21.5C","icon":"2422"}'
    ```

    Two habits from AWTRIX 3 no longer work over HTTP:

    * **An empty body is not a delete.** `PUT` with no body or `{}` is a validation error; removing
      an app is its own call, `DELETE /api/v1/apps/{name}`.
    * **The `Content-Type: application/json` header is required.** Without it the request fails
      with `415` or `400` before anything is read - `curl -d` alone does not send it
      ([Conventions](../reference/conventions.md#content-type-is-mandatory)).

=== "MQTT"

    | | AWTRIX 3 | AWTRIX NG |
    |---|---|---|
    | Create / update | `[prefix]/custom/weather` | `<prefix>/cmd/apps/pushed/weather` |
    | Delete | empty payload to the same topic | empty payload to the same topic |
    | Notification | `[prefix]/notify` | `<prefix>/cmd/notify` |
    | Next / previous / switch | `[prefix]/nextapp` … | `<prefix>/cmd/apps/next` · `.../previous` · `.../switch` |

    The delete-by-empty-payload convention survives on MQTT, so an automation that clears an app by
    publishing nothing keeps working once the topic is renamed. The full topic tree is in
    [MQTT topics](../reference/mqtt.md).

---

## Before and after

The same weather app, in both dialects:

=== "AWTRIX 3"

    ```json
    {
      "text": "21.5 C",
      "icon": "2422",
      "color": "#00AAFF",
      "duration": 8,
      "lifetime": 300,
      "lifetimeMode": 1,
      "pushIcon": 2,
      "scrollSpeed": 50
    }
    ```

=== "AWTRIX NG"

    ```json
    {
      "text": "21.5 C",
      "icon": "2422",
      "textColor": "#00AAFF",
      "durationMs": 8000,
      "lifetimeMs": 300000,
      "lifetimeExpiry": "mark",
      "iconMode": "push",
      "scroll": { "speed": 50 }
    }
    ```

Same content, same icon, same behaviour - every value found a new home. Colors themselves carry
over unchanged: hex strings and `[r, g, b]` arrays are both still accepted
([color forms](../reference/conventions.md#colors)).

---

## The key map

Every AWTRIX 3 custom-app key, and where it went. The full definition of each new key is in
[App & notification payload](../reference/payload.md).

### Text and styling

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `text` | `text` | Unchanged, string or fragment array |
| `text` fragments `{"t": …, "c": …}` | `{"text": …, "color": …}` | Keys are spelled out |
| `textCase` `0` / `1` / `2` | `textCase` `"inherit"` / `"upper"` / `"asTyped"` | Number → word |
| `topText` | - | No equivalent; place text freely with a `draw` [`text` command](../reference/payload.md#draw-commands) |
| `textOffset` | `textOffsetX` | Rename |
| `center` | `textCenter` | Rename |
| `color` | `textColor` | Rename; charts now have their own `chartColor` |
| `gradient` | `palette` + `textColor: "palette"` | See [gradients and rainbow](#gradients-rainbow-blink-and-fade) |
| `blinkText` | `textBlinkMs` | Rename, still milliseconds |
| `fadeText` | `textFadeMs` | Rename, still milliseconds |
| `rainbow` | `palette: "Rainbow"` + `textColor: "palette"` | See [gradients and rainbow](#gradients-rainbow-blink-and-fade) |
| `background` | `backgroundColor` | Rename |
| `noScroll` | `scroll: {"mode": "static"}` | See [the scroll object](#four-keys-became-one-scroll-object) |
| `scrollSpeed` | `scroll: {"speed": N}` | Still percent of the base rate |

### Icon

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `icon` | `icon` | Unchanged - icon ID, or inline base64 for JPEG and GIF |
| `pushIcon` `0` / `1` / `2` | `iconMode` `"fixed"` / `"pushOnce"` / `"push"` | Number → word |

### Timing and lifetime

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `duration` (seconds) | `durationMs` (milliseconds) | Multiply by 1000 |
| `lifetime` (seconds) | `lifetimeMs` (milliseconds) | Multiply by 1000 |
| `lifetimeMode` `0` / `1` | `lifetimeExpiry` `"remove"` / `"mark"` | Number → word |
| `repeat` | `repeat` | Kept; `0` turns it off where AWTRIX 3 wrote `-1` |
| `pos` | `PUT /api/v1/apps/order` | Position is no longer a payload key - see [`pos`](#pos-became-the-order-call) |

### Charts and progress bar

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `bar` | `barChart` | Rename |
| `line` | `lineChart` | Rename |
| `autoscale` | `chartAutoscale` | Rename |
| `barBC` | - | No equivalent; unfilled chart cells show the app background |
| `progress` | `progress` | Unchanged, 0-100, below 0 = off |
| `progressC` | `progressColor` | Rename |
| `progressBC` | `progressTrackColor` | Rename |

### Effects and overlay

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `effect` | `effect` | Kept, but the set of names differs - ask `GET /api/v1/capabilities`, browse [Background effects](../reference/visuals.md#background-effects) |
| `effectSettings.speed` | `effectSpeed` | Now a 0.1-10.0 multiplier of the normal pace |
| `effectSettings.palette` | `palette` | Now a top-level key the whole app shares |
| `effectSettings.blend` | `paletteBlend` | Rename |
| `overlay` | `overlay` | Same six weather names; `"clear"` became `""`, which inherits the global overlay |

### Drawing

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `draw` with command objects | `draw` with command arrays | See [draw commands](#draw-commands-arrays-instead-of-objects) |

### Notification-only keys

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `hold` | `hold` | Unchanged |
| `stack` | `stack` | Unchanged |
| `wakeup` | `wakeup` | Unchanged |
| `sound` | `sound` | Unchanged - melody file name or DFPlayer track |
| `rtttl` | `soundRtttl` | Rename |
| `loopSound` | `soundLoop` | Rename |
| `clients` | - | No equivalent; have your automation send to each device itself |

### Persistence

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `save` | - | Pushed apps are RAM-only by design - see [`save`](#save-is-gone-scripts-took-its-place) |

---

## The tricky ones, explained

### Gradients, rainbow, blink and fade

AWTRIX 3 had four competing text stylings - `gradient`, `rainbow`, `blinkText`, `fadeText` - that
excluded one another. Gradient and rainbow merged into one mechanism, the app
[palette](../reference/payload.md#palette):

```json title="gradient: [c1, c2] becomes"
{ "text": "OVERHEAT", "palette": ["#FFFF00", "#FF0000"], "textColor": "palette" }
```

```json title="rainbow: true becomes"
{ "text": "PARTY", "palette": "Rainbow", "textColor": "palette" }
```

The palette does more than the old keys did - it can hold up to 16 stops, repeat and move along the
text (`paletteSpan`, `paletteSpeed`), and the same ramp also colors charts, the progress bar and
the background effect. Blink and fade stayed what they were, as `textBlinkMs` and `textFadeMs`.
When both a palette and a blink/fade are set, the palette wins - the
[precedence table](../reference/payload.md#which-color-wins) has the exact order.

### Four keys became one `scroll` object

`noScroll`, `scrollSpeed` and the fixed scroll behaviour are now one
[`scroll` object](../reference/payload.md#scrolling) with modes AWTRIX 3 did not have - `wrap`,
`loop`, `bounce`, direction, off-screen entry and a hold time:

```json
{ "text": "A LONG HEADLINE", "scroll": { "mode": "bounce", "speed": 50 } }
```

The two direct translations: `"noScroll": true` → `"scroll": {"mode": "static"}`, and
`"scrollSpeed": 50` → `"scroll": {"speed": 50}`. Every field you leave out inherits from the global
scroll setting.

### Draw commands: arrays instead of objects

Each command is now an array that names the command first, instead of an object keyed by a
two-letter code:

=== "AWTRIX 3"

    ```json
    { "draw": [
      { "dp": [28, 4, "#FF0000"] },
      { "dr": [20, 2, 4, 4, "#0000FF"] },
      { "dt": [0, 0, "Hi", "#00FF00"] }
    ] }
    ```

=== "AWTRIX NG"

    ```json
    { "draw": [
      ["pixel", 28, 4, "#FF0000"],
      ["rect", 20, 2, 4, 4, "#0000FF"],
      ["text", 0, 0, "Hi", "#00FF00"]
    ] }
    ```

The codes map one to one: `dp` → `pixel`, `dl` → `line`, `dr` → `rect`, `df` → `rectFill`,
`dc` → `circle`, `dfc` → `circleFill`, `dt` → `text`, `db` → `bitmap`. There is also a new
`pixels` command for many dots of one color. Arguments and clipping rules:
[Draw commands](../reference/payload.md#draw-commands).

### `pos` became the order call

The experimental `pos` key is gone. The loop order is set once, for all apps, with a single call -
and unlike `pos` it is stored on the device and survives reboots:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","weather","Date"],"disabled":[]}'
```

A name in the order that does not exist yet simply waits, so your daily pushed app lands in its
slot the moment the automation sends it. Switching off is its own `disabled` list beside `order`, and
is required with it. Everything the order call can do - switching off, duplicates for extra screen
time - is in [Pushed apps → Reordering](pushed-apps.md#reordering-switching-off-and-duplicating).

### `save` is gone - scripts took its place

`save: true` wrote a custom app to flash so it survived a reboot. Pushed apps here are deliberately
RAM-only: after a restart your automation pushes again and the app comes back with *current* data
instead of a stale stored copy ([why](pushed-apps.md#a-pushed-app-lasts-until-awtrix-restarts)).

For content that should come back **by itself** - a label, a logo, anything needing no outside
data - write a [script](scripting.md): a small program stored on the device that generates its own
content. That is also the successor to AWTRIX 3's MQTT-placeholder `.json` files - a script can
[subscribe to MQTT topics](scripting.md) and render the values however it likes, where the old
placeholders allowed no formatting at all.

---

## Checks that are stricter than you remember

AWTRIX 3 accepted almost anything and quietly skipped what it did not understand. Here the rule is
**all or nothing**: the whole payload is applied, or the whole payload is rejected and nothing
changes. The upside is that every rejection names the culprit:

```json
{ "error": { "code": "validationFailed", "message": "unknown key", "field": "pushIcon" } }
```

What most commonly trips a freshly ported payload:

* **An old key name** - `422`, with the AWTRIX 3 key in `field`. Rename it per the
  [table above](#the-key-map).
* **A missing `Content-Type: application/json` header** - `415` or `400`
  ([why both](../reference/conventions.md#content-type-is-mandatory)).
* **A notification key on a pushed app** - `hold`, `stack`, `wakeup` and the sound keys are
  [notification-only](../reference/payload.md#notification-only-keys), same as before, but now
  sending one to an app is an error instead of a no-op.
* **A payload over 8192 bytes** - `413`; base64-encode bitmaps and see
  [keeping payloads small](graphics.md#keeping-payloads-small).

The complete rule set is under [payload → Errors](../reference/payload.md#errors).

---

## Migration checklist

1. **Rewrite the endpoint** - `POST /api/custom?name=x` → `PUT /api/v1/apps/pushed/x`, or the MQTT
   topic `custom/x` → `cmd/apps/pushed/x`.
2. **Add the `Content-Type: application/json` header** to every HTTP request.
3. **Rename the keys** using [the key map](#the-key-map).
4. **Multiply `duration` and `lifetime` by 1000** - all durations are milliseconds now.
5. **Turn numbers into words** - `textCase`, `pushIcon`→`iconMode`, `lifetimeMode`→`lifetimeExpiry`.
6. **Convert `draw` objects to arrays**, if you draw.
7. **Replace deletes** - over HTTP an empty body no longer removes an app; use
   `DELETE /api/v1/apps/{name}`.
8. **Re-create persistence** - drop `save`, push on a schedule or move the app to a
   [script](scripting.md); set the rotation once with `PUT /api/v1/apps/order`.
9. **Send it and read the errors** - each `422` names the next key to fix.

## Your panel type

AWTRIX 3 picked the panel with a single number, `MATRIX_LAYOUT` 0, 1 or 2. Here you describe the
panel by its parts, in the web UI under **Settings → Panel**. Find your old number in the first
column and set the fields next to it:

| AWTRIX 3 | What that is | Panel width | Panels | First LED | Wiring direction | Serpentine |
|---|---|---|---|---|---|---|
| `0` - the default, Ulanzi TC001 | one 32×8 panel, every second row backwards | `32` | `1` | Top left | Along the rows | on |
| `1` | four 8×8 tiles side by side | `8` | `4` | Top left | Along the rows | off |
| `2` | one 32×8 panel wired downwards | `32` | `1` | Top left | Along the columns | on |

Leave **Mirror** and **Rotate 180°** off - they are about the picture, not the cable, and AWTRIX 3
had no equivalent. The **Matrix size** line above the fields does the sum while you type; it should
read `32 × 8 = 256 LEDs` for all three.

Everything except the total width takes effect on the next frame, so you can watch the panel while
you switch **Serpentine** on and off. Changing `Panel width × Panels` needs a reboot.

!!! tip "If layout 1 comes out scrambled"
    Some 8×8 tiles are wired in a zigzag inside the tile, which AWTRIX 3 could not express. Turn
    **Serpentine** on and keep everything else - that combination is available here.

!!! tip "If your matrix is none of the three rows above"
    A self-built panel is often wired in a way no single `MATRIX_LAYOUT` value could describe, so
    there was no row to copy. Two more switches cover those builds: **Reverse chain** if each
    panel on its own looks right but the panels sit in the wrong order, and **Alternating panels**
    if every second panel is upside down. Four 8×8 tiles each wired from their right edge, for
    example, is **First LED** top right plus **Reverse chain** on.

The same three, over the API:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/system \
  -H "Content-Type: application/json" \
  -d '{"panelWidth":8,"panels":4,"panelStart":"topLeft","panelWiring":"rows","panelSerpentine":false}'
```

Every field, with ranges: [Panel and orientation](../reference/system.md#panel-and-orientation).

## Beyond apps

* **Device settings do not import** - set the device up fresh; battery calibration in particular
  works differently ([Coming from an AWTRIX 3 device](../reference/system.md#coming-from-an-awtrix-3-device)).
* **Home Assistant** - automations built on the AWTRIX 3 endpoints need the same endpoint and key
  changes; see [Home Assistant](home-assistant.md) for working examples.
* **Icons** - the same 8×8 icon IDs and files work; see [Icons & assets](icons.md).

## Related

* [Pushed apps](pushed-apps.md) - the full guide to what custom apps became
* [App & notification payload](../reference/payload.md) - every key, exactly specified
* [Visual reference](../reference/visuals.md) - colors, effects, palettes, overlays
* [App scripting](scripting.md) - self-contained apps that survive reboots
