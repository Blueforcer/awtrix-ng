# App & notification payload

One JSON schema drives both pushed apps and notifications. The same object describes a *page*: text, icon, colors, charts, draw primitives, a background effect, an overlay, and timing.

| | |
|---|---|
| **Pushed app** | `PUT /api/v1/apps/pushed/{name}` · MQTT `<prefix>/cmd/apps/pushed/<name>` |
| **Notification** | `POST /api/v1/notifications` · MQTT `<prefix>/cmd/notify` |

The schema has exactly **40 top-level keys** - 33 shared, plus 7 read only for notifications. Any other key is an error.

## Errors

One rule: **the whole payload is applied, or the whole payload is rejected.** Nothing is stored when a request fails, and no key quietly falls back to a default.

| Condition | Response |
|---|---|
| Body is not valid JSON | `400 invalidJson` |
| Body over 8192 bytes | `413 payloadTooLarge` |
| Unknown top-level key | `422 validationFailed`, `field` = the key |
| Malformed color, any position | `422 validationFailed`, `field` = the key |
| A mode key given a word that is not in its list | `422 validationFailed`, `field` = the key |
| Unknown `effect` or `overlay` name | `422 validationFailed`, `field` = `effect` / `overlay` |
| Unknown draw command name | `422 validationFailed`, `field` = `draw[<i>]` |
| Draw element that is not an array | `422 validationFailed`, `field` = `draw[<i>]` |
| Wrong argument count for a draw command | `422 validationFailed`, `field` = `draw[<i>]` |
| Non-numeric coordinate, size or radius | `422 validationFailed`, `field` = `draw[<i>]` |
| `pixels` with an odd coordinate tail | `422 validationFailed`, `field` = `draw[<i>]` |

Geometry outside the panel is **not** an error - it is clipped. A shape with zero width or height is not an error either and simply draws nothing; see [Draw commands](#draw-commands).

## Endpoints

### Pushed apps

The app name comes from the **URL path**, never from the JSON body.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"text":"21.5C","icon":"2422","textColor":"#00AAFF"}'
```

The app lives in RAM until it is replaced, deleted, expired by `lifetimeMs`, or AWTRIX restarts.
Nothing is written to flash for it; content that must return by itself after a reboot belongs in a
[script](../guides/scripting.md).

Delete an app with `DELETE /api/v1/apps/{name}` - the route on the app itself, which works for any
kind of app, not on
the `pushed` sub-collection. An empty body or the literal `{}` on `PUT` is **not** a delete: it
answers `422` and points you at `DELETE`.

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/apps/weather
```

A name must match `[A-Za-z0-9_-]{1,32}`, checked before the payload is parsed; a malformed one is
`400 invalidName`. Any method other than `PUT` on `/api/v1/apps/pushed/{name}` returns
`405 methodNotAllowed`.

### Notifications

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Doorbell","icon":"1234","hold":true,"soundRtttl":"d:d=4,o=5,b=120:c,e,g"}'
```

Dismiss the notification currently on screen:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/active
```

Or retract a specific one by its `name` - wherever it sits in the queue, not just the one on screen:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/backup-job
```

`200 {"ok":true}` when a match was removed, `404 notFound` when nothing in the queue carries that name. The literal name `active` is reserved for the on-screen route and cannot be addressed this way. Full rules: [`DELETE /api/v1/notifications/{name}`](http.md#delete-apiv1notificationsname).

`POST` is the only accepted method on `/api/v1/notifications`.

### Array payloads

A top-level **array** sent to `PUT /api/v1/apps/pushed/{name}` creates indexed apps `<name>0`, `<name>1`, … - one per object element. Non-object elements are skipped without consuming an index. `DELETE /api/v1/apps/{name}` erases the exact name plus the numbered apps that array push created; an app you pushed to `<name>1` yourself is a separate app and stays.

An array is **all-or-nothing**: if any element trips any rule in [Errors](#errors), or the batch as a whole will not fit under the resident-app cap, the whole request is rejected and none of its apps are created or updated.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/stocks \
  -H 'Content-Type: application/json' \
  -d '[{"text":"AAPL 189"},{"text":"MSFT 402"}]'
# -> creates apps "stocks0" and "stocks1"
```

An array sent to `POST /api/v1/notifications` may hold at most **one** element; more than one is rejected with `422 validationFailed` and nothing is queued, so post notifications one at a time. A single object element is parsed as the notification; an empty array - or one whose single element is not an object - queues a notification with every key left at its default.

### Limits

Every cap a payload can reach - the request body size, the number of resident pushed apps, the
notification queue depth and the chart point count - is listed with the response you get at each
edge in [Limits](limits.md#apps-and-notifications).

## Colors

Every color-typed field below accepts the same five forms - `"#FF0000"`, `"F00"`, `[255,0,0]`,
`["HSV",0,100,100]` or the packed integer `16711680` - and always reads back as uppercase
`"#RRGGBB"`. Anything the parser cannot read is rejected with `422 validationFailed` and the
offending key in `field`, wherever it sits in the payload.

These forms are the same everywhere in the API, written out once under
[Visuals → Colors](visuals.md#colors): exact ranges, HSV wrapping, and what `null` means on a
nullable color key.

## Text

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `text` | string \| array | - | `""` | The page text, or an array of colored fragments |
| `textCase` | string | `inherit` · `upper` · `asTyped` | `inherit` | Casing; `inherit` follows the global `uppercase` setting |
| `font` | string | `small` · `large` | `small` | Which panel font to draw with. `large` is seven rows tall and reaches the top row |
| `textColor` | color \| `"palette"` | - | global `textColor` (`#FFFFFF`) | Text color, or `"palette"` to paint from the app's [palette](#palette) |
| `textBlinkMs` | int | ms, 0 = off | `0` | Blink period |
| `textFadeMs` | int | ms, 0 = off | `0` | Sinusoidal fade period |
| `textCenter` | bool | - | `true` | Center text that fits; `false` left-aligns |
| `scroll` | object \| string | see below | inherited | Text motion - mode, direction, entry, trigger, speed and gap |
| `textOffsetX` | int | px | `0` | X shift applied after positioning |
| `textInFront` | bool | - | `false` | Draw order only - see below |

`text` is folded from UTF-8 into the matrix font's codepage: Latin-1 accents, Latin Extended-A letters, `€` and Cyrillic all have their own glyphs. A character with no mapping - an emoji, an unsupported script - renders as a single `?` placeholder: exactly one per dropped character, not one per byte. A number or bool passed as `text` is silently ignored, leaving the text empty. The full range table is under [Character mapping](visuals.md#what-is-mapped).

There is no lowercase mode.

### Colored fragments

Pass an array of `{"text": string, "color": color}` objects to color runs of text independently. Fragments draw left-to-right, each advancing by its own rendered width. There is no cap on fragment count.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/cpu \
  -H 'Content-Type: application/json' \
  -d '{"text":[{"text":"CPU ","color":"#888888"},{"text":"87","color":"#FF0000"},{"text":"%","color":"#888888"}]}'
```

A missing or non-string `text` yields `""`; a fragment without a `color` is white.

When `text` is a fragment array the top-level `textColor` is ignored and each fragment's own `color` wins - unless `textColor` is `"palette"`, which paints the whole run from the palette and ignores the fragment colors. `textBlinkMs`, `textFadeMs`, `textCase` and the global `uppercase` setting apply either way.

### Which color wins

For non-fragment text, exactly one styling path wins. Checked in this order:

| Priority | Condition | Result |
|---|---|---|
| 1 | `textColor: "palette"` with a `palette` set | Ramp across the text - `textBlinkMs`/`textFadeMs` **ignored** |
| 2 | `textFadeMs > 0` | Smooth pulsing fade of the resolved color |
| 3 | `textBlinkMs > 0` | Blink of the resolved color |
| 4 | - | Solid `textColor`, else global `textColor` |

So a palette beats `textColor`, and `textFadeMs` beats `textBlinkMs` when both are set. Asking for `"palette"` without setting one leaves the text on its own color. Painting text from a palette is described under [`palette`](#palette).

- **`textBlinkMs`** - the color shows during the *second* half of each period and black during the first. The glyphs are still painted in black, so they occlude the background rather than becoming transparent.
- **`textFadeMs`** - the color rises and falls smoothly between black and full brightness, once per period.

```bash
# Gradient across the string
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hot \
  -H 'Content-Type: application/json' \
  -d '{"text":"OVERHEAT","palette":["#FFFF00","#FF0000"],"textColor":"palette"}'

# Blinking red alert
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALERT","textColor":"#FF0000","textBlinkMs":600}'
```

### `textInFront`

`textInFront` sets z-order, not position - the baseline is row 6 either way. With `true` the decorations (draw commands, progress bar, charts) are painted first and the text lands **on top**; the default `false` paints the text first and the decorations over it.

### Positioning and centering

`textCenter` takes effect whenever the text is **not animating** - with `scroll.mode: "static"`, or when the text fits and `scroll.whenFits` is `static`. The text is then centered in the space to the right of the icon column (or across the whole panel when there is no icon), and never overlaps the icon. With `textCenter: false` the text is left-aligned against the icon column instead (or the left edge with no icon). While the text animates, `textCenter` has no effect - the scroll position sets x.

`textOffsetX` is added to the final x in the static case, and it participates in **every** scroll anchor: the rest position, the offscreen entry point, the exit point and the `loop` period all include it, so a positive `textOffsetX` lengthens each cycle by the same amount in every mode. X-axis only.

### Scrolling

`scroll` decides whether text moves and how. It takes an object of seven independent fields. A [script](../guides/scripting.md#styled-and-scrolling-text) sets the same fields, by the same names, on its own `scroll_text()` calls.

| Field | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `mode` | string | `static` · `wrap` · `loop` · `bounce` | `wrap` | Motion behaviour |
| `direction` | string | `left` · `right` | `left` | Travel direction |
| `entry` | string | `inline` · `offscreen` | `inline` | Start at rest on the panel, or scroll in from outside it |
| `whenFits` | string | `static` · `scroll` | `static` | Whether text that already fits still animates |
| `speed` | int | ≥ 0 | `100` | Percent of the base rate |
| `gap` | int | ≥ 0 | `8` | `loop` only - pixels between repetitions |
| `holdMs` | int | ≥ 0 | `1000` | Pause before the text starts moving, and at each `bounce` turn |

A bare string is shorthand for the mode alone: `"scroll": "bounce"` is exactly `"scroll": {"mode": "bounce"}`.

Every field is inherited **individually** from the global `scroll` setting, so `{"scroll":{"mode":"bounce"}}` bounces at the globally configured speed and leaves the other five fields as they are. Inheriting is simply leaving the key out - there is no special value that means "inherit". The global default is read when the page is drawn, so changing it also moves apps that are already pushed.

`speed`, `gap` and `holdMs` must not be negative, the enums take only the values listed above, and no other field name is accepted. A violation is rejected outright with `422 validationFailed` and the offending key in `field` - `scroll.speed`, say. This holds on every route that takes a `scroll`: pushed apps, notifications and `PATCH /api/v1/settings` alike, and nothing is stored when it trips.

```bash
# Half speed, sweeping back and forth
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"A LONG HEADLINE THAT WILL NOT FIT","scroll":{"mode":"bounce","speed":50}}'
```

#### Modes

| `mode` | Motion | Cycle counted | Hold |
|---|---|---|---|
| `static` | None. The text is drawn at its aligned position and overflow is clipped | never | - |
| `wrap` | Runs from the start anchor until it has fully exited, then jumps back to the start anchor | per exit | at the start anchor, on every cycle |
| `loop` | Continuous. A fresh copy always slides in behind the last, `gap` pixels apart, so the screen is never empty | per fold | initial only |
| `bounce` | Sweeps between resting beside the icon (or the left edge) and sitting flush against the far edge | per round trip | at **both** turning points |

`holdMs: 0` removes the hold entirely, which is the only way to get a `bounce` that reverses on the spot or a `wrap` that restarts without pausing.

The base rate is **21 px/s at `speed: 100`**, so `pxPerSec = 21 × (speed / 100)`. `0` freezes the text where it starts, and there is no upper clamp. `200` is the sharpest a scroll gets on an 8 px panel; above it legibility drops however smooth the motion is. Every hold lasts `holdMs`, 1000 ms by default.

#### Anchors

The text area begins at column 9 when an icon is present, column 0 otherwise.

With `direction: left`, an `inline` text rests at the start of that area, while an `offscreen` text starts just past the right edge and scrolls in; either way it finishes once it has moved fully off the left edge. `direction: right` mirrors the whole geometry - rest, entry and exit anchors all swap ends - so the text rests flush against the right edge, an `offscreen` text enters from just past the left edge, and it exits off the right edge.

#### Entry and trigger

`entry: offscreen` starts the text outside the panel and skips the initial hold. `wrap` re-enters from outside on every cycle, and drops the hold each time with it; `loop` uses the offscreen anchor for the initial position only, after which the fold carries it; `bounce` runs only its first leg from outside and then settles into the ping-pong between the two anchors. With `mode: "static"` it is ignored.

`whenFits: static` keeps the rule that text short enough to fit never moves. `whenFits: scroll` animates it regardless of width.

## Icon

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `icon` | string | - | `""` | Icon ID, or inline base64 when longer than 64 chars |
| `iconMode` | string | `fixed` · `pushOnce` · `push` | `fixed` | Whether approaching text shoves the icon aside |
| `iconOffsetX` | int | px | `0` | X shift of the icon |

The mode is chosen purely by **length**:

- **64 characters or fewer** - an icon ID resolved against the filesystem. Animated `/ICONS/<id>.gif` is tried **first**, then static `/ICONS/<id>.jpg`.
- **More than 64 characters** - inline base64 data, decoded and sniffed: a `GIF8` magic makes it an animated GIF, otherwise it is decoded as JPEG.

Only JPEG and GIF are supported - no PNG, no BMP. Icons are drawn at rows 0–7.

- **JPEG** always occupies an 8×8 square. A larger image is not rejected, but only its top-left 8×8 corner is shown, so draw JPEG icons at 8×8.
- **GIF** keeps its own width, up to the full 32×8 panel.

A non-string `icon` is ignored.

An icon narrower than the panel reserves a **9px column** (8px icon + 1px gap) that indents text, bars and the line chart. A GIF spanning the full 32 px is treated as a **background** instead: it is drawn at x=0 beneath the text, indents nothing, and replaces the app's `backgroundColor` colour and any `effect`. An icon that is missing or fails to decode falls back to the icon-less layout rather than leaving a black column.

Transparent GIF pixels render as **black** on the first frame of an animation; within an animation they keep what the previous frame drew there.

### `iconMode`

| Value | Behavior |
|---|---|
| `fixed` | Icon stays put; text scrolls past it |
| `pushOnce` | Scrolling text shoves the icon off to the left **once**; it stays gone, and the text then restarts at x=0 |
| `push` | Icon is pushed out but **returns** on every scroll cycle |

The shift travels 0 → −9px as the text approaches. Does nothing when `icon` is empty.

`iconOffsetX` moves the icon on the X axis only - there is no Y counterpart, the icon always occupies rows 0–7. It does not change the 9px column reserved for text and charts, so a positive `iconOffsetX` slides the icon *under* the text rather than moving the text out of the way.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"Long headline that scrolls","icon":"1234","iconMode":"push"}'
```

## Timing & how long a page lives

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `durationMs` | long | ms | `0` | How long to show; 0 or less uses global `appDurationMs` (7000). Honoured by both pushed apps and notifications |
| `lifetimeMs` | long | ms, 0 = forever | `0` | **Pushed apps only.** Auto-expire after this long |
| `lifetimeExpiry` | string | `remove` · `mark` | `remove` | What happens when the lifetime runs out |
| `repeat` | int | `0` = off | `0` | How many times scrolling text runs across the screen |

### `durationMs`

`durationMs` sets how long the page shows - for **both** pushed apps and notifications. A value of 0 or less falls back to the global `appDurationMs` setting (7000 ms), so a pushed app can dwell longer or shorter than the rest of the loop without touching the global setting.

For notifications, `durationMs` is ignored entirely when `hold` is `true`.

### `lifetimeMs` and `lifetimeExpiry`

Measured as elapsed time from when the app was received. A value of 0 or less disables expiry. Both keys parse on a notification but are never read there - only pushed apps expire.

- **`lifetimeExpiry: "remove"`** (default) - the app is deleted and drops out of the rotation.
- **`lifetimeExpiry: "mark"`** - the app stays in rotation with a 1px dark-red (`#6E0700`) frame drawn around the canvas as a stale marker.

```bash
# Vanish after 5 minutes
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/temp \
  -H 'Content-Type: application/json' \
  -d '{"text":"Transient","lifetimeMs":300000,"lifetimeExpiry":"remove"}'
```

### `repeat`

`repeat` sets how many times scrolling text runs across the screen. The page then stays exactly that long: the text is never cut off halfway, and the page does not linger once the text has been read.

It defaults to **`0`** - off. Nothing is counted, and the page stays for `durationMs` (or the global `appDurationMs`), however much text is left. `repeat: 1` shows long text once from beginning to end, `repeat: 2` twice, and so on. What counts as one run depends on the scroll mode - see [Scrolling](#scrolling).

Two cases where a count above `0` still does not decide:

- **The text does not move**, because it fits on the screen or the mode is `static`. There is nothing to count, so the page uses `durationMs` (or the global `appDurationMs`) as usual, and `repeat` has no effect.
- **You set `durationMs` yourself.** The page then stays at least that long. `{"text":"...","repeat":1,"durationMs":20000}` is shown for 20 seconds even if the text is read in 4.

It works the same for pushed apps and notifications.

## Background

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `backgroundColor` | color | - | absent → black | Solid canvas fill |

`backgroundColor` is ignored whenever an `effect` resolves: an effect owns the whole canvas and is never drawn alongside a fill colour.

## Charts

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `barChart` | array of int | max 16 entries | `[]` | Bar chart values |
| `lineChart` | array of int | max 16 entries | `[]` | Line chart values |
| `chartAutoscale` | bool | - | `true` | Scale to the data max, else fix the max at 8 |
| `chartColor` | color | - | resolved text color | Color of the bars and of the line |

`barChart` and `lineChart` can be combined; both are drawn. Both are capped at 16 entries, with extras silently dropped, and both share `chartAutoscale`. Non-numeric entries coerce to 0.

- **`chartAutoscale: true`** - the chart spans `min .. max` of the data, where `max` is floored at 1 and `min` at 0. An all-positive series therefore spans `0 .. max`; the range only opens downward once a value is actually negative.
- **`chartAutoscale: false`** - the range is fixed at `0 .. 8`: a value of 8 fills the full 8px height, and values outside the range clamp to the canvas.

**`barChart`** divides the space to the right of the icon column evenly among the values - each bar at least 1px wide, separated by a 1px gap. Bars are anchored to the row where the value **zero** falls: with all-positive data that is the bottom row, so a bar's height is proportional to its value as a fraction of the range max. Once the series contains a negative value the baseline lifts off the bottom, positive bars grow up from it and negative bars hang below it.

**`lineChart`** requires at least **2 points** - a 1-element array draws nothing. Its points are spread evenly across the space to the right of the icon column and connected left to right. A line has no baseline to anchor to, so it simply spans the full range: it sits on the bottom row at the range min and on the top row at the range max.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/traffic \
  -H 'Content-Type: application/json' \
  -d '{"barChart":[2,5,3,8,6,4,7,1],"chartColor":"#00FF00","chartAutoscale":true}'
```

`chartColor` colors both charts. Unfilled cells are not painted at all - they keep whatever the background or effect put there.

## Progress bar

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `progress` | int | percent, below 0 = off | `-1` | Fill percentage |
| `progressColor` | color | - | `#00FF00` | Filled portion |
| `progressTrackColor` | color | - | `#FFFFFF` | Unfilled portion |

Drawn on the **bottom row only**, spanning from x=8 when an icon is present, else x=0 - one pixel further left than text and charts, which start at x=9. Values above 100 clamp to 100; `progress: 0` draws a full row of `progressTrackColor`. Any value below 0 draws nothing. The filled part covers that percentage of the bar's width, and the track is the unfilled remainder of the row rather than a background behind it.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/download \
  -H 'Content-Type: application/json' \
  -d '{"text":"64%","progress":64,"progressColor":"#00AAFF","progressTrackColor":"#202020"}'
```

## Effects

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `effect` | string | case-insensitive | `""` | Animated background effect name |
| `effectSpeed` | float | 0.1 - 10.0 | `1.0` | Pace multiplier for the effect and the app's overlay |

`effect` is matched **case-insensitively** - `Plasma`, `plasma` and `PLASMA` are the same effect. An unknown name is rejected with `422 {"code":"validationFailed","field":"effect"}` and nothing is stored. An empty string means "no effect", and `backgroundColor`/black is used instead.

The 19 effect names, and what each one draws, are in
[Visual reference → Background effects](visuals.md#background-effects). The live list is at
`GET /api/v1/capabilities` → `effects`.

### `effectSpeed`

**`effectSpeed`** multiplies the pace of the animation: `2.0` runs it twice as fast, `0.5` half as
fast. It is **clamped to 0.1 - 10.0**, so `0` and negatives become `0.1` - a near-standstill, never a
full freeze - and anything above `10.0` is capped. It drives the app's effect **and** an overlay the
app names itself.

Colours come from the app-level [`palette`](#palette), which the effect shares with the text and the
charts. Which effects use a palette at all is listed by `GET /api/v1/capabilities` ->
`paletteEffects`; the rest keep their own colours whatever palette is set.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/ambient   -H 'Content-Type: application/json'   -d '{"effect":"Plasma","effectSpeed":0.5,"palette":"Lava"}'
```

Palette and speed belong to the app that sent them: two apps using `Plasma` with different palettes
keep their own colours, and an app carrying neither key gets the defaults.

## Palette

One palette per app, shared by whatever asks for it: the text, the charts, the progress bar and the
effect. A payload names it once.

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `palette` | string \| array \| null | max 16 stops | absent | Built-in or user palette name, or a list of colour stops |
| `paletteBlend` | bool | - | `true` | Interpolate between the 16 entries; `false` gives hard bands |
| `paletteSpan` | int | px, 0 = stretch | `0` | Pixels per full pass when painting text |
| `paletteSpeed` | float | 0.0 - 10.0, 0 = still | `0` | Palette passes per second when painting text |

**`palette`** as a string is resolved **case-insensitively**, as `/PALETTES/<name>.txt` on the
filesystem first and then against the 8 built-in names: `Cloud`, `Lava`, `Ocean`, `Forest`,
`Stripe`, `Party`, `Heat`, `Rainbow`. A file therefore replaces the built-in of the same name for as
long as it exists, which is how a built-in is edited. A name it cannot resolve either way is
rejected with `422 {"code":"validationFailed","field":"palette"}` - it is not quietly swapped for a
built-in. `null` and `""` clear it.

**`palette`** as an array takes 1 to 16 colour **stops**, which are spread across all 16 entries by
linear interpolation. Two stops describe a full ramp between them, not two entries followed by
fourteen leftovers. An empty array is rejected.

An element may instead be `{"color": <colour>, "pos": 0-100}`, placing that stop at a percentage of
the ramp rather than letting it fall on the even spread - so `pos` decides how much of the ramp each
colour gets. Both keys are required. The two element forms cannot be mixed in one array, out-of-order
stops are sorted, the ends extend flat, and two stops at one position are a hard edge. The same thing
a palette file writes as `RRGGBB@70`; see
[Visual reference → Custom palettes](visuals.md#custom-palettes).

To paint something from it, set that thing's colour field to the string `"palette"`:

| Field | Sampled by |
|---|---|
| `textColor` | pixel column across the text |
| `chartColor` | each bar's or point's value, within the chart's own range |
| `progressColor` | position along the bar; the fill reveals the ramp up to `progress` |

`progressTrackColor` stays a plain colour - the unfilled part of the bar has no value behind it.

### Painting text

The ramp is sampled **per pixel column**, so the colour is even across the string whatever the glyph
widths are. Two regimes:

- **`paletteSpan: 0`** (the default) stretches one pass between the first and last lit column. The
  text starts on the first colour and ends on the last.
- **`paletteSpan: N`** repeats the palette every `N` pixels. The cycle is seamless, so a long
  scrolling string keeps its colour variation instead of flattening out.

**`paletteSpeed`** moves the ramp along the text at that many palette passes per second. Motion
always cycles, so a speed above `0` puts a `paletteSpan: 0` ramp into the repeating regime with the
text's own width as the span.

```bash
# Gradient stretched across the string
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hot   -H 'Content-Type: application/json'   -d '{"text":"OVERHEAT","palette":["#FFFF00","#FF0000"],"textColor":"palette"}'

# Repeating, moving rainbow
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/party   -H 'Content-Type: application/json'   -d '{"text":"PARTY TIME","palette":"Rainbow","textColor":"palette","paletteSpan":24,"paletteSpeed":1}'

# Bars coloured by value
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/load   -H 'Content-Type: application/json'   -d '{"barChart":[3,5,2,8,6,9],"palette":"Heat","chartColor":"palette"}'
```

The `palettes` list returned by `GET /api/v1/capabilities` carries only the eight built-in names, not
your `/PALETTES/*.txt` files. Your own files still work by name; list them with
`GET /api/v1/files?dir=/PALETTES`.

## Overlay

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `overlay` | string | case-insensitive | `""` | Weather overlay drawn on top of everything |

Names are matched case-insensitively, so `"SNOW"` and `"snow"` are equivalent. The 6 overlays are
`rain`, `snow`, `drizzle`, `storm`, `thunder` and `frost`; what each one draws is in
[Visual reference → Weather overlays](visuals.md#weather-overlays). An unrecognized name is rejected
with `422 {"code":"validationFailed","field":"overlay"}` and nothing is stored, so a typo cannot
masquerade as an opt-out from the global overlay.

An empty `overlay` falls back to the global overlay set via `PATCH /api/v1/display`. A non-empty per-app overlay **wins** over the global one. Overlays are drawn **last**, on top of text and decorations.

Overlays honour the app's `effectSpeed` and `palette`: the speed scales their pace, and a palette recolours them. Without a palette they keep their stock colours.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/forecast \
  -H 'Content-Type: application/json' \
  -d '{"text":"4C","icon":"2422","overlay":"snow"}'
```

## Draw commands

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `draw` | array of arrays | uncapped | `[]` | Drawing commands, each an array with the command name first |

| Command | Arguments |
|---|---|
| `["pixel", x, y, color]` | One pixel |
| `["pixels", color, x1, y1, x2, y2, …]` | Many pixels in one color |
| `["line", x1, y1, x2, y2, color]` | Both endpoints included |
| `["rect", x, y, w, h, color]` | Outline, 1px, spans `x` … `x+w-1` |
| `["rectFill", x, y, w, h, color]` | Filled |
| `["circle", cx, cy, r, color]` | Center and radius |
| `["circleFill", cx, cy, r, color]` | Filled |
| `["text", x, y, "HI", color]` | Baseline sits at `y + 5` |
| `["bitmap", x, y, w, h, data]` | `data` is base64 RGB888 or an array of colors |

- The trailing `color` may be left out; the command then uses the app's text color. `pixels` takes its color first, where `null` means the same.
- Commands are drawn in array order.
- Off-canvas pixels are dropped, never wrapped.
- `w` or `h` of zero or less draws nothing, as does a negative radius. A radius of `0` draws the center pixel.
- A short `bitmap` leaves the remaining cells undrawn; extra entries are ignored.

`text` here is UTF-8 like the page's `text` and is drawn in the page's `font`, but is unaffected by `textCase`, `palette`, `textBlinkMs`, `textFadeMs`, `textCenter` and the global `uppercase` setting. The page's own text sits on a fixed baseline at row 6.

`bitmap` data comes in two interchangeable forms: an array of `w × h` colors, row-major, in any of the [color forms](#colors); or a base64 string of `w × h × 3` raw RGB888 bytes. The base64 form is far smaller for a large image.

The number of commands is bounded only by the 8192-byte body limit. See [Keeping payloads small](../guides/graphics.md#keeping-payloads-small) for how far that goes.

```bash
# A framed box with a filled circle and a label
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/art \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["rect",0,0,32,8,"#202020"],
        ["circleFill",4,4,2,"#F00"],
        ["text",9,1,"HI"]
      ]}'
```

## Notification-only keys

These 7 keys are accepted **only** by `POST /api/v1/notifications`. A pushed app that sends one is rejected with `422 validationFailed` (an app's name comes from its URL path, not the body).

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `name` | string | - | `""` | Label for [dismissing this notification by name](#notifications); matched exactly, use URL-safe characters |
| `hold` | bool | - | `false` | Never auto-expire; stay until dismissed |
| `stack` | bool | - | `true` | Queue behind existing notifications, else replace the current one |
| `wakeup` | bool | - | `false` | Render even while the matrix is powered off |
| `sound` | string \| int | - | `""` | MP3 sound or melody file name, or DFPlayer track |
| `soundRtttl` | string | - | `""` | Inline RTTTL melody |
| `soundLoop` | bool | - | `false` | Re-trigger the melody when it finishes |

### `hold` and `stack`

**`hold: true`** - the notification never auto-expires; `durationMs` and `appDurationMs` are ignored and it stays until `DELETE /api/v1/notifications/active`. It pins only the **front** of the queue; stacked notifications behind it wait indefinitely.

**`stack: true`** (default) - appended to the queue and shown after the ones ahead of it. A push into a [full queue](limits.md#apps-and-notifications) is rejected with `507 insufficientStorage`.

**`stack: false`** - **replaces** the notification currently on screen, leaving any queued behind it intact. Scroll, icon and sound start again from the beginning. On an empty queue, `stack: false` simply pushes.

```bash
# Interrupt whatever is showing right now
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"URGENT","stack":false,"textColor":"#FF0000"}'
```

### `wakeup`

Renders the notification even while the matrix is powered off via `PATCH /api/v1/display {"power":false}`. Display blanking is skipped for as long as a `wakeup` notification is the active one.

### Sound

Both `sound` and `soundRtttl` are gated on the global `soundEnabled` setting, and play once when the notification first appears.

`sound` accepts a **string or an integer** - an integer is converted to its decimal string, so `"sound": 5` becomes `"5"`. Bools, floats and objects are ignored. Its meaning depends on the sound hardware:

- **Speaker (I2S DAC)** - `/SOUNDS/<sound>.mp3` plays if it exists, else the melody below. A playing radio stream is interrupted and reconnects afterwards.
- **Buzzer** - names `/MELODIES/<sound>.txt`, an RTTTL file. A missing file plays nothing.
- **DFPlayer** - parsed as a track number, which must be greater than 0.

`soundRtttl` is an inline melody string played directly, with no filesystem access. An unparseable melody plays nothing - a notification is not rejected over its melody, so unlike [`POST /api/v1/sounds/play`](http.md#post-apiv1soundsplay) there is no `422` here. Check a melody on that route, or in the [Audio tab](../getting-started/web-ui.md#melodies), before pasting it into a notification.

When both `sound` and `soundRtttl` are present, only the `soundRtttl` melody plays. On DFPlayer hardware, `soundRtttl` is not supported at all.

**`soundLoop: true`** re-triggers whichever of `soundRtttl`/`sound` applies once it finishes, for as long as the notification is shown. Combined with `hold`, this gives an indefinite alarm:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALARM","hold":true,"soundLoop":true,"soundRtttl":"siren:d=8,o=5,b=200:c,g,c,g"}'
```

## Render order

Each frame is painted in this order:

1. **Background** - the effect if `effect` resolves, otherwise a clear to `backgroundColor` or black.
2. **Text and decorations** - if `textInFront`, decorations then text; otherwise text then decorations. Decorations are always `draw` commands → progress → bar chart → line chart.
3. **Icon** - drawn at rows 0–7, at `iconOffsetX` plus any `iconMode` shift.
4. **Overlay** - the per-app overlay if set, else the global one.
5. **Stale marker** - a dark-red frame if a `lifetimeExpiry: "mark"` app has expired. Drawn before the icon and overlay, so those paint over it.

## Worked example

Everything at once - a pushed app with an icon, a gradient, a chart, a progress bar, an effect and an overlay:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/dashboard \
  -H 'Content-Type: application/json' \
  -d '{
        "text": "SERVER",
        "icon": "1234",
        "palette": ["#00FF00", "#00AAFF"],
        "textColor": "palette",
        "textInFront": true,
        "iconMode": "push",
        "scroll": {"speed": 80},
        "barChart": [3, 5, 2, 8, 6],
        "chartColor": "#333333",
        "chartAutoscale": true,
        "progress": 72,
        "progressColor": "#00FF00",
        "progressTrackColor": "#101010",
        "effect": "PlasmaCloud",
        "effectSpeed": 0.4,
        "overlay": "rain",
        "lifetimeMs": 600000,
        "lifetimeExpiry": "mark"
      }'
```
