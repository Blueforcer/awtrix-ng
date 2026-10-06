# App & notification payload

One JSON format describes both pushed apps and notifications. It sets what an app or a
notification shows: text, icon, colors, charts, drawings, a background effect, a weather overlay
and timing.

| | |
|---|---|
| **Pushed app** | `PUT /api/v1/apps/pushed/{name}` · MQTT `<prefix>/cmd/apps/pushed/<name>` |
| **Notification** | `POST /api/v1/notifications` · MQTT `<prefix>/cmd/notify` |

Every top-level key is listed on this page. A few keys work only for notifications. Any other
key is an error.

<!-- only tc002 -->
The optional `layout` key splits the display into [regions](../guides/layouts.md), each with its
own text, icon, chart, progress bar or drawing. It replaces the visual keys below and cannot be
combined with them. Colors, palettes, effects, overlays and draw commands go into the layout instead.
<!-- /only -->

## Endpoints

### Pushed apps

The app name comes from the **URL**, never from the JSON body.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"text":"21.5C","icon":"2422","textColor":"#00AAFF"}'
```

A pushed app stays until you replace or delete it, its `lifetimeMs` runs out, or AWTRIX restarts.
It does not come back after a restart. For content that should, write a
[script](../guides/scripting/index.md).

Delete an app with `DELETE /api/v1/apps/{name}`. This route works for any kind of app. There is
no `DELETE` on the `pushed` path. An empty body or `{}` on `PUT` does **not** delete: it answers
`422` and points you to `DELETE`.

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/apps/weather
```

A name must match `[A-Za-z0-9_-]{1,32}`. Any other name gets `400 invalidName`, and the body is
not read. Any method other than `PUT` on `/api/v1/apps/pushed/{name}` gets
`405 methodNotAllowed`, whatever the name.

### Notifications

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Doorbell","icon":"1234","hold":true,"sound":{"rtttl":"d:d=4,o=5,b=120:c,e,g"}}'
```

Close the notification shown:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/active
```

Or remove a notification by its `name`: wherever it is in the queue, not only shown:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/notifications/backup-job
```

This answers `200 {"ok":true}` when one was removed and `404 notFound` when no notification in
the queue has that name. The name `active` is reserved for the notification that is shown. Full
rules: [`DELETE /api/v1/notifications/{name}`](http.md#delete-apiv1notificationsname).

`POST` is the only method on `/api/v1/notifications`.

### Array payloads

Send a JSON **array** to `PUT /api/v1/apps/pushed/{name}` to create numbered apps `<name>0`,
`<name>1`, …: one per object in the array. Entries that are not objects are skipped and do not
use up a number.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/stocks \
  -H 'Content-Type: application/json' \
  -d '[{"text":"AAPL 189"},{"text":"MSFT 402"}]'
# -> creates apps "stocks0" and "stocks1"
```

`DELETE /api/v1/apps/{name}` removes the app with that exact name plus the numbered apps an array
created. An app you pushed to `<name>1` yourself is a separate app and stays.

An array is **all or nothing**. If any element breaks a rule in [Errors](#errors), or the new apps
would not fit under the app limit, the whole request is rejected and no app is created or
changed.

An array sent to `POST /api/v1/notifications` may hold at most **one** element. More than one
gets `422 validationFailed` and nothing is queued. Send notifications one at a time. A single
object element is used as the notification. An empty array, or one whose element is not an
object, queues a notification with every key at its default.

## Colors

Every color key below accepts the same five forms (`"#FF0000"`, `"F00"`, `[255,0,0]`,
`["HSV",0,100,100]` or the number `16711680`) and is always read back as uppercase `"#RRGGBB"`.
Anything else gets `422 validationFailed` with the key in `field`, wherever it is in the payload.

Exact ranges, HSV wrapping and what `null` means are in
[Visuals → Colors](visuals.md#colors).

## Text

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `text` | string \| array | - | `""` | The text, or an array of colored pieces |
| `textCase` | string | `inherit` · `upper` · `asTyped` | `inherit` | Upper or lower case. `inherit` follows the global `uppercase` setting |
| `font` | string | a name from `capabilities.fonts` | `small` | The font: `small`, `large` or a Matrix font. See [The fonts](../guides/text.md#the-fonts) |
| `textColor` | color \| `"palette"` | - | global `textColor` (`#FFFFFF`) | Text color, or `"palette"` to color the text from the app's [palette](#palette) |
| `textBlinkMs` | int | ms, 0 = off | `0` | Blink period |
| `textFadeMs` | int | ms, 0 = off | `0` | Period of a smooth fade in and out |
| `textAlign` | string | `start` · `center` · `end` | `center` | Where still text sits: left, centered or right |
| `scroll` | object \| string | see below | inherited | How text moves: mode, direction, start, speed and gap |
| `textOffsetX` | int | px | `0` | Moves the text left or right after it is placed |
| `textInFront` | bool | - | `false` | Draws the text on top of charts and drawings. See below |

`text` is UTF-8. Latin accents, Latin Extended-A, Greek, Cyrillic, Vietnamese, IPA letters,
common currency signs, and the Chinese and Korean characters for dates and weekdays all have
their own letters in every font. A character without a letter (an emoji, an unsupported
script) shows as one `?`. A number or `true`/`false` as `text` is ignored and the text stays
empty. The full list is in [Character mapping](visuals.md#what-is-mapped).

There is no lowercase mode.

### Colored fragments

To color parts of the text differently, pass an array of `{"text": string, "color": color}`
objects. They are drawn left to right, one after the other. There is no limit on how many.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/cpu \
  -H 'Content-Type: application/json' \
  -d '{"text":[{"text":"CPU ","color":"#888888"},{"text":"87","color":"#FF0000"},{"text":"%","color":"#888888"}]}'
```

A piece without `text` (or with a `text` that is not a string) is empty. A piece without `color`
is white.

With pieces, the top-level `textColor` is ignored and each piece uses its own `color`, unless
`textColor` is `"palette"`: then the whole text is colored from the palette and the piece colors
are ignored. `textBlinkMs`, `textFadeMs`, `textCase` and the global `uppercase` setting apply
either way.

### Which color wins

For plain text, only one coloring applies. AWTRIX checks in this order:

| Priority | Condition | Result |
|---|---|---|
| 1 | `textColor: "palette"` and a `palette` is set | Palette colors across the text: `textBlinkMs`/`textFadeMs` are **ignored** |
| 2 | `textFadeMs > 0` | The color fades smoothly in and out |
| 3 | `textBlinkMs > 0` | The color blinks |
| 4 | - | Plain `textColor`, else the global `textColor` |

So a palette beats `textColor`, and `textFadeMs` beats `textBlinkMs`. `"palette"` without a
`palette` keeps the text in its normal color. See [`palette`](#palette).

- **`textBlinkMs`**: the text is black for the first half of each period and in color for the
  second half. The black text still covers the background. It does not become see-through.
- **`textFadeMs`**: the text fades from black to full color and back, once per period.

```bash
# Gradient across the text
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hot \
  -H 'Content-Type: application/json' \
  -d '{"text":"OVERHEAT","palette":["#FFFF00","#FF0000"],"textColor":"palette"}'

# Blinking red alert
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALERT","textColor":"#FF0000","textBlinkMs":600}'
```

### `textInFront`

`textInFront` decides what is drawn on top, not where the text sits: the text stays where its
font puts it either way. With `true`, drawings, progress bar and charts are drawn first and the text **on
top**. With the default `false`, the text is drawn first and the others over it.

### Positioning and alignment

`textAlign` works whenever the text is **not moving**: with `scroll.mode: "static"`, or when
the text fits and `scroll.whenFits` is `static`. It places the text in the space right of the
icon, or across the whole display without an icon, and the text never covers the icon:

- `start`: the text starts right after the icon, or at the left edge without an icon.
- `center`: the text is centered.
- `end`: the text ends at the right edge of the display, on every display width.

Still text wider than that space starts right after the icon, or at the left edge without an
icon. While the text scrolls, `textAlign` has no effect.

`textOffsetX` moves static text left or right. For scrolling text, it moves every point of the
scroll (where it rests, where it enters and leaves, and the `loop` distance), so a positive value
makes each cycle longer by that many pixels in every mode. It moves left/right only.

### Scrolling

`scroll` decides whether and how text moves. It takes an object with seven separate fields.
A [script](../guides/scripting/drawing.md#styled-and-scrolling-text) uses the same fields, with the same
names, for its `scroll_text()` calls.

| Field | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `mode` | string | `static` · `wrap` · `loop` · `bounce` | `wrap` | How the text moves |
| `direction` | string | `left` · `right` | `left` | Direction of travel |
| `entry` | string | `inline` · `offscreen` | `inline` | Start on the display, or scroll in from outside |
| `whenFits` | string | `static` · `scroll` | `static` | Whether text that fits still moves |
| `speed` | int | ≥ 0 | `100` | Speed in percent of the normal speed |
| `gap` | int | ≥ 0 | `8` | `loop` only: pixels between repeats |
| `holdMs` | int | ≥ 0 | `1000` | Pause before the text starts moving, and at each `bounce` turn |

A plain string sets only the mode: `"scroll": "bounce"` is the same as
`"scroll": {"mode": "bounce"}`.

Each field you leave out comes from the global `scroll` setting. So `{"scroll":{"mode":"bounce"}}`
bounces at the global speed and keeps the other five fields as set globally. There is no special
value for "use the global one": just leave the key out. If you change the global setting later,
apps that are already pushed follow it too.

`speed`, `gap` and `holdMs` must not be negative, the word fields take only the values listed,
and no other field name is accepted. Otherwise the request gets `422 validationFailed` with the
key in `field`: for example `scroll.speed`. This is the same on every route that takes `scroll`:
pushed apps, notifications and `PATCH /api/v1/settings`. Nothing is saved.

```bash
# Half speed, moving back and forth
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"A LONG HEADLINE THAT WILL NOT FIT","scroll":{"mode":"bounce","speed":50}}'
```

#### Modes

| `mode` | Movement | One run is | Pause |
|---|---|---|---|
| `static` | None. Text that does not fit is cut off | never counted | - |
| `wrap` | Moves until it has left the display, then starts again from the start position | each time it leaves | at the start, every time |
| `loop` | Moves without end. A new copy follows `gap` pixels behind, so the display is never empty | each full repeat | at the very start only |
| `bounce` | Moves back and forth between resting next to the icon (or the left edge) and touching the other edge | each round trip | at **both** ends |

`holdMs: 0` removes the pause. This is the only way to get a `bounce` that turns at once, or a
`wrap` that restarts without pausing.

At `speed: 100` text moves **21 pixels per second**: `pixels per second = 21 × speed / 100`.
`0` stops the text where it starts. There is no upper limit<!-- only esp32 esp32-s3 -->, but above `200` text on an 8-pixel
display gets hard to read<!-- /only -->.

#### Start and end points

When there is an icon, the text area starts right after the icon and its [`iconGap`](#icongap):
column 9 for an 8-pixel icon with the default gap. Without an icon it starts at column 0.

With `direction: left`, `inline` text starts at the beginning of that area, and `offscreen` text
starts just past the right edge and scrolls in. Either way it ends when it has fully left the
left edge. `direction: right` mirrors all of this: the text rests at the right edge, `offscreen`
text enters from the left, and it leaves on the right.

#### Entry and trigger

`entry: offscreen` starts the text outside the display, without the first pause. `wrap` comes in
from outside on every run, each time without a pause. `loop` starts outside only the first time.
`bounce` comes in from outside only for its first leg and then moves between its two ends. With
`mode: "static"`, `entry` has no effect.

`whenFits: static` keeps text that fits still. `whenFits: scroll` moves it anyway.

## Icon

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `icon` | string | - | `""` | Icon ID, the image itself as a data URL, or a web address |
| `iconMode` | string | `fixed` · `pushOnce` · `push` | `fixed` | Whether scrolling text pushes the icon away |
| `iconOffsetX` | int | px | `0` | Moves the icon left or right |
| `iconGap` | int | 0–128 px | `1` | Empty columns between icon and text |
| `icons` | array | up to 4 objects | `[]` | More icons at any `x`, `y` position |

`icon` takes one of <!-- only esp32 esp32-s3 -->two<!-- /only --><!-- only tc002 -->three<!-- /only --> forms:

- **An icon ID** of up to 64 characters. AWTRIX looks for `/ICONS/<id>.gif` **first**, then
  `/ICONS/<id>.jpg`.
- **A data URL**: the image itself, base64-encoded: `data:image/gif;base64,…` for a GIF,
  `data:image/jpeg;base64,…` for a JPEG. The type in the URL decides how the image is read. An
  image that does not match its type is not shown.
<!-- only tc002 -->
- **A web address** that starts with `http://` or `https://`, up to 2048 characters. The clock
  downloads the picture itself.
  See [Pictures from the internet](../guides/icons.md#pictures-from-the-internet).
<!-- /only -->

Anything else (plain base64 without the `data:` prefix, a PNG data URL, an ID longer than 64
characters) is rejected with `422`.
<!-- only esp32 esp32-s3 -->

A web address (`http://` or `https://`) is accepted, but shows no icon and keeps no space for it.
<!-- /only -->

Icon files and data URLs must be JPEG or GIF. PNG and BMP do not work there.<!-- only tc002 --> A web address can
also point to a PNG.<!-- /only --> An icon lower than the display is centered vertically.

- A **JPEG** always takes an 8×8 square. A larger JPEG is not rejected, but only its top-left
  8×8 part is shown. Make JPEG icons 8×8.
- A **GIF** keeps its own width and height, up to the size of the display.
<!-- only tc002 -->
- A **web address** always takes a 16×16 square, as tall as the display. The square is there at
  once and stays empty until the picture has arrived.
<!-- /only -->

An `icon` that is not a string is ignored.

An icon narrower than the display takes **its own width plus `iconGap`** on the left: 9 pixels for
an 8-pixel icon with the default gap. Text, bar chart and line chart start after it. The
progress bar starts right at the icon's edge and runs under the gap.
Scrolling text disappears at the edge of that space, so the gap stays clear while text moves.
Static text is not cut there, and a negative `textOffsetX` can still move it into the gap.

A GIF as wide as the display is used as a **background** instead: it starts at x=0 behind the text,
takes no space, and replaces the `backgroundColor` and any `effect`. An icon that is missing or
cannot be loaded is left out, and everything else is laid out as if there were no icon.

Transparent GIF pixels are **black** on the first frame of an animation. On later frames they
keep what the previous frame showed.

### `iconMode`

| Value | Behavior |
|---|---|
| `fixed` | The icon stays. The text scrolls out behind the gap |
| `pushOnce` | Scrolling text pushes the icon off to the left **once**. It stays gone, and the text then restarts at x=0 |
| `push` | The icon is pushed out and **comes back** on every scroll run |

The icon moves left as the text comes near, until the icon and its gap have left the display:
9 pixels for an 8-pixel icon with the default gap. Without an `icon` this key does nothing.

`iconOffsetX` moves the `icon` left or right only. Its height on the display does not change. It does not move the
space kept for text and charts. A positive `iconOffsetX` therefore moves the icon *over* the
text, and the icon covers it. For freely placed images, use `icons` below.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"Long headline that scrolls","icon":"1234","iconMode":"push"}'
```

### `iconGap`

`iconGap` is the number of empty columns between the right edge of the icon and the text. It
counts from the icon's real width, so a 12-pixel GIF with the default gap puts the text at
column 13. `0` puts the text right next to the icon: useful when the icon image already ends in
an empty column. The gap moves with the icon under `push` and `pushOnce`, and scrolling text
never runs into it.

The value must be a whole number from 0 to 128. Anything else (a negative number, a fraction, a
string or `null`) gets `422 validationFailed` with `field` = `iconGap`.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"Long headline that scrolls","icon":"1234","iconGap":2}'
```

### Multiple icons

Pushed apps and notifications take the same optional `icons` array:

```json
{
  "icons": [
    {"icon": "weather", "x": 0, "y": 0},
    {"icon": "mail", "x": 12, "y": 0},
    {"icon": "music", "x": 24, "y": 0}
  ]
}
```

Each object needs an `icon` string that is not empty, in the same form as the normal `icon` key
(ID or data URL). `x` and `y` are optional whole numbers from −65535 to 65535, default 0. Parts
outside the display are cut off. Each image keeps its own size and must be no larger than the
display.

Each icon animates on its own, even two copies of the same image, each with its own colors and
frame times. They are drawn in array order, after the text, drawings and the normal `icon`, and
before the overlay. Later icons cover earlier ones. They take no space from the text and do not
move with `iconMode`. You can still use `icon` for an image next to the text or a full-width GIF
background.

There are at most **4 extra icons per app or notification**. More than 4, a wrong coordinate, a
missing `icon` or an unknown key rejects the whole request with `422 validationFailed` and a field
such as `icons[1].x`.

If an image cannot be loaded, only that icon is left out. A missing image is shown once it is
uploaded. When memory is short, AWTRIX tries again after five seconds. An app or a notification
with several extra icons may take a few frames to show all of them. Their animations start
together once all are there.

Send `"icons": []` to remove a pushed app's extra icons. In a script, call `icon(name, x, y)`
several times in `draw()`. See [Display and drawing](../guides/scripting/drawing.md#panel-and-drawing).

## Timing & how long an app lives {#timing-how-long-a-page-lives}

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `durationMs` | long | ms | `0` | How long the app or notification is shown. 0 or less uses the global `appDurationMs` (7000). Works for pushed apps and notifications |
| `lifetimeMs` | long | ms, 0 = forever | `0` | **Pushed apps only.** Remove or mark the app after this time |
| `lifetimeExpiry` | string | `remove` · `mark` | `remove` | What happens when the lifetime runs out |
| `repeat` | int | `0` = off | `0` | How many times scrolling text runs across the display |

### `durationMs`

`durationMs` sets how long the app is shown, for **both** pushed apps and notifications. With 0
or less, the global `appDurationMs` setting (7000 ms) is used. So one pushed app can stay longer
or shorter than the others without changing the global setting.

For a notification with `hold: true`, `durationMs` is ignored.

### `lifetimeMs` and `lifetimeExpiry`

The lifetime counts from the moment AWTRIX received the app. 0 or less means it never runs out.
A notification accepts both keys but ignores them: only pushed apps run out.

- **`lifetimeExpiry: "remove"`** (default): the app is deleted and leaves the rotation.
- **`lifetimeExpiry: "mark"`**: the app stays in the rotation with a 1-pixel dark-red
  (`#6E0700`) frame around it, to show it is out of date.

```bash
# Disappear after 5 minutes
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/temp \
  -H 'Content-Type: application/json' \
  -d '{"text":"Transient","lifetimeMs":300000,"lifetimeExpiry":"remove"}'
```

### `repeat`

`repeat` sets how many times scrolling text runs across the display. The app or notification then
stays exactly that long: the text is never cut off halfway, and it does not stay on after the text
has been read.

The default is **`0`**: off. Then it stays for `durationMs` (or the global
`appDurationMs`), however much text is left. `repeat: 1` shows long text once from start to end,
`repeat: 2` twice, and so on. What counts as one run depends on the scroll mode. See
[Scrolling](#scrolling).

Two cases where `repeat` does not decide:

- **The text does not move**, because it fits or the mode is `static`. There is nothing to count,
  so `durationMs` (or the global `appDurationMs`) is used.
- **You set `durationMs` yourself.** It then stays at least that long.
  `{"text":"...","repeat":1,"durationMs":20000}` is shown for 20 seconds even if the text is read
  in 4.

It works the same for pushed apps and notifications.

## Background

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `backgroundColor` | color | - | none → black | Background color |

`backgroundColor` is ignored when an `effect` is set: the effect fills the whole background.

## Charts

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `barChart` | array of int | max 16 entries | `[]` | Values for a bar chart |
| `lineChart` | array of int | max 16 entries | `[]` | Values for a line chart |
| `chartAutoscale` | bool | - | `true` | Scale to the largest value. `false` fixes the top at 8 |
| `chartColor` | color | - | the text color | Color of the bars and the line |

You can use `barChart` and `lineChart` together. Both are drawn. Both take at most 16 entries (more are ignored) and both use `chartAutoscale`. Entries that are not numbers count as 0.

- **`chartAutoscale: true`**: the chart goes from the smallest to the largest value, but the top
  is at least 1 and the bottom at most 0. So all-positive values go from `0` to the largest. The
  range only goes below 0 when a value is negative.
- **`chartAutoscale: false`**: the range is fixed at `0` to `8`: a value of 8 fills the full
  height, and values outside the range are cut to fit.

**`barChart`** shares the space right of the icon evenly between the values. Each bar is at least
1 pixel wide, with a 1-pixel gap between bars. Bars start at the row where **zero** is: with only
positive values that is the bottom row. With a negative value, the zero line moves up. Positive
bars grow up from it and negative bars hang below it.

**`lineChart`** needs at least **2 values**: a single value draws nothing. The points are spread
evenly across the space right of the icon and joined from left to right. The lowest value is on
the bottom row and the highest on the top row.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/traffic \
  -H 'Content-Type: application/json' \
  -d '{"barChart":[2,5,3,8,6,4,7,1],"chartColor":"#00FF00","chartAutoscale":true}'
```

`chartColor` colors both charts. Empty parts of the chart are not painted. The background or
effect shows through.

## Progress bar

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `progress` | int | percent, below 0 = off | `-1` | How full the bar is |
| `progressColor` | color | - | `#00FF00` | The filled part |
| `progressTrackColor` | color | - | `#FFFFFF` | The empty part |

The bar is drawn on the **bottom row only**. With an icon it starts right at the icon's edge (x=8 for an 8-pixel icon), otherwise at x=0. It also covers the [`iconGap`](#icongap), so it
starts a little further left than text and charts. Values above 100 count as 100.
`progress: 0` draws the whole row in `progressTrackColor`, and a value below 0 draws nothing.
The filled part covers that percentage of the bar, and the rest of the row is the track.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/download \
  -H 'Content-Type: application/json' \
  -d '{"text":"64%","progress":64,"progressColor":"#00AAFF","progressTrackColor":"#202020"}'
```

## Effects

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `effect` | string | any case | `""` | Name of an animated background effect |
| `effectSpeed` | float | 0.1 - 10.0 | `1.0` | Speed of the effect and of the app's overlay |

Upper and lower case do not matter in `effect`: `Plasma`, `plasma` and `PLASMA` are the same. An
unknown name gets `422 {"code":"validationFailed","field":"effect"}` and nothing is saved. An
empty string means no effect. `backgroundColor` or black is used.

The 19 effects and what each one looks like are listed in
[Visual reference → Background effects](visuals.md#background-effects). The current list is in
`GET /api/v1/capabilities` → `effects`.

### `effectSpeed`

**`effectSpeed`** multiplies the speed of the animation: `2.0` is twice as fast, `0.5` half as
fast. Values are **kept within 0.1 - 10.0**: `0` and negative values become `0.1` (very slow, but
never fully stopped), and values above `10.0` become `10.0`. It sets the speed of the app's
effect **and** of an overlay the app sets itself.

The colors come from the app's [`palette`](#palette), the same one used for text and charts.
Which effects use a palette is listed in `GET /api/v1/capabilities` -> `paletteEffects`. The
others keep their own colors.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/ambient \
  -H 'Content-Type: application/json' \
  -d '{"effect":"Plasma","effectSpeed":0.5,"palette":"Lava"}'
```

Palette and speed belong to the app that sets them. Two apps with `Plasma` and different palettes
each keep their own colors, and an app without these keys gets the defaults.

## Palette

One palette per app, shared by the text, the charts, the progress bar and the effect. You name
it once.

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `palette` | string \| array \| null | max 16 stops | none | Name of a built-in or custom palette, or a list of colors |
| `paletteBlend` | bool | - | `true` | Smooth blend between the 16 colors. `false` gives hard stripes |
| `paletteSpan` | int | px, 0 = stretch | `0` | Pixels per full palette run when coloring text |
| `paletteSpeed` | float | 0.0 - 10.0, 0 = still | `0` | Palette runs per second when coloring text |

**`palette` as a name**: upper and lower case do not matter. AWTRIX first looks for the file
`/PALETTES/<name>.txt`, then for one of the 8 built-in palettes: `Cloud`, `Lava`, `Ocean`,
`Forest`, `Stripe`, `Party`, `Heat`, `Rainbow`. So a file with a built-in name replaces that
built-in for as long as the file exists: this is how you change a built-in palette. A name that
matches neither gets `422 {"code":"validationFailed","field":"palette"}`. AWTRIX does not quietly
use another palette. `null` and `""` remove the palette.

**`palette` as an array**: 1 to 16 colors (**stops**), spread evenly over the 16 palette
entries with smooth blending. Two stops make a full blend from the first to the second color.
An empty array, or one with more than 16 colors, is rejected.

An element may also be `{"color": <color>, "pos": 0-100}`. This puts that color at a percentage
of the palette instead of spreading the colors evenly, so `pos` sets how much room each color
gets. Both keys are required. You cannot mix both forms in one array. Stops are sorted by
position, the first and last color extend to the ends, and two stops at the same position make a
hard edge. A palette file writes the same as `RRGGBB@70`. See
[Visual reference → Custom palettes](visuals.md#custom-palettes).

To use the palette for something, set that thing's color key to `"palette"`:

| Key | What picks the color |
|---|---|
| `textColor` | the pixel column across the text |
| `chartColor` | each bar's or point's value, within the chart's range |
| `progressColor` | the position along the bar. The filled part shows the palette up to `progress` |

`progressTrackColor` is always a plain color.

### Painting text

The palette is applied **per pixel column**, so the colors spread evenly whatever the letter
widths are.

- **`paletteSpan: 0`** (default) stretches the palette once from the first to the last lit
  column: the text starts with the first color and ends with the last.
- **`paletteSpan: N`** repeats the palette every `N` pixels, without a visible seam. Long
  scrolling text keeps changing color instead of looking flat.

**`paletteSpeed`** moves the colors along the text at that many palette runs per second. With a
speed above `0`, a `paletteSpan: 0` palette repeats across the width of the text.

```bash
# Gradient stretched across the text
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hot \
  -H 'Content-Type: application/json' \
  -d '{"text":"OVERHEAT","palette":["#FFFF00","#FF0000"],"textColor":"palette"}'

# Repeating, moving rainbow
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/party \
  -H 'Content-Type: application/json' \
  -d '{"text":"PARTY TIME","palette":"Rainbow","textColor":"palette","paletteSpan":24,"paletteSpeed":1}'

# Bars colored by value
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/load \
  -H 'Content-Type: application/json' \
  -d '{"barChart":[3,5,2,8,6,9],"palette":"Heat","chartColor":"palette"}'
```

The `palettes` list in `GET /api/v1/capabilities` shows only the eight built-in names, not your
`/PALETTES/*.txt` files. Your files still work by name. List them with
`GET /api/v1/files?dir=/PALETTES`.

## Overlay

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `overlay` | string | any case | `""` | Weather overlay drawn on top of everything |

Upper and lower case do not matter: `"SNOW"` and `"snow"` are the same. The 6 overlays are
`rain`, `snow`, `drizzle`, `storm`, `thunder` and `frost`. What each looks like is in
[Visual reference → Weather overlays](visuals.md#weather-overlays). An unknown name gets
`422 {"code":"validationFailed","field":"overlay"}` and nothing is saved, so a typo cannot
silently turn off the global overlay.

An empty `overlay` uses the global overlay set with `PATCH /api/v1/display`. An app's own overlay
**wins** over the global one. Overlays are drawn **last**, on top of text and drawings.

Overlays follow the app's `effectSpeed` and `palette`: the speed sets how fast they move, and a
palette changes their colors. Without a palette they keep their normal colors.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/forecast \
  -H 'Content-Type: application/json' \
  -d '{"text":"4C","icon":"2422","overlay":"snow"}'
```

## Draw commands

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `draw` | array of arrays | no limit | `[]` | Drawing commands. Each is an array starting with the command name |

| Command | Arguments |
|---|---|
| `["pixel", x, y, color]` | One pixel |
| `["pixels", color, x1, y1, x2, y2, …]` | Many pixels in one color |
| `["line", x1, y1, x2, y2, color]` | A line, both end points included |
| `["rect", x, y, w, h, color]` | A 1-pixel outline from `x` to `x+w-1` |
| `["rectFill", x, y, w, h, color]` | A filled rectangle |
| `["circle", cx, cy, r, color]` | A circle: center and radius |
| `["circleFill", cx, cy, r, color]` | A filled circle |
| `["text", x, y, "HI", color]` | Text. With `y` 1 it sits on the same rows as the text of the `text` key<!-- only tc002 -->, in an app drawn at double size<!-- /only --> |
| `["bitmap", x, y, w, h, data]` | An image. `data` is base64 RGB888 or an array of colors |

- You may leave out the last `color`. The command then uses the app's text color. `pixels` takes
  its color first, and `null` there means the same.
- Commands are drawn in array order.
- Pixels outside the display are cut off. They do not wrap around.
- `w` or `h` of zero or less draws nothing, and so does a negative radius. A radius of `0` draws
  the center pixel.
- A `bitmap` with too few colors leaves the rest empty. Extra colors are ignored.

`text` here is UTF-8 like the `text` key and uses the same `font`, but `textCase`,
`palette`, `textBlinkMs`, `textFadeMs`, `textAlign` and the global `uppercase` setting do not
apply to it. In `small`, `y` is the top row of the capitals.

`bitmap` data comes in two forms: an array of `w × h` colors, row by row, in any of the
[color forms](#colors). Or a base64 string of `w × h × 3` bytes (red, green, blue for each
pixel). The base64 form is much smaller for a large image.

The number of commands is limited only by the request size limit (<!-- only esp32 esp32-s3 -->8192 bytes<!-- /only --><!-- only tc002 -->2 MiB<!-- /only -->).
See [Keeping payloads small](../guides/graphics.md#keeping-payloads-small).

```bash
# A frame with a filled circle and a label
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/art \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["rect",0,0,32,8,"#202020"],
        ["circleFill",4,4,2,"#F00"],
        ["text",9,1,"HI"]
      ]}'
```

## Notification-only keys

These 5 keys work **only** with `POST /api/v1/notifications`. A pushed app that sends one gets
`422 validationFailed` (a pushed app's name comes from its URL, not from the body).

| Key | Type | Range | Default | Meaning |
|---|---|---|---|---|
| `name` | string | - | `""` | A name to [remove this notification by](#notifications). Must match exactly, use characters that are safe in a URL |
| `hold` | bool | - | `false` | Stay until closed. Never close by itself |
| `stack` | bool | - | `true` | Wait in line behind other notifications. `false` replaces the current one |
| `wakeup` | bool | - | `false` | Show even while the display is switched off |
| `sound` | string \| object \| array | - | none | A sound to play when the notification appears. See [Sound](#sound) |

### `hold` and `stack`

**`hold: true`**: the notification never closes by itself. `durationMs` and `appDurationMs` are
ignored. It stays until `DELETE /api/v1/notifications/active`. Notifications waiting behind it
wait until then.

**`stack: true`** (default): the notification joins the end of the queue and is shown after the
ones before it. If the [queue is full](limits.md#apps-and-notifications), it gets
`507 insufficientStorage`.

**`stack: false`**: the notification **replaces** the one shown. The ones waiting behind it
stay. Scrolling, icon and sound start from the beginning. With an empty queue it is simply shown.

```bash
# Interrupt whatever is on screen right now
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"URGENT","stack":false,"textColor":"#FF0000"}'
```

### `wakeup`

Shows the notification even while the display is switched off with
`PATCH /api/v1/display {"power":false}`. The display stays on for as long as the `wakeup`
notification is shown.

### Sound

`sound` takes the [sound object](http.md#the-sound-object) of `POST /api/v1/audio/play`: a stored
name, an object with one source key, or a list of 1 to 4 of them.<!-- only esp32-s3 tc002 --> `station` is not allowed here.<!-- /only -->
The sound plays as an alert, at the alert volume, when the notification appears.

| `sound` | Plays |
|---|---|
<!-- only esp32 -->
| `"ding"` | the melody `/MELODIES/ding.txt` |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `"ding"` | the MP3 `/MP3/ding.mp3`, else the melody `/MELODIES/ding.txt` |
| `{"file":"Racer/boost"}` | the sound `boost` of the script `Racer` |
<!-- /only -->
| `{"rtttl":"bell:d=4,o=5,b=120:c,e,g"}` | the melody in the request |
<!-- only tc002 -->
| `{"speech":"The door is open."}` | the text, read aloud (needs a voice) |
| `[{"speech":"The door is open."},"ding"]` | the first entry the clock can play |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `{"track":3}` | DFPlayer track 3 |
| `[{"track":3},"ding"]` | the first entry the clock can play |
<!-- /only -->
| `""` or `null` | no sound |

- A notification sent by a script with `notify()` looks for a plain name in the script's own
  folder first.
- `sound` is checked when the notification arrives. A sound with a mistake, for example a melody
  that cannot be read, gets `422 validationFailed` and the notification is not shown. `field`
  starts with `sound`: `sound.rtttl`, or `sound[1].file` inside a list. A number is not a sound:
  `"sound": 5` answers `must be a string, object or list`.
- A name that is not stored, or a sound the clock cannot play, is not an error. The notification
  is shown without sound.

**`"loop": true`** inside the sound plays it again each time it ends, for as long as the
notification is shown. When the notification leaves the display, the sound stops at once. Without
`loop`, a sound plays to its end. Together with `hold`, this makes an alarm that plays until you
close it:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALARM","hold":true,"sound":{"rtttl":"siren:d=8,o=5,b=200:c,g,c,g","loop":true}}'
```

## Render order

Each frame is drawn in this order. Later layers cover earlier ones:

1. **Background**: the `effect` if set, otherwise `backgroundColor` or black.
2. **Text and drawings**: with `textInFront`, drawings first and then text. Otherwise text
   first. Drawings are always in this order: `draw` commands → progress bar → bar chart → line
   chart.
3. **Out-of-date frame**: the dark-red frame of an expired `lifetimeExpiry: "mark"` app.
4. **Icon**: centered vertically, moved by `iconOffsetX` and `iconMode`. A full-width GIF icon is the
   background instead.
5. **Extra icons**: the `icons` entries, in array order, at their `x`, `y` positions.
6. **Overlay**: the app's overlay if set, else the global one.

## Worked example

Everything at once: a pushed app with an icon, a gradient, a chart, a progress bar, an effect
and an overlay:

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

## Errors

One rule: **the whole payload is applied, or none of it.** When a request fails nothing is saved.

The keys that take a number or `true`/`false` (all but `iconGap`), and `text`, `icon`, `effect`,
`overlay` and `name`, do not refuse a value of another type: the value is ignored and the key
keeps its default. `"hold":"yes"` gives a notification without `hold`, `"textInFront":1` keeps
`false`, and `"repeat":"many"` keeps `0`. These problems are refused:

| Problem | Answer |
|---|---|
| Body is not valid JSON | `400 invalidJson` |
| Body over <!-- only esp32 esp32-s3 -->8192 bytes<!-- /only --><!-- only tc002 -->2 MiB<!-- /only --> | `413 payloadTooLarge` |
| Unknown top-level key | `422 validationFailed`, `field` = the key |
| A color that cannot be read, anywhere | `422 validationFailed`, `field` = the key |
| A word key given a word that is not in its list | `422 validationFailed`, `field` = the key |
| Unknown `effect` or `overlay` name | `422 validationFailed`, `field` = `effect` / `overlay` |
| A notification `sound` with a mistake | `422 validationFailed`, `field` = `sound` or `sound.<key>` |
| Unknown draw command | `422 validationFailed`, `field` = `draw[<i>]` |
| A draw command that is not an array | `422 validationFailed`, `field` = `draw[<i>]` |
| Wrong number of arguments for a draw command | `422 validationFailed`, `field` = `draw[<i>]` |
| A coordinate, size or radius that is not a number | `422 validationFailed`, `field` = `draw[<i>]` |
| `pixels` with an odd number of coordinates | `422 validationFailed`, `field` = `draw[<i>]` |

Drawing outside the display is **not** an error: the parts outside are cut off. A shape with zero
width or height is not an error either. It just draws nothing (see
[Draw commands](#draw-commands)).<!-- only tc002 --> With `layout`, each region must fit inside the display. What is
drawn inside a region is cut off at its edges.<!-- /only -->

## Limits

Every cap a payload can reach (request size, number of pushed apps, notification queue length
and chart points) is listed with its error in [Limits](limits.md#apps-and-notifications).

## Related

- [HTTP API](http.md)
- [MQTT API](mqtt.md)
- [Visual reference](visuals.md)
- [Limits](limits.md)
- [Errors](errors.md)
