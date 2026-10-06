---
only: [esp32, esp32-s3]
---

# Coming from AWTRIX 3

This page helps you move your AWTRIX 3 automations to AWTRIX NG.

On AWTRIX 3 you pushed **custom apps**. In AWTRIX NG they are called **[pushed
apps](pushed-apps.md)**. The idea is the same: your automation sends a JSON object, and AWTRIX
adds it to the rotation as an app and shows it until you update or remove it. What differs is the
address you send to, the names of the keys, and how strictly the payload is checked.

Three changes cover almost everything:

1. **New endpoint.** `POST /api/custom?name=x` became `PUT /api/v1/apps/pushed/x`.
2. **New key names.** Everything is `camelCase`, durations are **milliseconds** with an `...Ms`
   suffix, and numeric mode switches became words: `lifetimeMode: 1` is
   `lifetimeExpiry: "mark"`.
3. **Strict validation.** AWTRIX 3 ignored keys it did not know. AWTRIX NG rejects the whole
   payload with `422` and names the unknown key. Your old payload will not half-work. The error
   tells you exactly what still needs renaming.

!!! tip "Migrate by error message"
    Point your old payload at the new endpoint and read the `field` in each `422` response. Rename
    that key using the [table below](#the-key-map), send again, repeat. When the device answers
    `200`, the payload is fully migrated. Nothing is ever silently dropped.

---

## Convert a flow automatically

Paste an old flow and get it back in the NG dialect. A flow can be a Home Assistant automation or
blueprint, a Node-RED or n8n export, a `curl` command, or a bare JSON payload. Endpoints, MQTT
topics and payload keys are rewritten in place. Your comments, templates and formatting around
them stay as they are. Anything that cannot be converted safely is left untouched and listed as a
warning that links to the matching section below: JavaScript that builds payloads, template
expressions, and keys with no NG equivalent.

<div id="awtrix-flow-converter">
  <p>The converter needs JavaScript. The tables below cover the same ground by hand.</p>
</div>
<script type="module" src="../../assets/flow-converter/ui.js"></script>

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

    Two AWTRIX 3 habits do not work over HTTP in AWTRIX NG:

    * **An empty body is not a delete.** `PUT` with no body or `{}` is a validation error. To
      remove an app, call `DELETE /api/v1/apps/{name}`.
    * **Send `Content-Type: application/json`.** `curl -d` alone sends a form header instead, and
      the request fails with `415` or `400` before anything is read
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

Same content, same icon, same behavior: every value found a new home. Colors carry over
unchanged. Hex strings and `[r, g, b]` arrays both work
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
| `topText` | - | No equivalent. Place text freely with a `draw` [`text` command](../reference/payload.md#draw-commands) |
| `textOffset` | `textOffsetX` | Rename |
| `center` | `textAlign` | `true` → `"center"`, `false` → `"start"` |
| `color` | `textColor` | Rename. Charts have their own `chartColor` |
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
| `icon` | `icon` | Icon IDs unchanged. Inline base64 needs a prefix: `data:image/gif;base64,` or `data:image/jpeg;base64,` |
| `pushIcon` `0` / `1` / `2` | `iconMode` `"fixed"` / `"pushOnce"` / `"push"` | Number → word |

### Timing and lifetime

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `duration` (seconds) | `durationMs` (milliseconds) | Multiply by 1000 |
| `lifetime` (seconds) | `lifetimeMs` (milliseconds) | Multiply by 1000 |
| `lifetimeMode` `0` / `1` | `lifetimeExpiry` `"remove"` / `"mark"` | Number → word |
| `repeat` | `repeat` | Kept. `0` turns it off where AWTRIX 3 wrote `-1` |
| `pos` | `PUT /api/v1/apps/order` | Not a payload key in AWTRIX NG. See [`pos`](#pos-became-the-order-call) |

### Charts and progress bar

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `bar` | `barChart` | Rename |
| `line` | `lineChart` | Rename |
| `autoscale` | `chartAutoscale` | Rename |
| `barBC` | - | No equivalent. Unfilled chart cells show the app background |
| `progress` | `progress` | Unchanged, 0-100, below 0 = off |
| `progressC` | `progressColor` | Rename |
| `progressBC` | `progressTrackColor` | Rename |

### Effects and overlay

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `effect` | `effect` | Kept, but the set of names differs. Ask `GET /api/v1/capabilities`, or browse [Background effects](../reference/visuals.md#background-effects) |
| `effectSettings.speed` | `effectSpeed` | A 0.1-10.0 multiplier of the normal pace |
| `effectSettings.palette` | `palette` | A top-level key the whole app shares |
| `effectSettings.blend` | `paletteBlend` | Rename |
| `overlay` | `overlay` | Same six weather names. `"clear"` became `""`, which inherits the global overlay |

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
| `sound` | `sound` | A name stays a name: `"sound":"ding"`. NG looks for an uploaded MP3 first, then a melody file. A DFPlayer track is `"sound":{"track":3}`. See [Sound](sounds.md#play-a-sound) |
| `rtttl` | `sound` | Put the melody in a sound object: `"sound":{"rtttl":"bell:d=4,o=5,b=120:c,e,g"}` |
| `loopSound` | `loop` inside `sound` | `"loop":true` goes into the sound object: `"sound":{"file":"ding","loop":true}` |
| `clients` | - | No equivalent. Have your automation send to each device itself |

### Persistence

| AWTRIX 3 | AWTRIX NG | What to change |
|---|---|---|
| `save` | - | Pushed apps are not kept across a restart. See [`save`](#save-is-gone-scripts-took-its-place) |

---

## The tricky ones, explained

### Gradients, rainbow, blink and fade

AWTRIX 3 had four text stylings that excluded one another: `gradient`, `rainbow`, `blinkText` and
`fadeText`. Gradient and rainbow became one key, the app
[palette](../reference/payload.md#palette):

```json title="gradient: [c1, c2] becomes"
{ "text": "OVERHEAT", "palette": ["#FFFF00", "#FF0000"], "textColor": "palette" }
```

```json title="rainbow: true becomes"
{ "text": "PARTY", "palette": "Rainbow", "textColor": "palette" }
```

The palette does more than the old keys did. It can hold up to 16 stops, repeat and move along the
text (`paletteSpan`, `paletteSpeed`), and the same ramp also colors charts, the progress bar and
the background effect. Blink and fade stayed what they were, as `textBlinkMs` and `textFadeMs`.
When both a palette and a blink or fade are set, the palette wins. The
[precedence table](../reference/payload.md#which-color-wins) has the exact order.

### Four keys became one `scroll` object

`noScroll`, `scrollSpeed` and the fixed scroll behavior are one
[`scroll` object](../reference/payload.md#scrolling) in AWTRIX NG. It has modes AWTRIX 3 did not
have: `wrap`, `loop`, `bounce`, direction, entry from outside the display and a hold time:

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

AWTRIX NG has no `pos` key. The rotation order is set once, for all apps, with a single call.
Unlike `pos`, it is stored on the device and survives reboots:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H "Content-Type: application/json" \
  -d '{"order":["Time","weather","Date"],"disabled":[]}'
```

A name in the order that does not exist yet simply waits, so your daily pushed app lands in its
slot the moment the automation sends it. Switching off is its own `disabled` list beside `order`, and
is required with it. Send no order call at all and your apps rotate in the order they arrive, the way
AWTRIX 3 sorted them in. Everything the order call can do, such as switching off and duplicates
for more display time, is in
[Pushed apps → Reordering](pushed-apps.md#reordering-switching-off-and-duplicating).

### `save` is gone - scripts took its place

On AWTRIX 3, `save: true` stored a custom app so it survived a reboot. In AWTRIX NG, pushed apps
are not kept across a restart: your automation pushes again and the app comes back with *current*
data instead of an old stored copy ([why](pushed-apps.md#a-pushed-app-lasts-until-awtrix-restarts)).

For content that should come back **by itself**, such as a label, a logo or anything else that
needs no outside data, write a [script](scripting/index.md). A script is a small program stored on
the device that makes its own content. It also replaces AWTRIX 3's MQTT-placeholder `.json` files:
a script can [subscribe to MQTT topics](scripting/network.md#mqtt) and show the values in any
format.

---

## Checks that are stricter than you remember

AWTRIX 3 accepted almost anything and quietly skipped what it did not understand. Here the rule is
**all or nothing**: the whole payload is applied, or the whole payload is rejected and nothing
changes. The upside is that every rejection names the culprit:

```json
{ "error": { "code": "validationFailed", "message": "unknown key", "field": "pushIcon" } }
```

What most commonly trips a freshly ported payload:

* **An old key name:** `422`, with the AWTRIX 3 key in `field`. Rename it per the
  [table above](#the-key-map).
* **A form `Content-Type` header**, which `curl -d` sends by default: `415` or `400`
  ([why both](../reference/conventions.md#content-type-is-mandatory)).
* **A notification key on a pushed app:** `hold`, `stack`, `wakeup` and the sound keys are
  [notification-only](../reference/payload.md#notification-only-keys), as in AWTRIX 3. Sending one
  to an app is an error in AWTRIX NG, where AWTRIX 3 ignored it.
* **A payload over 8192 bytes:** `413`. Base64-encode bitmaps and see
  [keeping payloads small](graphics.md#keeping-payloads-small).

The complete rule set is under [payload → Errors](../reference/payload.md#errors).

---

## Migration checklist

1. **Rewrite the endpoint:** `POST /api/custom?name=x` → `PUT /api/v1/apps/pushed/x`, or the MQTT
   topic `custom/x` → `cmd/apps/pushed/x`.
2. **Add the `Content-Type: application/json` header** to every HTTP request.
3. **Rename the keys** using [the key map](#the-key-map).
4. **Multiply `duration` and `lifetime` by 1000:** all durations are in milliseconds.
5. **Turn numbers into words:** `textCase`, `pushIcon`→`iconMode`, `lifetimeMode`→`lifetimeExpiry`.
6. **Convert `draw` objects to arrays**, if you draw.
7. **Replace deletes:** over HTTP an empty body does not remove an app. Use
   `DELETE /api/v1/apps/{name}`.
8. **Re-create persistence:** drop `save`, and push on a schedule or move the app to a
   [script](scripting/index.md). Set the rotation once with `PUT /api/v1/apps/order`.
9. **Send it and read the errors:** each `422` names the next key to fix.

## Your display type {#your-panel-type}

AWTRIX 3 picked the display with a single number, `MATRIX_LAYOUT` 0, 1 or 2. Here you describe the
display by its parts, in the web UI under **System → Panel**. Find your old number in the first
column and set the fields next to it:

| AWTRIX 3 | What that is | Panel width | Panels | First LED | Wiring direction | Serpentine |
|---|---|---|---|---|---|---|
| `0` (default<!-- only esp32 -->, Ulanzi TC001<!-- /only -->) | one 32×8 panel, every second row backwards | `32` | `1` | Top left | Along the rows | on |
| `1` | four 8×8 tiles side by side | `8` | `4` | Top left | Along the rows | off |
| `2` | one 32×8 panel wired downwards | `32` | `1` | Top left | Along the columns | on |

Leave **Mirror** and **Rotate 180°** off. They turn the picture, not the wiring, and AWTRIX 3 had
nothing like them. The **Display size** line above the fields does the sum while you type. It
should read `32 × 8 = 256 LEDs` for all three.

Everything except the total width takes effect at once, so you can watch the display while you
switch **Serpentine** on and off. Changing `Panel width × Panels` needs a restart.

!!! tip "If layout 1 comes out scrambled"
    Some 8×8 tiles are wired in a zigzag inside the tile, which AWTRIX 3 could not express. Turn
    **Serpentine** on and keep everything else the same.

!!! tip "If your wiring is none of the three rows above"
    A self-built display is often wired in a way no single `MATRIX_LAYOUT` value could describe, so
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

* **Device settings do not import.** Set the device up fresh. Battery calibration in particular
  works differently ([Coming from an AWTRIX 3 device](../reference/system.md#coming-from-an-awtrix-3-device)).
* **Home Assistant:** automations built on the AWTRIX 3 endpoints need the same endpoint and key
  changes. See [Home Assistant](home-assistant.md) for working examples.
* **Icons:** the same 8×8 icon files work, with the same IDs. See [Icons](icons.md).

## Related

* [Pushed apps](pushed-apps.md) - the full guide to what custom apps became
* [App & notification payload](../reference/payload.md) - every key, exactly specified
* [Visual reference](../reference/visuals.md) - colors, effects, palettes, overlays
* [Scripting guide](scripting/index.md) - self-contained apps that survive reboots
