# Visual reference

Every visual name AWTRIX NG understands, exactly as it appears on the wire.

There are **four separate lists of names**, and they do not mix:

| Name list | Where it goes | Case |
|---|---|---|
| [Background effects](#background-effects) | `effect` on a pushed-app / notification payload | insensitive |
| [Weather overlays](#weather-overlays) | `overlay` on a payload, or `overlay` on `PATCH /api/v1/display` | insensitive |
| [Palettes](#palettes) | `palette` on a payload | insensitive |
| [Transitions](#transitions) | `transitionEffect` in `PATCH /api/v1/settings` (device-wide) | insensitive |

All four resolve **case-insensitively** - `"matrix"`, `"Matrix"` and `"MATRIX"` are the same
effect. The spelling used throughout this page is the one
[`GET /api/v1/capabilities`](#discovering-the-names) lists, and the one the API returns.

`Ripple` and `Fade` each appear in two lists - once as a background effect, once as a transition -
and the two behaviours are unrelated. `Random` is a transition name only. Read a name in the
context of the key that carries it.

---

## Background effects

A background effect fills the page **behind** text, icons and charts. Set it per app with the
`effect` key. The name is matched case-insensitively.

```bash
curl -X POST 'http://<awtrix-ip>/api/v1/apps?name=demo' \
  -H 'Content-Type: application/json' \
  -d '{"text":"HELLO","effect":"Pacifica"}'
```

AWTRIX draws **19 background effects**:

| `effect` | What it draws | Honours `palette`? |
|---|---|---|
| `Plasma` | Full-canvas sine plasma, hue sweeping the whole wheel | yes |
| `TheaterChase` | Every third column lit, marching sideways (unlit pixels black) | yes |
| `Fade` | Whole canvas pulsing one colour; stock look is a dark-blue breath | yes |
| `MovingLine` | One full-height vertical line sweeping left → right | yes |
| `BrickBreaker` | Three rows of bricks, white ball, grey paddle on the bottom row | **no** |
| `PingPong` | A single pixel bouncing horizontally and vertically | **no** |
| `Radar` | Sweeping radius line from the centre; stock look is green | yes |
| `Checkerboard` | 2×2-cell checkerboard, inverting every animation step | yes |
| `Fireworks` | An expanding ring burst at a random position and colour every 20 steps | yes |
| `PlasmaCloud` | Softer, slower plasma with a narrower hue band | yes |
| `Ripple` | A single ring expanding from the centre, repeating | yes |
| `Snake` | A 6-pixel snake advancing one pixel per frame, wrapping row by row | yes |
| `Pacifica` | Ocean waves; stock look is blue-teal | yes |
| `Matrix` | Per-column falling green trails, bright-headed, with random phases and trail lengths | **no** |
| `SwirlIn` | A 48-point spiral converging inward | yes |
| `SwirlOut` | A 48-point spiral expanding outward | yes |
| `LookingEyes` | Two 8x8 eyes that glance around and blink every few seconds | **no** |
| `TwinklingStars` | 22 randomly placed stars twinkling on a 30-step cycle | yes |
| `ColorWaves` | Horizontal hue sweep across the panel | yes |

An empty `effect` string, or no `effect` key at all, draws no background.

An effect marked **no** above has fixed colours: sending a `palette` alongside it returns success
and changes nothing on screen.

A name AWTRIX does not know (`"Plasm"`) fails validation with
`422 {"code":"validationFailed","field":"effect"}` and **nothing is stored** - array payloads are
all-or-nothing. Casing is not a typo: `"pacifica"` resolves to `Pacifica`. Check spelling against
[`GET /api/v1/capabilities`](#discovering-the-names).

---

## Effect settings

An effect takes two things from the payload it rides on: a speed of its own, and the app's
[palette](#palettes), which it shares with the text and the charts.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `effectSpeed` | float | `0.1 … 10` | `1.0` | multiplier | Multiplier on the effect's own base rate (clamped) |
| `palette` | string or array | built-in name, file name, or ≤ 16 stops | *unset* - effect keeps its own colours | - | Colour source for palette-driven effects and weather overlays |
| `paletteBlend` | bool | - | `true` | - | Interpolate between palette entries instead of using 16 hard bands |

```bash
curl -X POST 'http://<awtrix-ip>/api/v1/apps?name=demo'   -H 'Content-Type: application/json'   -d '{"text":"HI","effect":"Plasma","effectSpeed":0.4,"palette":"Lava"}'
```

### How `effectSpeed` works

Every effect and overlay has a **base rate**: how many animation steps it takes per second, where
one step is one visible state change - one pixel of travel, one checkerboard flip, one chase offset.
`effectSpeed` is a **multiplier on that base rate**: `2.0` runs an animation twice as fast, `0.5`
half as fast.

The base rates are calibrated against each other, so `1` means the same perceived pace everywhere.
Effects still differ in character - `storm` falls faster than `drizzle`.

A value outside `0.1 … 10` is silently pulled to the nearest bound rather than rejected.

`effectSpeed` drives the resolved background effect **and** an overlay named on the same payload,
so `storm` at `2` falls twice as fast. An overlay set device-wide is tuned separately, through
`overlaySettings` on `PATCH /api/v1/display`.

### How `palette` works

Three accepted forms:

=== "A name"

    ```json
    {"palette": "Heat"}
    ```

    Matched **case-insensitively**, against `/PALETTES/<name>.txt` on the filesystem first and the
    [built-in names](#palettes) second - so a file of that name wins. If neither exists the payload
    is **rejected** with `422 {"code":"validationFailed","field":"palette"}`. A typo does not become
    a built-in palette.

=== "Inline stops"

    ```json
    {"palette": ["#FF0000", "#00FF00", [0,0,255], ["HSV",60,100,100]]}
    ```

    1 to **16** colour **stops**; entries past the 16th are dropped. Each accepts every
    [colour form](#colors). The stops are spread evenly across all 16 palette entries, with the
    colours in between blended, so two stops describe a complete ramp. An **empty array** is
    rejected.

=== "Inline stops with positions"

    ```json
    {"palette": [{"color": "#FF0000", "pos": 0},
                 {"color": "#FFFF00", "pos": 70},
                 {"color": "#FFFFFF", "pos": 100}]}
    ```

    `pos` is `0-100`, where the stop sits across the ramp - the same thing `RRGGBB@70` says in a
    [palette file](#custom-palettes).

    Both keys are required, and the forms cannot be mixed: an array that places some stops and not
    others is rejected. Out-of-order stops are sorted.

One palette serves the whole app. What paints out of it is chosen per consumer, by setting a colour
field to the string `"palette"`:

```json
{"palette": "Heat", "textColor": "palette", "chartColor": "palette"}
```

An effect that honours palettes takes it without being asked; it has no colour field to opt in with.

### The palette is per app

The palette and `effectSpeed` you send belong to that app alone. If app A and app B both use
`Plasma` and app B sets `palette: "Lava"`, **app A keeps its own colours**. Deleting an app that set
a palette leaves no residue either - the next app to use that effect starts from its stock colours.

---

## Weather overlays

An overlay draws **on top of** the finished page - it never clears the canvas, so text and icons
stay visible underneath. Overlay names are matched case-insensitively: `rain`, `Rain` and `RAIN`
are the same overlay.

AWTRIX draws **6 weather overlays**:

| `overlay` | What it draws | Base rate |
|---|---|---|
| `rain` | Scattered blue drops falling plumb, with short darker tails - about a third of the columns are wet at a time | ~15 steps/s |
| `snow` | Bright and dim grey flakes, swaying sideways as they fall | ~7 steps/s |
| `drizzle` | Fine mist - sparse bare flecks, lighter blue, hardly any tail | ~10 steps/s |
| `storm` | Dense wind-slanted streaks - long tails, drifting one column per three rows | ~25 steps/s |
| `thunder` | `storm`, plus a full-white flash at irregular intervals a few seconds apart, sometimes double-blinking | ~25 steps/s |
| `frost` | **Static** irregular icy crust along the top and bottom edges | none - it does not animate |

One step moves every drop down one pixel, so `rain` takes about half a second to cross the panel.
The rates differ - heavier weather falls faster - and the speed multiplier (`effectSpeed` on a
payload, `speed` in `overlaySettings`) scales whichever one applies. `frost` draws the same frame
whatever the speed is set to. Drop placement is pseudo-random, so the fall never settles into a
repeating pattern.

Overlay colours go through the [palette](#palettes), so a supplied palette recolours all six,
`frost` included. Without a palette the stock colours are unchanged.

```bash
# red rain
curl -X POST 'http://<awtrix-ip>/api/v1/apps?name=weather' \
  -H 'Content-Type: application/json' \
  -d '{"text":"7C","overlay":"rain","palette":"Lava"}'
```

### Setting an overlay per app

```bash
curl -X POST 'http://<awtrix-ip>/api/v1/apps?name=weather' \
  -H 'Content-Type: application/json' \
  -d '{"text":"7C","overlay":"snow"}'
```

An unknown per-app `overlay` name fails with `422 {"code":"validationFailed","field":"overlay"}`
and **nothing is stored**.

### Setting the global overlay

`PATCH /api/v1/display` sets a device-wide overlay. `null` clears it.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain"}'
```

```bash
# clear it
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":null}'
```

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `overlay` | string or null | one of the 6 names, or `null` | none | - | Device-wide weather overlay |
| `overlaySettings` | object | `{speed, palette, blend}` | *unset* | - | Tunes the device-wide overlay |

Both keys are validated. A non-string, non-null `overlay` gives
`422 {"overlay","must be a string or null"}`; an unknown name gives
`422 {"overlay","unknown overlay"}`; a non-object `overlaySettings` gives
`422 {"overlaySettings","must be an object"}`. The whole PATCH applies completely or not at all.

```bash
# calm the global rain down to a third of its pace
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain","overlaySettings":{"speed":0.3}}'
```

Settings belong to the overlay they were set for: `{"overlay":null}` drops them as well, so they
never resurface on whatever overlay you pick next.

Both values are readable from `GET /api/v1/display` - `overlay` (`null` when none) and
`overlaySettings`, which always reports the effective `speed`, `palette` (`null` when unset) and
`blend`.

The Web UI exposes the speed as an **Overlay speed** slider under **Display → Weather overlay**,
in percent: 100 % is the overlay's own pace.

### Which overlay wins

- **Pushed apps and notifications:** a per-app `overlay` **wins** over the global one. The global
  overlay applies only when the payload carries no `overlay`.
- **Built-in apps** (time, date, temperature, humidity, battery): **only** the global overlay
  applies. These apps have no payload, so there is no per-app overlay to consult.
- **Settings follow the overlay.** An overlay named on a payload is driven by that payload's
  own `effectSpeed` and `palette`; the device-wide overlay is driven by `overlaySettings`,
  whichever app is on screen. An app's settings never reach an overlay the app did not name.

---

## Palettes

A palette is a **16-entry colour table**. Every effect, overlay and text colour that draws from a
palette picks its colours out of those 16 entries.

**8 built-in names** are accepted by `palette` (case-insensitive):

| Palette | Character |
|---|---|
| `Cloud` | Blues and dark blues, rising to sky blue and one white entry |
| `Lava` | Black → maroon → dark red → red → orange, with one white peak |
| `Ocean` | Midnight blue, navy, teal, sea green, aqua, light sky blue |
| `Forest` | Dark green → forest/olive → lime and lawn green |
| `Stripe` | Eight saturated hues alternating with black - hard stripes |
| `Party` | Purples, magentas, reds, oranges and yellows - no green band |
| `Heat` | Black → red → yellow → white ramp |
| `Rainbow` | Full hue wheel in 16 steps |

### What `blend` changes

Every palette holds 16 colours. As an effect animates, it sweeps through them in order and wraps
from the last back to the first.

- **`paletteBlend: true`** (the default) - the colour slides smoothly from one entry to the next,
  so the sweep looks like a continuous gradient.
- **`paletteBlend: false`** - only the 16 colours themselves are used, giving **16 hard colour
  bands**.

### Custom palettes

Upload `/PALETTES/<name>.txt` to the AWTRIX filesystem - one stop per line, **one to sixteen** lines.
A leading `#` is tolerated and blank lines are skipped. Then reference it by file name (without
`.txt`) in `palette`. Names containing `/` or `..` are rejected, and the file name is matched
case-insensitively like every other name in the API.

Two line forms:

| Line | Means |
|---|---|
| `RRGGBB` | a stop with no position - the stops are spread evenly |
| `RRGGBB@<0-100>` | a stop at that percentage of the ramp |

```
FF0000@0
FFFF00@70
FFFFFF@100
```

The lines are colour **stops**, interpolated across all 16 entries: a three-line file describes a
three-colour ramp, not three colours followed by thirteen leftovers. With positions, each stop
decides how much of the ramp it gets - the example above spends 70% of its length going red to
yellow. Before the first stop and after the last the colour is flat, the way a gradient's ends
extend, and two stops at the same position are a hard edge.

Positions are **all or nothing**: a file that gives some lines a position and not others is rejected.
Stops written out of order are sorted. Anything else malformed - a line that is not six hex digits, a
position above 100 - rejects the file, which comes back as `422` on the `palette` field of whatever
names it.

A name is looked up as a **file first** and only then as a built-in, so a file may carry a built-in's
name and replaces that palette while it exists - deleting the file restores the original. That is how
a built-in is edited: the [palette editor](../guides/palette-editor.md#change-a-built-in) writes such
a file when you change one. A name that matches neither is rejected with `422 validationFailed` on
the `palette` field.

---

## Transitions

A transition animates the change from one app's page to the next. It is a **device-wide setting**,
not a per-app key: there is no way to give one app its own transition.

On the wire `transitionEffect` is a **name** (a string), sent to `PATCH /api/v1/settings`:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"transitionEffect":"Pixelate","transitionDurationMs":600}'
```

| Setting | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `transitionEffect` | string | one of the 22 names | `"Rain"` | - | How pages change |
| `transitionDurationMs` | int | 0 … `INT_MAX` | `1000` | ms | How long one transition takes |
| `autoTransition` | bool | - | `true` | - | Whether AWTRIX rotates through apps at all |

**22 transition names:**

| `transitionEffect` | What happens | Honours navigation direction? |
|---|---|---|
| `Random` | Picks one of the other twenty-one, re-picked for every transition - never itself | - |
| `Slide` | Horizontal slide - both pages move together | **yes** |
| `Dim` | Old page fades to black, then the new page fades up | no |
| `Zoom` | New page grows from the centre over the old one | no |
| `Rotate` | Vertical roll - the old page rolls out, the new one rolls in | **yes** |
| `Pixelate` | Per-pixel dissolve - the same pixel pattern every time, not random | no |
| `Curtain` | New page reveals from **both** edges toward the centre | no |
| `Ripple` | Circular reveal growing from the centre | no |
| `Blink` | Old page blinks out in hard steps, new page blinks in the same way | no |
| `Reload` | Column sweep, always left → right | no |
| `Fade` | Straight per-channel crossfade | no |
| `Cover` | New page slides **over** a stationary old page | **yes** |
| `Uncover` | Old page slides away and leaves the new page standing | **yes** |
| `Split` | New page opens from the centre outward - `Curtain` in reverse | no |
| `Blinds` | Vertical bars that widen until they meet. Bar width scales with the panel | no |
| `Blocks` | Dissolve in 4×2 tiles instead of single pixels. Same pattern every time, like `Pixelate` | no |
| `Flash` | Old page ramps into solid **white** and the new page ramps out of it | no |
| `Diamond` | Diamond-shaped reveal from the centre - `Ripple` in Manhattan distance | no |
| `Wave` | Column sweep with a sine bend, so the edge crosses as a wave | **yes** |
| `Rain` | Every column rolls vertically on its own staggered clock | **yes** |
| `Melt` | Old page drips off column by column and leaves the new page behind | **yes** |
| `Interlace` | Alternating rows slide in from opposite sides | **yes** |

An unknown name is rejected with `422 validationFailed` whose message begins `must be one of:` and
lists the names above - the name table is matched case-insensitively.

Every transition is derived from the configured matrix geometry, so all of them work on any panel
width from 32 to 128. `Flash` drives the whole panel to near-white around the midpoint, which at
high `brightness` on a wide panel is briefly the largest current draw AWTRIX produces.

### Pacing

Every transition takes exactly `transitionDurationMs`, but they do not all map that time onto
motion the same way. They are calibrated so that they *feel* like the same length:

- **Wipes and slides** ease in and out.
- **Dissolves and cross-fades** (`Dim`, `Zoom`, `Pixelate`, `Blink`, `Fade`, `Blocks`, `Flash`)
  run linearly.
- **`Rain` and `Melt` stagger each column's start, not its speed.** Every column falls at the same
  rate and the last one still lands exactly at the end.

---

## Colors

Every colour key in the whole API - payload keys, settings keys, palette entries, draw-command
colours - accepts the same forms.

### Accepted input forms

| Form | Example | Notes |
|---|---|---|
| 6-digit hex string | `"#FF00AA"` or `"FF00AA"` | Leading `#` optional; digits `0-9 a-f A-F` |
| 3-digit hex shorthand | `"#F0A"` or `"F0A"` | Each nibble expanded ×17 → `#FF00AA` |
| RGB array | `[255, 0, 170]` | Each channel **clamped** to 0…255; extra elements ignored |
| HSV array | `["HSV", 320, 100, 100]` | The `"HSV"` tag is **case-sensitive** |
| Packed integer | `16711850` | A bare JSON number, masked to `0xRRGGBB` |

An 8-digit `#RRGGBBAA` value is **rejected** - there is no alpha channel.

**HSV ranges:** `h` is wrapped into `[0, 360)`, and negatives are handled correctly (`-30` becomes
`330`). `s` and `v` are **percentages clamped to 0…100**, *not* 0…255 - `["HSV",0,100,100]` is pure
red, and `["HSV",0,100,255]` is the same pure red, because `v` clamps.

### Output form

Colours always come back as **`"#RRGGBB"` with uppercase hex digits**, regardless of which input
form you used. Nullable colours come back as JSON `null`.

### Nullable colours

| Key group | `null` means |
|---|---|
| `timeColor`, `dateColor`, `humidityColor`, `temperatureColor`, `batteryColor` | **inherit** - fall back to `textColor` |
| `colorCorrection`, `colorTint` | **off** - no correction applied |

Send `null` to restore either behaviour. Only an explicit `null` carries that meaning - no colour
value is reserved, so `#000000` and `#FFFFFF` are settable and read back as themselves.

### A malformed colour is rejected everywhere

Settings, indicators, the moodlight and app/notification payloads all run a colour through the same
parser. Every one of them rejects a value it cannot read with `422 validationFailed`, the offending
key in `field`, and stores nothing. The message differs by route: settings, indicators and the
moodlight give
`must be a color ("#RGB", "#RRGGBB", [r,g,b], ["HSV",h,s,v] or a packed integer)`, while a colour
key inside an app or notification payload gives the shorter `"<key>" is not a valid color`. Match
on the code and `field`, never on the wording.

---

## Display color pipeline

Four settings change how the panel looks without changing what the apps draw:
`saturation` drains or keeps the colour, `gamma` shapes the brightness curve of each
channel, and `colorCorrection` and `colorTint` scale red, green and blue separately -
the first to correct the panel's own cast, the second to warm or cool the whole picture.
Types, ranges and defaults: [Settings - Panel](settings.md#panel).

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"saturation":40,"gamma":2.2,"colorCorrection":"#FFB0F0","colorTint":null}'
```

They are applied in that order, to everything on the panel - apps, icons, notifications,
the moodlight and Art-Net frames alike. None of them change the framebuffer
`GET /api/v1/display/screen` returns, which is why a screenshot can look different from
the panel in front of you. All four apply live.

`settings.gamma` is the display output curve. The ambient-light → brightness response has its own
separate curve, set by `ldrGamma` - see [Brightness & sensors](../guides/brightness.md).

The device-wide text defaults - `textColor`, `uppercase` and `scroll` - are in
[Settings - Global text](settings.md#global-text). A payload overrides each of them.

---

## Text

### Encoding

Send **UTF-8**. The font is addressed by Unicode code point, so nothing is converted on the way in.
This applies to plain `text`, to each `{"text":…}` fragment, to `dt` draw-op text and to script
output alike.

A code point the font does not cover produces a single `?` placeholder - exactly one per code point,
not one per byte. `"Hi 🎉"` renders as `"Hi ?"`.

### What is mapped

| Group | Behaviour |
|---|---|
| ASCII (`U+0020`–`U+007E`) | Its own glyph |
| Latin-1 supplement (`ä ö ü ß é à` …) | Its own glyph |
| Latin Extended-A (`ą ć ę ł ń ś ż ź č ď ě ř š ž ő ű` and their capitals) | Its own glyph |
| Cyrillic (`U+0401`–`U+0491`) | Its own glyph, upper and lower case distinct |
| Punctuation (`– — ‘ ’ “ ”`) and `€` | Its own glyph |
| Everything else, including Greek | Replaced by a single `?` |

An accented letter sits on the same baseline as its bare form, with the mark above it. In `small`
the Latin-1 accents keep to the five rows a bare letter uses, the letter body giving up a row to
make room; Latin Extended-A and `Ё ё` take a row of their own and reach panel row 0. In `large` the
mark fits into the same seven rows either way.

### Font

**Two fonts**, selected per app with the [`font`](payload.md#text) key.

`small` draws ASCII with the AWTRIX panel font and everything beyond it - accents, Cyrillic,
punctuation - from [Matrix-Fonts](https://github.com/trip5/Matrix-Fonts). `large` is Matrix-Fonts
throughout. Matrix-Fonts is copyright © 2026 Trip5, MIT licensed; the full notice ships with the
firmware source in `assets/fonts/MatrixFonts.LICENSE`.

| | `small` (default) | `large` |
|---|---|---|
| Capital height | 5 px | 7 px |
| Rows used | 1–5, or 0–5 for `č ő ż Ё` | 0–6, plus row 7 for descenders |
| Character width | 4 px | 4 px |
| Space width | 2 px | 2 px |

Coverage is identical in both - the [mapped groups](#what-is-mapped) above - so the choice never
changes which characters render, only how tall they are and, because scrolling is decided by
rendered width, whether a given string scrolls.

`large` leaves only the bottom row free. An app drawing its own graphics along the top wants
`small`.

### Palette text

Set `textColor` to the string `"palette"` and the text is painted from the app's
[palette](#palettes) instead of a flat colour. The ramp is sampled **per pixel column**, so it stays
even whatever the glyph widths are.

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"PARTY TIME","palette":"Rainbow","textColor":"palette","paletteSpan":24,"paletteSpeed":1}'
```

`paletteSpan` is the pixels per full pass - `0`, the default, stretches one pass across the whole
string. `paletteSpeed` moves it at that many passes per second, independently of `effectSpeed`.

Every key a payload can carry - text, colours, charts, effects, palettes and draw
commands - is specified in [App & notification payload](payload.md). This page covers
what each one looks like on the panel.

### Scrolling

One `scroll` object describes text motion, and it means the same thing in a payload and in the
device settings. The fields, their ranges and their defaults are in
[Payload - Scrolling](payload.md#scrolling); what the four modes look like is this:

| `mode` | Motion | Cycle counted | Hold |
|---|---|---|---|
| `static` | None; the text is drawn at its aligned position and overflow is clipped | never | - |
| `wrap` | Off the far edge, then back to the start anchor | per exit | at the start anchor, every cycle |
| `loop` | Continuous; every repetition that touches the panel is drawn, so there is no empty seam | per fold | initial only |
| `bounce` | Sweeps between the icon column and the far edge | per round trip | at both turning points |

In a payload each field is optional and inherits the device-wide default on its own, so
`{"scroll":{"mode":"bounce"}}` bounces at the configured speed. A bare string is shorthand for the
mode: `"scroll": "bounce"` ≡ `"scroll": {"mode": "bounce"}`. An unknown field, an unknown value or a
negative number is `422 validationFailed` with the offending key in `field` - on
`PATCH /api/v1/settings` and inside a payload alike, and in a payload the whole app or notification
is rejected with it.

Every hold lasts `holdMs`, and `entry: offscreen` skips the initial one. Full anchor geometry: [Payload → Scrolling](payload.md#scrolling).

### Draw commands

The `draw` array carries **9 commands**. Each element is an array with the command name first:

| Command | Draws |
|---|---|
| `["pixel", x, y, color]` | Pixel |
| `["pixels", color, x1, y1, …]` | Many pixels in one colour |
| `["line", x1, y1, x2, y2, color]` | Line |
| `["rect", x, y, w, h, color]` | Rect (outline) |
| `["rectFill", x, y, w, h, color]` | Filled rect |
| `["circle", cx, cy, r, color]` | Circle (outline) |
| `["circleFill", cx, cy, r, color]` | Filled circle |
| `["text", x, y, "string", color]` | Text |
| `["bitmap", x, y, w, h, data]` | Bitmap - row-major, base64 RGB888 or an array of colours |

```bash
curl -X POST 'http://<awtrix-ip>/api/v1/apps?name=box' \
  -H 'Content-Type: application/json' \
  -d '{"draw":[["rect",0,0,32,8,"#00FF00"],["pixel",16,4,[255,0,0]]]}'
```

Every draw colour accepts every [colour form](#colors). Leave it off and the command uses the
app's resolved text colour; a colour the parser cannot read, or a command name it does not know, is
rejected with `422 validationFailed` and `"field":"draw[<index>]"`.

---

## Panel wiring

How the LED strip runs through your panel is system configuration, not a display setting:
`panelWidth`, `panels`, `panelStart`, `panelWiring` and `panelSerpentine`, documented under
[Panel and orientation](system.md#panel-and-orientation). Get it wrong and the image comes out
mirrored, scrambled or split into blocks; the web UI's **Panel** section is where you fix it.

---

## Discovering the names

`GET /api/v1/capabilities` returns the live name lists, so a client never has to hard-code them:

```bash
curl http://<awtrix-ip>/api/v1/capabilities
```

```json
{
  "effects": ["BrickBreaker", "Checkerboard", "..."],
  "paletteEffects": ["Checkerboard", "ColorWaves", "..."],
  "transitions": ["Random", "Slide", "Dim", "..."],
  "overlays": ["drizzle", "frost", "rain", "..."],
  "palettes": ["Cloud", "Lava", "Ocean", "Forest", "Stripe", "Party", "Heat", "Rainbow"]
}
```

| Array | Contents | Ordering |
|---|---|---|
| `effects` | Every background effect AWTRIX can draw | **Alphabetical** |
| `paletteEffects` | The effects from `effects` that use the app's `palette` | **Alphabetical** |
| `overlays` | Every weather overlay | **Alphabetical** |
| `transitions` | Every transition | `Random` first, then as listed |
| `palettes` | The eight built-in palettes | As listed |

`paletteEffects` is what to offer a palette picker for - the fixed-colour effects appear in
`effects` but not here. The names in every array are spelled the way the API returns them; what you
send is matched case-insensitively.

`palettes` lists the built-ins only. Palettes you upload as `/PALETTES/*.txt` work everywhere a
built-in name does, but they do not appear in this array - list them with
`GET /api/v1/files?dir=/PALETTES`.

The identical JSON is published retained to MQTT `<prefix>/state/capabilities` on every connect,
and backs the Home Assistant select options.
