# Charts & drawing

Anything you can put on the matrix that is not text or an icon comes from four keys in the
app/notification payload: `progress`, `barChart`, `lineChart` and `draw`. They live in the same JSON
body as everything else. (`backgroundColor`, `effect` and `overlay` also paint the canvas,
independently of these four - see [Layering](#layering).)

Start here - a download progress bar with a label:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/download \
  -H 'Content-Type: application/json' \
  -d '{"text":"64%","progress":64,"progressColor":"#00AAFF","progressTrackColor":"#202020"}'
```
<!-- shot:begin id=64-progress hash=777efbfc -->
![The panel showing "64%", a 64% progress bar along the bottom row](../assets/shots/graphics/64-progress.png){ .shot }
<!-- shot:end -->


**What you see:** `64%` on the middle rows, and along the very bottom row of the panel a bar that is
bright blue for the left ~20 pixels and dark grey for the remaining ~12.

Every example on this page sends `Content-Type: application/json`, which is
[mandatory on every write](../reference/conventions.md#content-type-is-mandatory). For the
per-key tables - types, ranges, defaults - see [App & notification payload](../reference/payload.md).

---

## The canvas

Every coordinate on this page refers to the same grid:

- **x** runs `0` … *panel width* − 1 left to right (`31` on the default 32-wide panel; wider on
  multi-panel setups, see [Panel and orientation](../reference/system.md#panel-and-orientation)),
  **y** runs `0` … `7` top to bottom. `(0,0)` is top-left.
- Off-canvas pixels are dropped, never wrapped to the other side. You can safely draw a circle that
  hangs off the edge.
- Colors accept `"#RRGGBB"`, `"RRGGBB"`, `"RGB"` shorthand, `[r,g,b]`, `["HSV",h,s,v]`, or a packed
  integer - everywhere, with no exceptions. See [Colors](../reference/payload.md#colors).

An `icon` takes the leftmost 9 pixels and pushes the charts across - see
[Combining with an icon](#combining-with-an-icon).

---

## Draw a progress bar

`progress` is a percentage. It paints the **bottom row only**.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/build \
  -H 'Content-Type: application/json' \
  -d '{"text":"BUILD","progress":30,"progressColor":"#00FF00","progressTrackColor":"#FFFFFF"}'
```
<!-- shot:begin id=build-progress hash=3012488b -->
![The panel showing "BUILD", a 30% progress bar along the bottom row](../assets/shots/graphics/build-progress.png){ .shot }
<!-- shot:end -->


**What you see:** `BUILD` across the panel, and on row 7 a green segment covering the leftmost ~9
pixels with white filling the rest of the row all the way to x=31.

- `progressColor` paints the filled part of the row, `progressTrackColor` the remainder. The track
  is drawn, not left transparent - if you want the bar to disappear into the background, set
  `progressTrackColor` to your background color.
- `progress: 0` is **not** "no bar" - it draws a full row of `progressTrackColor`. Use `-1` (or omit
  the key) to turn the bar off. Anything above 100 clamps to 100.

Defaults and ranges: [Progress bar](../reference/payload.md#progress-bar).

---

## Draw a bar chart

`barChart` takes an array of integers, one per bar, up to 16 of them.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/traffic \
  -H 'Content-Type: application/json' \
  -d '{"barChart":[2,5,3,8,6,4,7,1],"chartColor":"#00FF00"}'
```
<!-- shot:begin id=barchart hash=a9e11e89 -->
![The panel showing a bar chart of 8 columns](../assets/shots/graphics/barchart.png){ .shot }
<!-- shot:end -->


**What you see:** eight green columns filling the whole panel width, evenly spaced with a gap
between them, all growing up from the bottom row. The tallest (`8`) reaches the top of the panel;
the shortest (`1`) is a single pixel on the bottom row. The rest are proportional in between.

**Sizing.** The bars split the available width evenly between them, separated by a 1px gap, so more
bars means narrower bars. Add an icon and they share the narrower space left beside it.

**Scaling.** `chartAutoscale` (default `true`) decides what "full height" means:

- **`true`** - the largest value in your data becomes full height. `[2,5,3,8]` and `[20,50,30,80]`
  render **identically**. Good for showing shape; bad for comparing two updates against each other.
- **`false`** - the max is fixed at **8**. A value of 8 fills the panel, and anything larger is
  clamped flat to the top. Use this when the absolute number matters.

```bash
# Same data, absolute scale - values are read against a fixed max of 8
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/traffic \
  -H 'Content-Type: application/json' \
  -d '{"barChart":[2,5,3,8,6,4,7,1],"chartAutoscale":false,"chartColor":"#00FF00"}'
```
<!-- shot:begin id=barchart-2 hash=7654a47f -->
![The panel showing a bar chart of 8 columns](../assets/shots/graphics/barchart-2.png){ .shot }
<!-- shot:end -->


**Color.** `chartColor` sets the bars - and the line, if the page also has one. Omit it and both use
the resolved text color (your `textColor` key, or the global `textColor` setting). Cells that are
not part of a bar are not painted at all; they keep whatever the background or effect put there.

Negative values are allowed. Under the default autoscale they still draw a visible bar below the
zero line; with `chartAutoscale: false` anything at or below zero draws nothing at all. A 17th
entry and beyond are dropped.

Full table: [Charts](../reference/payload.md#charts).

---

## Draw a line chart

`lineChart` takes an array of integers and plots a polyline across the panel. It needs at least
**2 points** - a 1-element array draws nothing.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/temp \
  -H 'Content-Type: application/json' \
  -d '{"lineChart":[3,4,6,5,7,8,6,4],"chartColor":"#FF8800"}'
```
<!-- shot:begin id=linechart hash=a4027603 -->
![The panel showing a line chart](../assets/shots/graphics/linechart.png){ .shot }
<!-- shot:end -->


**What you see:** an orange zig-zag stretching the full width - the first point sits at x=0, the
last at x=31, with the six others spaced evenly between, and the segments drawn as 1px lines
connecting them.

It shares the 16-entry cap, `chartAutoscale` and `chartColor` with `barChart`. Send both in one
payload and both are drawn, in that one shared color:

```bash
# Bars behind, line on top, in one page
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/combo \
  -H 'Content-Type: application/json' \
  -d '{"barChart":[2,5,3,8,6,4,7,1],"lineChart":[2,5,3,8,6,4,7,1],"chartColor":"#00FF00"}'
```
<!-- shot:begin id=barchart-linechart hash=3461b85f -->
![The panel showing a bar chart of 8 columns, a line chart](../assets/shots/graphics/barchart-linechart.png){ .shot }
<!-- shot:end -->


**What you see:** eight green columns with a line of the same green tracing across their tops.

Full table: [Charts](../reference/payload.md#charts).

---

## Draw commands

`draw` is an array of commands. Each command is itself an array, with the **command name first**:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/art \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["rect",0,0,32,8,"#202020"],
        ["circleFill",4,4,2,"#F00"],
        ["text",9,1,"HI"]
      ]}'
```

**What you see:** a dark grey frame around the whole panel, a small red disc near the left edge, and
`HI` in your app's text color beside it - the label takes that color because the command leaves its
color out.

Two rules cover the whole list:

- **Array order is draw order.** The last command wins wherever two overlap.
- **A malformed command is rejected.** A misspelled name, a wrong argument count, a coordinate that
  is not a number - each gives `422 validationFailed` with `"field":"draw[<index>]"`, and nothing at
  all is stored.

The trailing `color` is optional everywhere: leave it off and the command uses your app's text
color. `pixels` takes its color **first** instead, where `null` means the same thing.

There are nine commands, each with an example below: [`pixel`](#draw-a-pixel),
[`pixels`](#draw-many-pixels), [`line`](#draw-a-line), [`rect` and `rectFill`](#draw-a-rectangle),
[`circle` and `circleFill`](#draw-a-circle), [`text`](#draw-text) and [`bitmap`](#draw-a-bitmap).
Argument order for each: [Draw commands](../reference/payload.md#draw-commands).

### Draw a pixel

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/corners \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["pixel",0,0,"#F00"],
        ["pixel",31,0,"#0F0"],
        ["pixel",0,7,"#00F"],
        ["pixel",31,7,"#FF0"]
      ]}'
```

**What you see:** four single lit LEDs, one in each corner of an otherwise black panel - red
top-left, green top-right, blue bottom-left, yellow bottom-right. This is the fastest way to
confirm your coordinate system and your panel wiring are right.

### Draw many pixels

`pixels` names one color and then lists coordinates in `x, y` pairs. Use it whenever a drawing is a
scattering of points in the same color - it is far shorter than one `pixel` command each.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/stars \
  -H 'Content-Type: application/json' \
  -d '{"draw":[["pixels","#FFF",2,1,7,0,13,3,19,1,25,2,29,5]]}'
```
<!-- shot:begin id=draw hash=4af494ee -->
![The panel showing drawn shapes](../assets/shots/graphics/draw.png){ .shot }
<!-- shot:end -->


**What you see:** six white points scattered across the upper half of the panel, like a small
starfield.

A trailing coordinate with no partner is an error - `["pixels","#FFF",0,0,1]` is rejected.

### Draw a line

Both endpoints are included.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/cross \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["line",0,0,31,7,"#F0F"],
        ["line",0,7,31,0,"#0FF"]
      ]}'
```

**What you see:** a magenta diagonal from the top-left corner down to the bottom-right, crossed by
a cyan diagonal running the other way - a large X filling the panel, the two lines overlapping near
the middle where the cyan (drawn second) wins.

### Draw a rectangle

`rect` is a 1px outline, `rectFill` is solid. `w` and `h` are a **size, not a second coordinate**:
a rect at `x=0` with `w=32` spans x=0 … x=31.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/box \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["rectFill",0,0,32,8,"#000040"],
        ["rect",2,1,12,6,"#FFF"],
        ["rectFill",18,2,10,4,"#F00"]
      ]}'
```

**What you see:** the whole panel washed dark blue; a hollow white rectangle on the left (12 wide,
6 tall, with the dark blue showing through its middle); and a solid red block on the right.

A `w` or `h` of zero or less draws nothing, and is not an error.

### Draw a circle

`cx`/`cy` are the **center**, not a bounding-box corner, and `r` is the radius.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/rings \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["circle",8,4,3,"#0F0"],
        ["circleFill",24,4,3,"#F00"]
      ]}'
```

**What you see:** a hollow green ring roughly 7px across on the left half, and a solid red disc the
same size on the right half, both vertically centred.

- `r = 0` draws the single center pixel.
- A negative radius draws nothing.
- At these sizes a filled circle looks chunky - a radius-2 disc is a 5px-wide plus-shape with
  corners.

### Draw text

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/label \
  -H 'Content-Type: application/json' \
  -d '{"draw":[
        ["rectFill",0,0,32,8,"#101010"],
        ["text",1,1,"12:30","#FA0"]
      ]}'
```

**What you see:** a dim grey panel with amber `12:30` sitting near the top-left.

A drawn label's `y` is the glyph **top**, not the baseline: the baseline lands at `y + 5`, so `y: 1`
puts the glyph roughly at rows 1–6. The main `text` key uses the other convention, with its baseline
fixed at row 6.

A drawn label is also **raw**. Unlike the main `text` key it is not affected by `textCase`,
`palette`, `textBlinkMs`, `textFadeMs`, `textCenter`, or the global `uppercase`
setting. It is UTF-8 like every other text field. For styled, scrolling, centred
text use `text` instead - see [Text & colors](text.md).

### Draw a bitmap

A row-major blit of `w × h` pixels starting at `(x, y)`. The data is either an array of colors - in
any of the usual [color forms](../reference/payload.md#colors) - or a base64 string of raw RGB888
bytes.

```bash
# A 4x2 checkerboard of red and green at the top-left
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/bmp \
  -H 'Content-Type: application/json' \
  -d '{"draw":[["bitmap",0,0,4,2,[
        "#F00","#0F0","#F00","#0F0",
        "#0F0","#F00","#0F0","#F00"
      ]]]}'
```

**What you see:** a small 4×2 patch in the top-left corner alternating red and green, with the
second row offset from the first.

A **short** array is not an error - the blit just stops, leaving the remaining cells untouched.
Extra entries beyond `w × h` are ignored. For anything approaching a full panel, send base64
instead; see [Keeping payloads small](#keeping-payloads-small).

---

## Keeping payloads small

A request body is capped at 8 KB - see [Limits](../reference/limits.md#requests). Four habits keep a
busy drawing well inside it.

**Short hex.** `#F00` is the same red as `#FF0000` and four characters shorter. Every digit is
doubled, so `#1A2` means `#11AA22`.

**Leave the color out.** A command without a trailing color uses your app's text color. If most of a
drawing is one color, set `textColor` once and drop it everywhere else.

**Group your points.** Drawing many single pixels one command at a time is the most expensive thing
you can do. `["pixels","#0F0",0,0,1,1,2,2]` names the color once and then lists coordinates in
pairs - a hundred points fit in about a quarter of the space.

**Send bitmaps as base64.** For a full-panel image, the base64 form of `["bitmap", …]` is
dramatically smaller than listing every pixel as its own color.

---

## Layering

Within one page, decorations are always painted in this order: **`draw` commands → `progress` →
`barChart` → `lineChart`**. So a line chart paints over a bar chart, and both paint over your draw
commands. The icon goes on next, and an `overlay` (`rain`, `snow`, …) on top of everything.

`textInFront` flips one thing - whether the **text** goes under or over that decoration group:

| `textInFront` | Order |
|---|---|
| `false` (default) | text first, decorations painted **over** it |
| `true` | decorations first, text painted **on top** |

It changes z-order only; the text baseline is row 6 either way.

```bash
# Text on top of a bar chart instead of behind it
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/cpu \
  -H 'Content-Type: application/json' \
  -d '{"text":"CPU","textInFront":true,"barChart":[2,5,3,8,6,4,7,1],"chartColor":"#040"}'
```
<!-- shot:begin id=cpu-barchart hash=9db63956 -->
![The panel showing "CPU", a bar chart of 8 columns](../assets/shots/graphics/cpu-barchart.png){ .shot }
<!-- shot:end -->


**What you see:** dark green bars filling the panel with white `CPU` legible on top of them. Drop
`textInFront` and the bars paint over the letters instead.

Underneath all of that sits the background: the `effect` animation if one resolves, otherwise
`backgroundColor` or black. An effect owns the whole canvas, so `backgroundColor` is ignored while
one is active - see [Effects & overlays](effects.md).

Full order: [Render order](../reference/payload.md#render-order).

---

## Combining with an icon

An `icon` reserves 9px on the left - 8px of icon and a 1px gap. `barChart` and `lineChart` respect
it and start at `x = 9`; `progress` starts at `x = 8`; draw commands ignore it completely and use
raw coordinates, so they will paint over the icon.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"icon":"11201","lineChart":[3,4,6,5,7,8,6,4],"chartColor":"#FF8800"}'
```
<!-- shot:begin id=11201-linechart hash=bf7c13bf -->
![The panel showing an icon on the left, a line chart](../assets/shots/graphics/11201-linechart.png){ .shot }
<!-- shot:end -->


**What you see:** the icon occupying the leftmost 8 pixels, then a 1px gap, then the orange line
chart plotted across the remaining 23 pixels.

If the icon file is missing or fails to decode, charts, text and the scroll model all fall back to
the icon-less layout at `x = 0` rather than leaving a blank column behind. You still get no icon, so
verify the ID.

Only JPEG and GIF are supported for icons. See [Icons & assets](icons.md).

---

## When nothing appears

A request either applies whole or is rejected whole. If you got `200 {"ok":true}`, every key in the
body was accepted, and a missing drawing is down to its geometry rather than a typo.

| Symptom | Likely cause |
|---|---|
| `415 unsupportedMediaType` | Missing `Content-Type: application/json` - `curl -d` sends a form body |
| `413 payloadTooLarge` | Body over the size limit - see [Keeping payloads small](#keeping-payloads-small) |
| `422` with `"field":"draw[2]"` | The third draw command: unknown name, wrong argument count, or a coordinate that is not a number |
| `422` with `"field"` naming a key | A misspelled key name, or a color AWTRIX could not read |
| Line chart missing | Fewer than 2 points in `lineChart` |
| Rectangle missing | `w` or `h` is 0 or negative, or you passed a second coordinate instead of a size |
| Bars all look the same height across updates | `chartAutoscale` is `true` - set it to `false` for an absolute scale |
| Chart shifted 9px right | An `icon` is present and reserving its column |
| Drawing invisible over a busy background | An `overlay` (`rain`, `snow`, …) is painting over it - check [Layering](#layering) |
| Only the 17th and later bars missing | The 16-entry cap on `barChart`/`lineChart` |

Full list of codes: [Errors](../reference/errors.md).

---

## Related

- [App & notification payload](../reference/payload.md) - every key, type, range and default
- [Visual reference](../reference/visuals.md) - colors, palettes, the matrix layout
- [Text & colors](text.md) - the `text` key, styling and scrolling
- [Effects & overlays](effects.md) - animated backgrounds and weather overlays
- [Pushed apps](pushed-apps.md) - lifecycle, expiry, rotation
- [Scripting](scripting.md) - the same marks drawn from your own logic, on AWTRIX itself
