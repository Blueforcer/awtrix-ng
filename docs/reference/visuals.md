# Visual reference

This page lists every visual name AWTRIX NG understands – effects, overlays, palettes and
transitions – and shows what each one looks like. It also covers colors, text and fonts.

There are **four separate lists of names**. They do not mix:

| Name list | Where it goes | Case |
|---|---|---|
| [Background effects](#background-effects) | `effect` on a pushed-app / notification payload | insensitive |
| [Weather overlays](#weather-overlays) | `overlay` on a payload, or `overlay` on `PATCH /api/v1/display` | insensitive |
| [Palettes](#palettes) | `palette` on a payload | insensitive |
| [Transitions](#transitions) | `transitionEffect` in `PATCH /api/v1/settings` (whole device) | insensitive |

Upper and lower case never matter – `"matrix"`, `"Matrix"` and `"MATRIX"` are the same effect. This
page uses the spelling that [`GET /api/v1/capabilities`](#discovering-the-names) lists and the API
returns.

`Ripple` and `Fade` are in two lists – as a background effect and as a transition – and the two
look different. `Random` is only a transition. What a name means depends on the key you use it
with.

---

## Background effects

A background effect fills the display **behind** text, icons and charts. Set it per app with the
`effect` key.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
  -H 'Content-Type: application/json' \
  -d '{"text":"HELLO","effect":"Pacifica"}'
```

AWTRIX has **19 background effects**:

| `effect` | What it shows | Uses `palette`? |
|---|---|---|
| `Plasma` | Flowing plasma over the whole display, through all colors | yes |
| `TheaterChase` | Every third column lit, the lights marching sideways over a rainbow | yes |
| `Fade` | Horizontal color bands from top to bottom, all slowly cycling through the colors | yes |
| `MovingLine` | A full-width line bouncing up and down, with a short glow behind it | yes |
| `BrickBreaker` | Breakout that plays itself: the ball bounces off the paddle and clears the rows of bricks, then the wall is rebuilt. The palette colors the brick rows | yes |
| `PingPong` | Pong that plays itself: two paddles return a red ball, and now and then one misses | **no** |
| `Radar` | A beam sweeping clockwise round the center with a fading afterglow; green by default | yes |
| `Checkerboard` | A checkerboard in two colors that drift through the palette | yes |
| `Fireworks` | Rockets that rise and burst into sparks, several at a time | yes |
| `PlasmaCloud` | Soft drifting clouds in a few colors at a time, slowly shifting | yes |
| `Ripple` | Rings spreading from random points and fading out | yes |
| `Snake` | Snake that plays itself: it hunts the red apple and grows, and starts again when it gets stuck | yes |
| `Pacifica` | Ocean waves; blue-teal by default | yes |
| `Matrix` | Green trails falling down each column, with bright heads, at random times and lengths | **no** |
| `SwirlIn` | A rainbow spiral turning inwards over the whole display | yes |
| `SwirlOut` | A rainbow spiral turning outwards over the whole display | yes |
| `LookingEyes` | Two eyes that look around and blink every few seconds | **no** |
| `TwinklingStars` | Stars that flare up, fade and come back somewhere else | yes |
| `ColorWaves` | The colors spread once across the display, sliding sideways | yes |

<!-- only esp32 esp32-s3 -->
Every effect fills any display size.
<!-- /only -->
<!-- only tc002 -->
Every effect fills the whole display. In a pushed app or a notification it is drawn at double
size on the 26×8 grid while [`enlargeApps`](settings.md#global-text) is on, the default. In a
layout, in a script and in an app that keeps its full size, it uses all 52×16 pixels:
`LookingEyes` and the squares of `Checkerboard` are drawn twice as large, `BrickBreaker`,
`PingPong` and `Snake` play on the bigger field, and the other effects use the extra pixels for
more detail. Weather overlays always use single pixels.
<!-- /only -->

An empty `effect`, or no `effect` key, means no background.

The effects marked **no** always use their own colors. If you send a `palette` with them, the
request succeeds but nothing changes on the display.

An unknown name (`"Plasm"`) is rejected with `422 {"code":"validationFailed","field":"effect"}`
and **nothing is stored** – with a list of apps, none of them is stored. Upper and lower case are
not a typo: `"pacifica"` works. Check the spelling with
[`GET /api/v1/capabilities`](#discovering-the-names).

---

## Effect settings

An effect takes two things from its payload: its own speed, and the app's [palette](#palettes),
which it shares with the text and the charts.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `effectSpeed` | float | `0.1 … 10` | `1.0` | multiplier | Speed factor for the effect (values outside the range are set to the nearest limit) |
| `palette` | string or array | built-in name, file name, or ≤ 16 stops | *unset* – the effect keeps its own colors | - | Colors for palette effects and weather overlays |
| `paletteBlend` | bool | - | `true` | - | Blend smoothly between palette colors instead of showing 16 hard bands |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
  -H 'Content-Type: application/json' \
  -d '{"text":"HI","effect":"Plasma","effectSpeed":0.4,"palette":"Lava"}'
```

### How `effectSpeed` works

Each effect and overlay has its own normal speed. `effectSpeed` multiplies it: `2.0` runs twice as
fast, `0.5` half as fast. At `1` all effects feel about equally fast, but they still differ in
character – `storm` falls faster than `drizzle`.

A value outside `0.1 … 10` is not rejected; AWTRIX uses the nearest limit.

`effectSpeed` also sets the speed of an overlay named in the same payload, so `storm` at `2` falls
twice as fast. The overlay for the whole device has its own speed, set with `overlaySettings` on
`PATCH /api/v1/display`.

### How `palette` works

You can give a palette in three ways:

=== "A name"

    ```json
    {"palette": "Heat"}
    ```

    AWTRIX first looks for a file `/PALETTES/<name>.txt`, then for a
    [built-in palette](#palettes) with that name. Upper and lower case do not matter. A file with
    that name wins. If neither exists, the payload is **rejected** with
    `422 {"code":"validationFailed","field":"palette"}`.

=== "Color stops"

    ```json
    {"palette": ["#FF0000", "#00FF00", [0,0,255], ["HSV",60,100,100]]}
    ```

    1 to **16** colors ("stops"); anything after the 16th is ignored. Each can use any
    [color form](#colors). The stops are spread evenly and the colors in between are blended, so
    two stops already give a full gradient. An **empty list** is rejected.

=== "Color stops with positions"

    ```json
    {"palette": [{"color": "#FF0000", "pos": 0},
                 {"color": "#FFFF00", "pos": 70},
                 {"color": "#FFFFFF", "pos": 100}]}
    ```

    `pos` is `0-100`: where the stop sits along the gradient – the same as `RRGGBB@70` in a
    [palette file](#custom-palettes).

    Both keys are required. Either every stop has a position or none has; mixing is rejected.
    Stops out of order are sorted.

One palette serves the whole app. To paint text or a chart from it, set that color key to the
string `"palette"`:

```json
{"palette": "Heat", "textColor": "palette", "chartColor": "palette"}
```

Effects that use palettes take it automatically.

### The palette is per app

The palette and `effectSpeed` you send belong only to that app. If app A and app B both use
`Plasma` and app B sets `palette: "Lava"`, **app A keeps its own colors**. When you delete an app,
its palette goes with it.

---

## Weather overlays

An overlay is drawn **on top of** the app. Text and icons stay visible underneath.

AWTRIX has **6 weather overlays**:

| `overlay` | What it shows | Normal speed |
|---|---|---|
| `rain` | Blue drops falling straight down with short darker tails – about a third of the columns at a time | ~15 steps/s |
| `snow` | Bright and dim gray flakes swaying as they fall | ~7 steps/s |
| `drizzle` | Fine light-blue drops, few and hardly any tail | ~10 steps/s |
| `storm` | Dense slanted streaks with long tails, blown sideways | ~25 steps/s |
| `thunder` | `storm` plus lightning every few seconds: a jagged bolt with a branch while the sky lights up and dies away, sometimes with a second flare. The app stays visible through the flash | ~25 steps/s |
| `frost` | An icy border along the top and bottom edge that shimmers softly and glints now and then; the middle stays clear. The border is thicker on taller displays | ~10 steps/s |

At each step every drop moves down one pixel, so `rain` crosses the
<!-- only esp32 esp32-s3 -->8-pixel display in about half a second.<!-- /only --><!-- only tc002 -->16-pixel display in about one second.<!-- /only --> Heavier weather falls faster. The speed factor (`effectSpeed` on a payload, `speed` in
`overlaySettings`) changes the speed; for `frost` it is the speed of the shimmer. The drops fall at
random places, so the pattern never repeats.

A [palette](#palettes) changes the colors of all six overlays, `frost` too. Without a palette they
keep their normal colors.

```bash
# red rain
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"text":"7C","overlay":"rain","palette":"Lava"}'
```

### Setting an overlay per app

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"text":"7C","overlay":"snow"}'
```

An unknown `overlay` name is rejected with `422 {"code":"validationFailed","field":"overlay"}`
and **nothing is stored**.

### Setting the global overlay

`PATCH /api/v1/display` sets an overlay for the whole device. `null` removes it.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain"}'
```

```bash
# remove it
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":null}'
```

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `overlay` | string or null | one of the 6 names, or `null` | none | - | Weather overlay for the whole device |
| `overlaySettings` | object | `{speed, palette, blend}` | *unset* | - | Speed and colors of that overlay |

Both keys are checked. An `overlay` that is neither a string nor `null` gives
`422 {"overlay","must be a string or null"}`; an unknown name gives
`422 {"overlay","unknown overlay"}`; an `overlaySettings` that is not an object gives
`422 {"overlaySettings","must be an object"}`. The PATCH is applied completely or not at all.

```bash
# slow the global rain down to a third of its speed
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain","overlaySettings":{"speed":0.3}}'
```

`overlaySettings` belong to the overlay you set them for. `{"overlay":null}` removes them too, so
the next overlay you pick starts fresh.

`GET /api/v1/display` returns both: `overlay` (`null` when none) and `overlaySettings` with the
current `speed`, `palette` (`null` when not set) and `blend`.

In the web UI, the speed is the **Overlay speed** slider under **Display → Weather overlay**, in
percent: 100 % is the overlay's normal speed.

### Which overlay wins

- **Pushed apps and notifications:** an `overlay` in the app's payload<!-- only tc002 --> or in its `layout`<!-- /only --> **wins**
  over the global one. The global overlay shows only when neither has an `overlay`.
- **Built-in apps** (<!-- only esp32 esp32-s3 -->time, date, temperature, humidity, battery<!-- /only --><!-- only tc002 -->time, status<!-- /only -->): **only** the global overlay
  shows, because these apps have no payload.
- **Speed and colors come with the overlay.** An overlay from a payload uses that payload's
  `effectSpeed` and `palette`. The global overlay uses `overlaySettings`, whatever app is
  shown.

---

## Palettes

A palette is a set of **16 colors**. Effects, overlays and text that use a palette take their
colors from it.

**8 built-in palettes** can be used by name in `palette`:

| Palette | Colors |
|---|---|
| `Cloud` | Blues and dark blues, up to sky blue and one white |
| `Lava` | Black → maroon → dark red → red → orange, with one white peak |
| `Ocean` | Midnight blue, navy, teal, sea green, aqua, light sky blue |
| `Forest` | Dark green → forest/olive → lime and lawn green |
| `Stripe` | Eight strong colors alternating with black – hard stripes |
| `Party` | Purples, magentas, reds, oranges and yellows – no green |
| `Heat` | Black → red → yellow → white |
| `Rainbow` | All colors of the rainbow in 16 steps |

### What `blend` changes

While an effect runs, it moves through the 16 colors in order and starts over after the last one.

- **`paletteBlend: true`** (default) – the color changes smoothly from one to the next, like a
  gradient.
- **`paletteBlend: false`** – only the 16 colors themselves are used, so you see **16 hard
  bands**.

### Custom palettes

1. Create a text file with one color per line, **one to sixteen** lines. Lines after the
   sixteenth color are ignored.
2. Upload it to AWTRIX as `/PALETTES/<name>.txt`.
3. Use `<name>` (without `.txt`) as `palette`.

A leading `#` on a line is allowed and empty lines are skipped. Names with `/` or `..` are
rejected. Upper and lower case in the name do not matter.

Each line has one of two forms:

| Line | Means |
|---|---|
| `RRGGBB` | a color stop without a position – the stops are spread evenly |
| `RRGGBB@<0-100>` | a color stop at that percentage of the gradient |

```
FF0000@0
FFFF00@70
FFFFFF@100
```

The lines are color **stops** of a gradient across all 16 colors: a three-line file gives a
three-color gradient. With positions, you decide how much room each part gets – the example above
uses 70 % of the gradient to go from red to yellow. Before the first and after the last stop the
color stays the same. Two stops at the same position make a hard edge.

Either every line has a position or none has; mixing is rejected. Stops out of order are sorted.
A file with any other error – a line that is not six hex digits, a position above 100 – cannot be
used. Any payload that names it is rejected with `422` on the `palette` field.

A file may have the same name as a built-in palette. It then replaces the built-in until you
delete the file. This is how you change a built-in palette: the
[palette editor](../guides/palette-editor.md#change-a-built-in) saves such a file for you. A name
that matches neither a file nor a built-in is rejected with `422 validationFailed` on the `palette`
field.

---

## Transitions

A transition is the animation between one app and the next. It is **one setting for the whole
device**; you cannot give one app its own transition.

`transitionEffect` and `transitionDirection` are **names** (strings), sent to
`PATCH /api/v1/settings`:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"transitionEffect":"Slide","transitionDirection":"reverse","transitionDurationMs":600}'
```

| Setting | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `transitionEffect` | string | one of the 22 names | `"Rain"` | - | The animation between apps |
| `transitionDirection` | string | `normal` · `reverse` | `"normal"` | - | Normal or mirrored movement |
| `transitionDurationMs` | int | 0 … 2147483647 | `1000` | ms | How long one transition takes |
| `autoTransition` | bool | - | `true` | - | Whether AWTRIX switches apps automatically |

`reverse` flips sideways movement left/right and vertical movement up/down. It does not change the
app order: **Next** and automatic switching still go to the same app, and **Previous** still moves
the other way. Transitions marked `no` below look the same either way.

**22 transitions:**

| `transitionEffect` | What happens | Follows the direction? |
|---|---|---|
| `Random` | A different one of the other 21 each time – never itself | - |
| `Slide` | Both apps slide sideways together | **yes** |
| `Dim` | The old app fades to black, then the new app fades in | no |
| `Zoom` | The new app grows from the center over the old one | no |
| `Rotate` | The old app rolls out upwards or downwards, the new one rolls in | **yes** |
| `Pixelate` | The new app appears pixel by pixel – in the same pattern every time | no |
| `Curtain` | The new app appears from **both** edges towards the center | no |
| `Ripple` | A growing circle from the center shows the new app | no |
| `Blink` | The old app blinks out in hard steps, the new app blinks in | no |
| `Reload` | A column sweep, always left → right | no |
| `Fade` | A smooth crossfade | no |
| `Cover` | The new app slides **over** the old one, which stays in place | **yes** |
| `Uncover` | The old app slides away and shows the new one underneath | **yes** |
| `Split` | The new app opens from the center outwards – `Curtain` the other way round | no |
| `Blinds` | Vertical bars that widen until they meet. The bars get wider on wider displays | no |
| `Blocks` | Like `Pixelate`, but in 4×2 blocks. Same pattern every time | no |
| `Flash` | The old app turns **white**, then the new app appears out of the white | no |
| `Diamond` | A growing diamond from the center shows the new app | no |
| `Wave` | A column sweep with a curved, wave-like edge | **yes** |
| `Rain` | Each column rolls down on its own, one after the other | **yes** |
| `Melt` | The old app drips away column by column and leaves the new app | **yes** |
| `Interlace` | Every second row slides in from the other side | **yes** |

An unknown name is rejected with `422 validationFailed`. The message starts with `must be one of:`
and lists the names above.

<!-- only esp32 esp32-s3 -->
All transitions work on any display width from 32 to 128. `Flash` makes the whole display nearly
white for a moment. On a wide display at high `brightness`, that is the moment AWTRIX draws the most
current.
<!-- /only -->
<!-- only tc002 -->
`Flash` makes the whole display nearly white for a moment.
<!-- /only -->

### Pacing

Every transition takes exactly `transitionDurationMs`, and they are tuned to *feel* equally long:

- **Slides and wipes** start slowly, speed up and slow down at the end.
- **Fades and dissolves** (`Dim`, `Zoom`, `Pixelate`, `Blink`, `Fade`, `Blocks`, `Flash`) run at
  an even speed.
- **`Rain` and `Melt`** start each column a little later, but all columns fall at the same speed
  and the last one lands exactly at the end.

---

## Colors

Every color key in the whole API – payload keys, settings, palette colors, draw commands –
accepts the same forms.

### Accepted input forms

| Form | Example | Notes |
|---|---|---|
| 6-digit hex string | `"#FF00AA"` or `"FF00AA"` | `#` optional; digits `0-9 a-f A-F` |
| 3-digit hex short form | `"#F0A"` or `"F0A"` | Each digit is doubled → `#FF00AA` |
| RGB array | `[255, 0, 170]` | Each value is limited to 0…255; extra values are ignored |
| HSV array | `["HSV", 320, 100, 100]` | `"HSV"` must be in capitals |
| Number | `16711850` | The color as one number (`0xRRGGBB`) |

An 8-digit `#RRGGBBAA` value is **rejected** – there is no transparency.

**HSV values:** `h` is the hue in degrees; values outside 0…359 wrap around (`-30` becomes `330`).
`s` and `v` are **percentages from 0 to 100**, *not* 0…255. Larger values count as 100, so
`["HSV",0,100,100]` and `["HSV",0,100,255]` are both pure red.

### Output form

Colors always come back as **`"#RRGGBB"` in capitals**, whatever form you sent. Optional colors
that are not set come back as JSON `null`.

### Nullable colors

| Keys | `null` means |
|---|---|
| `timeColor`, `dateColor`, `humidityColor`, `temperatureColor`, `batteryColor` | **use `textColor`** |
| `colorCorrection`, `colorTint` | **off** – no correction |

Send `null` to get this behavior back. Only `null` means this – `#000000` and `#FFFFFF` are normal
colors and come back as themselves.

### A malformed color is rejected everywhere

Settings, indicators, the mood light and app/notification payloads all reject a color they cannot
read. The answer is `422 validationFailed` with the key in `field`, and nothing is stored. The
message differs: settings, indicators and the mood light say `must be a color`; a color in an app
or notification payload says `invalid color`. In your own code, check the code and `field`, not
the message text.

---

## Display colors {#display-color-pipeline}

Four settings change how the whole display looks. They do not change what the apps show – only how
the colors come out of the LEDs. Types, ranges and defaults are in
[Settings – Color](settings.md#panel).

| Setting | What you see | When to change it |
|---|---|---|
| `saturation` | `100` shows colors as they are. Lower values make everything paler, `0` shows only grays. | The display looks too colourful, or you want a calm, gray look. |
| `gamma` | Higher values make dark and medium colors darker, so dim shades look more natural. Lower values make them brighter. Bright colors stay about the same. | Dark colors look washed out (raise it) or disappear (lower it). |
| `colorCorrection` | Every color is multiplied by this color. `#FFFFFF` or `null` changes nothing; `#FFB0F0` weakens green. | Your LEDs have a color cast – for example white looks greenish. |
| `colorTint` | Works like `colorCorrection` and adds on top of it. | You want the whole picture warmer (`#FFD6AA`) or cooler. |

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"saturation":40,"gamma":2.2,"colorCorrection":"#FFB0F0","colorTint":null}'
```

Good to know:

- The settings affect everything on the display: apps, icons, notifications<!-- only esp32 esp32-s3 -->, the mood light and
  Art-Net<!-- /only --><!-- only tc002 --> and the mood light<!-- /only -->.
- Changes show at once.
- A screenshot from `GET /api/v1/display/screen` or the web UI preview does **not** include
  these settings. That is why a screenshot can look different from the real display.
<!-- only esp32 esp32-s3 -->
- `gamma` here is for the display colors. How the light sensor changes the brightness has its own
  setting, `ldrGamma` – see [Brightness & sensors](../guides/brightness.md).
<!-- /only -->

The default text settings – `textColor`, `uppercase` and `scroll` – are in
[Settings – Global text](settings.md#global-text). A payload can override each of them.

---

## Text

### Encoding

Send text as **UTF-8**. This applies to plain `text`, to each `{"text":…}` part, to text in draw
commands and to text from scripts.

A character the font does not have is shown as one `?` – one per character. `"Hi 🎉"` shows as
`"Hi ?"`.

### What is mapped

| Group | Result |
|---|---|
| ASCII (`U+0020`–`U+007E`) | Shown |
| Latin-1 supplement (`ä ö ü ß é à` …) | Shown |
| Latin Extended-A (`ą ć ę ł ń ś ż ź č ď ě ř š ž ő ű` and their capitals) | Shown |
| Cyrillic (`U+0401`–`U+0491`) | Shown, upper and lower case different |
| Greek (`U+0386`–`U+03CE`) | Shown, upper and lower case different |
| Vietnamese (`Ơ ơ Ư ư`, `U+1EA0`–`U+1EF9`) and `ẞ` | Shown |
| IPA phonetic letters (`U+0250`–`U+02AF`) | Shown |
| Chinese and Korean date and weekday characters: `〇 一 二 三 四 五 六 七 八 九 十 年 月 日 星 期 天 上 下 午 零`, `년 월 일 화 수 목 금 토 오 전 후 요` | Shown |
| Punctuation (`– — ‘ ’ “ ” …`), currency (`€ ₡ ₦ ₩ ₪ ₫ ₭ ₮ ₱ ₲ ₴ ₵ ₸ ₹ ₺ ₼ ₽ ₾ ₿ ￥`), `℃ ℉ □` and `�` | Shown |
| `⓪ ①`–`⑩`, and the en, em and thin spaces (`U+2002`, `U+2003`, `U+2009`) | Empty space of a fixed width: `⓪` 0 px, `①` 1 px … `⑩` 10 px |
| Everything else, including emoji | Shown as one `?` |

Every font has the same characters – `small`, `large` and all ten Matrix-Fonts.

An accented letter sits on the same line as the letter without accent, with the accent above it.
In `small`, the Latin-1 accents fit into the five rows of a normal letter (the letter gets one row
shorter). Latin Extended-A, `Ё ё`, accented Greek capitals and Vietnamese capitals with a horn or
two accents use one extra row at the top of the display (row 0). In `large` all accents fit into the
same seven rows.

### Font

**Two standard fonts** and the Matrix-Fonts below. A pushed app or a notification picks one with
the [`font`](payload.md#text) key.

`small` uses the AWTRIX font for ASCII and [Matrix-Fonts](https://github.com/trip5/Matrix-Fonts)
for everything else – accents, Cyrillic, punctuation. `large` uses Matrix-Fonts for everything. The
fonts are licensed under the SIL Open Font License 1.1; Trip5 and the original font authors are
credited in the license notices that come with the firmware source<!-- only tc002 --> and the update package<!-- /only -->. AWTRIX NG
itself has its own license.

| | `small` (default) | `large` |
|---|---|---|
| Capital height | 5 px | 7 px |
| Rows used | 1–5, or 0–5 for `č ő ż Ё Ά Ơ` | 0–6, plus row 7 for descenders |
| Character width | 4 px | 4 px |
| Space width | 2 px | 2 px |

Both fonts have the same [characters](#what-is-mapped). The choice changes only the height – and,
because the text width changes, whether a text scrolls.

`large` leaves only the bottom row free. If your app draws its own graphics along the top, use
`small`.

Pushed apps, notifications<!-- only tc002 -->, layout text regions<!-- /only --> and the script function `font(name)` can all use Matrix-Fonts.
`GET /api/v1/capabilities` lists each available font ID with `ascent`, `descent` and `lineHeight`
in pixels. In a pushed app or a notification, ascent and descent decide where the text sits: the
font's rows are centered in the height of the display<!-- only esp32 esp32-s3 -->, which puts `small` and `large` on the same rows as always on an
8-pixel display<!-- /only -->.

| IDs | Ascent | Descent | Line height |
|---|---|---|---|
| `matrix-light6` | 6 | 0 | 6 |
| `matrix-chunky8x6` | 8 | 0 | 8 |
<!-- only tc002 -->
| `matrix-chunky6`, `matrix-chunky6x`, `matrix-light6x` | 6 | 0 | 6 |
| `matrix-chunky8`, `matrix-chunky8x`, `matrix-light8`, `matrix-light8x`, `matrix-light8x6` | 8 | 0 | 8 |
<!-- /only -->

See [the font selection](../guides/text.md#the-fonts) for what each font looks like.

The Matrix-Fonts keep all their original characters and spacing, including characters wider than
eight pixels. Missing characters were added in each font's own style, so every font has all the
[characters above](#what-is-mapped) and changing the font never turns a character into `?`.
<!-- only tc002 -->In layout text regions, vertical alignment takes accents and descenders into account.<!-- /only -->
Font sources
and credits are in the
[font catalog record](../developers/fonts.md).

### Palette text

Set `textColor` to the string `"palette"` to paint the text with the app's [palette](#palettes)
instead of one color. The colors change **column by column**, so the gradient is even whatever
the letter widths are.

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"PARTY TIME","palette":"Rainbow","textColor":"palette","paletteSpan":24,"paletteSpeed":1}'
```

`paletteSpan` is the width in pixels of one full run through the palette. `0` (the default)
stretches one run across the whole text. `paletteSpeed` moves the colors by that many runs per
second, independent of `effectSpeed`.

All keys a payload can have – text, colors, charts, effects, palettes and draw commands – are in
[App & notification payload](payload.md). This page shows what they look like on the display.

### Scrolling

One `scroll` object sets how text moves. It means the same in a payload and in the settings. The
fields, ranges and defaults are in [Payload – Scrolling](payload.md#scrolling). The four modes look
like this:

| `mode` | Movement | Counts as one cycle | Pause |
|---|---|---|---|
| `static` | None; text that does not fit is cut off | never | - |
| `wrap` | Off the far edge, then starts again | each time it leaves | at the start, every cycle |
| `loop` | Continuous, without an empty gap | each repetition | only at the very start |
| `bounce` | Back and forth between the icon and the far edge | each round trip | at both ends |

In a payload every field is optional and uses the device default when left out, so
`{"scroll":{"mode":"bounce"}}` bounces at your usual speed. A plain string sets only the mode:
`"scroll": "bounce"` means `"scroll": {"mode": "bounce"}`. An unknown field, an unknown value or a
negative number gives `422 validationFailed` with the key in `field` – in `PATCH /api/v1/settings`
and in a payload alike. In a payload, the whole app or notification is rejected.

Every pause lasts `holdMs`; `entry: offscreen` skips the first one. Exact positions:
[Payload → Scrolling](payload.md#scrolling).

### Draw commands

The `draw` list knows **9 commands**. Each command is a list with the command name first:

| Command | Draws |
|---|---|
| `["pixel", x, y, color]` | Pixel |
| `["pixels", color, x1, y1, …]` | Many pixels in one color |
| `["line", x1, y1, x2, y2, color]` | Line |
| `["rect", x, y, w, h, color]` | Rectangle (outline) |
| `["rectFill", x, y, w, h, color]` | Filled rectangle |
| `["circle", cx, cy, r, color]` | Circle (outline) |
| `["circleFill", cx, cy, r, color]` | Filled circle |
| `["text", x, y, "string", color]` | Text |
| `["bitmap", x, y, w, h, data]` | Image – row by row, as base64 RGB888 or a list of colors |

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/box \
  -H 'Content-Type: application/json' \
  -d '{"draw":[["rect",0,0,32,8,"#00FF00"],["pixel",16,4,[255,0,0]]]}'
```

Every draw color can use any [color form](#colors). Without a color, the command uses the app's
text color. An unreadable color or an unknown command name is rejected with
`422 validationFailed` and `"field":"draw[<index>]"`.

<!-- only esp32 esp32-s3 -->
---

## Panel wiring

How the LED strip runs through your display is part of the system configuration, not a display
setting: `panelWidth`, `panels`, `panelStart`, `panelWiring`, `panelColorOrder` and
`panelSerpentine`, described under [Panel and orientation](system.md#panel-and-orientation). If it
is wrong, the picture is mirrored, scrambled or split into blocks. Fix it in the web UI's **Panel**
section.
<!-- /only -->

---

## Discovering the names

`GET /api/v1/capabilities` returns the current name lists, so your tools do not need to copy them:

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

| Array | Contents | Order |
|---|---|---|
| `effects` | All background effects | **Alphabetical** |
| `paletteEffects` | The effects that use the app's `palette` | **Alphabetical** |
| `overlays` | All weather overlays | **Alphabetical** |
| `transitions` | All transitions | `Random` first, then as listed above |
| `palettes` | The eight built-in palettes | As listed above |

Use `paletteEffects` for a palette picker – the effects with fixed colors are in `effects` but
not here. The names are spelled the way the API returns them; upper and lower case do not matter
when you send them.

`palettes` lists only the built-in palettes. Palettes you upload as `/PALETTES/*.txt` work
everywhere a built-in name works, but they are not in this list – list them with
`GET /api/v1/files?dir=/PALETTES`.

The same JSON is published, retained, to MQTT `<prefix>/state/capabilities` every time AWTRIX
connects, and fills the Home Assistant select lists.

## Related

- [App & notification payload](payload.md) – every key a payload can carry
- [Settings](settings.md) – transitions, text defaults and display colors
- [Effects & overlays](../guides/effects.md)
- [Palette editor](../guides/palette-editor.md)
