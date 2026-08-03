# Text & colors

Almost every page on the panel is text. This guide covers getting a string to look the way you
want: the right case, the right color, colored in pieces, animated, scrolling or held still, and
with your language's characters intact.

## Start here

Paste this, replacing `<awtrix-ip>` with the IP address of your AWTRIX:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"HELLO","textColor":"#FF8800"}'
```

An orange `HELLO`. Everything else on this page is one more key in that same JSON object, and every
key works identically on a pushed app:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/greeting \
  -H 'Content-Type: application/json' \
  -d '{"text":"HELLO","textColor":"#FF8800"}'
```

The `Content-Type` header is
[mandatory on every write](../reference/conventions.md#content-type-is-mandatory).

---

## Case

Text comes out uppercase by default - that is the device-wide `uppercase` setting, which ships **on**.

Per page, `textCase` overrides it:

| `textCase` | Result |
|---|---|
| `"inherit"` (default) | Follow the global `uppercase` setting |
| `"upper"` | Force uppercase, regardless of the setting |
| `"asTyped"` | Leave the text exactly as sent |

```bash
# Mixed case, just this once
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Good morning","textCase":"asTyped"}'
```

Any other word is rejected with `422 validationFailed`. To change the default for every page instead, set
the global flag once:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"uppercase":false}'
```

See [Settings → Global text](../reference/settings.md#global-text).

---

## One color for the whole string

`textColor` accepts several input forms - all of these are the same orange:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ORANGE","textColor":"#FF8800"}'      # hex, # optional

curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ORANGE","textColor":[255,136,0]}'    # RGB array, clamped 0-255

curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ORANGE","textColor":["HSV",32,100,100]}'  # h 0-360, s/v 0-100
```

The short form `"F80"` (each digit expanded ×17) and a packed integer such as `16746496` work too.
AWTRIX always *answers* in `"#RRGGBB"`, whatever you sent. Exact ranges, HSV wrapping and what
`null` means: [Visual reference → Colors](../reference/visuals.md#colors).

Leave `textColor` out and the text uses the global `textColor` setting (default white). A colour
AWTRIX cannot read is rejected with `422 validationFailed` and the offending key in `field`, and
nothing is stored.

---

## Different colors in one string

Send `text` as an array of fragments instead of a string. Each fragment is `{"text": "...", "color": color}`
and they are drawn left to right:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/cpu \
  -H 'Content-Type: application/json' \
  -d '{"text":[{"text":"CPU ","color":"#888888"},{"text":"87","color":"#FF0000"},{"text":"%","color":"#888888"}]}'
```

A grey label with a red number. There is no cap on the number of fragments, and a fragment without a
`color` is white.

A fragment array ignores the top-level `textColor` - each fragment's own `color` decides. The one
exception is `textColor: "palette"`, which paints the whole run from the palette and ignores the
fragment colors. `textBlinkMs`, `textFadeMs`, `textCase` and the global `uppercase` setting apply
either way.

---

## Painting from a palette

Set `textColor` to the string `"palette"` and the text takes its colours from the app's palette
instead of one flat colour. The palette is either a name or a list of colour stops:

```bash
# A yellow-to-red gradient stretched across the string
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/hot   -H 'Content-Type: application/json'   -d '{"text":"OVERHEAT","palette":["#FFFF00","#FF0000"],"textColor":"palette"}'
```

Yellow on the left, red on the right. The ramp climbs evenly across the string, starting on the first
stop and ending on the last.

Two stops make a gradient; more make a scale. Any of the eight built-in palettes works by name, as
does any palette of your own - see [Palette editor](palette-editor.md):

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/warm   -H 'Content-Type: application/json'   -d '{"text":"23.4C","palette":"Heat","textColor":"palette"}'
```

A name that is neither a built-in nor a palette on AWTRIX fails with
`422 {"code":"validationFailed","field":"palette"}`.

### Repeating and moving it

Two knobs turn the stretched ramp into a running one:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications   -H 'Content-Type: application/json'   -d '{"text":"PARTY TIME","palette":"Rainbow","textColor":"palette","paletteSpan":24,"paletteSpeed":1}'
```

- **`paletteSpan`** is how many pixels one full pass takes. `0` - the default - stretches a single
  pass across the whole string, which is what you want for a gradient. A number repeats the palette
  every that many pixels, which is what you want for long scrolling text: a stretched ramp over
  forty characters is nearly flat, a repeating one keeps its colour.
- **`paletteSpeed`** is how many passes travel past per second. `0` holds still. Motion always
  cycles, so a speed on a stretched ramp makes it repeat over the text's own width.

Neither is affected by `effectSpeed`.

---

## Blink and fade

Two time-based animations, both in milliseconds:

```bash
# Blink: on/off, 600 ms period
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"ALERT","textColor":"#FF0000","textBlinkMs":600}'

# Fade: smooth pulse, 2 s period
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"BREATHE","textColor":"#00AAFF","textFadeMs":2000}'
```

`textBlinkMs` is the full on/off period. `textFadeMs` swells and dims the colour smoothly over the
period rather than snapping. `0` (the default) means off for both, and when both are set, **fade wins**.

During the off half of a blink the letters are drawn in black, not left transparent. Over a
`backgroundColor` or an `effect` the blink reads as black letters punched into the picture rather
than as the text disappearing.

---

## Only one styling wins

For non-fragment text these are checked in order, and the first match takes the string:

| Priority | If you set… | You get |
|---|---|---|
| 1 | `textColor: "palette"` with a `palette` set | Ramp across the text - `textBlinkMs`/`textFadeMs` **ignored** |
| 2 | `textFadeMs > 0` | Smooth fade of the resolved color |
| 3 | `textBlinkMs > 0` | Blink of the resolved color |
| 4 | nothing above | Solid `textColor`, else the global `textColor` setting |

So `{"textColor":"palette","palette":"Heat","textBlinkMs":500}` does not blink. Reference table:
[Payload → Which color wins](../reference/payload.md#which-color-wins).

---

## Scrolling, or not

Text scrolls **only when it does not fit**. On the 32×8 panel that is roughly eight characters, and
fewer with an icon, which takes the left quarter of the panel.

Short text is placed once and stays put. Long text rests at the start for a second, then travels
left; once it has fully exited it snaps back and rests again.

All of that lives in one key, `scroll`:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"A LONG HEADLINE THAT WILL NOT FIT","scroll":{"mode":"loop","speed":50}}'
```

| Field | Values | Default | What it does |
|---|---|---|---|
| `mode` | `static` · `wrap` · `loop` · `bounce` | `wrap` | How the text moves |
| `direction` | `left` · `right` | `left` | Which way it travels |
| `entry` | `inline` · `offscreen` | `inline` | Whether it starts on the panel or slides in from outside |
| `whenFits` | `static` · `scroll` | `static` | Whether short text moves too |
| `speed` | `0` and up, percent | `100` | Percentage of the base rate of **21 px/s**; `0` freezes it |
| `gap` | `0` and up, pixels | `8` | `loop` only - the space between repetitions |
| `holdMs` | `0` and up, ms | `1000` | Pause before the text starts, and at each `bounce` turn |

Send only what you want to change. Each field you leave out keeps whatever AWTRIX is configured
with, so `{"scroll":{"mode":"bounce"}}` bounces at your usual speed. When the mode is all you want,
a bare string does it: `{"scroll":"bounce"}`.

Every detail - ranges, what each mode does at the edges, and how errors are reported:
[Payload → Scrolling](../reference/payload.md#scrolling).

### The four modes

- **`wrap`** (default) - the text runs off the far edge, jumps back to the start and holds again.
- **`loop`** - a continuous marquee. The next repetition is already coming in as the last one leaves,
  so nothing ever restarts from an empty panel. `gap` sets how much space is left between them.
- **`bounce`** - the text sweeps until its end is flush with the far edge, then back, pausing at both
  turning points. Set `holdMs` to `0` for a sweep that reverses on the spot.
- **`static`** - no motion at all. The text is drawn once and whatever does not fit is cut off at the
  right edge.

```bash
# Never scroll - clip instead
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"A LONG HEADLINE THAT WILL NOT FIT","scroll":"static"}'
```

### Direction, entry and short text

`direction: "right"` mirrors the whole layout rather than just running the text backwards: it rests
against the far edge, leaves by the near one, and every mode follows.

`entry: "offscreen"` starts the text outside the panel and drops the opening pause. That is the
"scroll in from blank" arrival:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"HEADLINE","scroll":{"entry":"offscreen","whenFits":"scroll"}}'
```

`whenFits: "scroll"` is what makes that work for text short enough to fit, which by default would
never move.

### Keeping a page up until it has been read

A page keeps its normal duration, and text that overflows the panel is shown as far as it gets in
that time. `repeat` asks for whole runs instead - one exit for `wrap`, one round trip for `bounce`:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"A LONG HEADLINE THAT WILL NOT FIT","repeat":1}'
```

The page then stays exactly as long as those runs take. A headline that is read in 4 seconds gives
way after 4 seconds instead of waiting out the rest of the normal app time. Ask for `repeat: 2` to
have it read twice, and set `durationMs` if you want it to stay longer.

Text that never moves - because it fits, or because the mode is `static` - has nothing to count, so
the page keeps its normal duration whatever `repeat` says.

It works the same on a notification.

---

## Centering

`textCenter` defaults to **`true`**, and it applies whenever the text is standing still - when it fits,
or when `scroll.mode` is `static`:

```bash
# Left-align instead
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/label \
  -H 'Content-Type: application/json' \
  -d '{"text":"CPU","textCenter":false}'
```

With an icon, centering happens within the space *right of* the icon column, not across the whole
panel. Once the text is moving, `textCenter` has no effect.

`textOffsetX` nudges the result horizontally, in both the static and the scrolling case:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/label \
  -H 'Content-Type: application/json' \
  -d '{"text":"CPU","textCenter":false,"textOffsetX":2}'
```

There is no vertical control - the baseline is fixed.

`textInFront` sets draw order, not position: `true` paints charts, progress bars and draw commands
first and the text **on top**; the default `false` paints them **over** the text.

See [Payload → Positioning and centering](../reference/payload.md#positioning-and-centering).

---

## Umlauts, accents and other languages

Send **UTF-8** - in plain `text`, in every fragment's `text`, in script output and in drawn labels
alike.

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Grüße, 21°C, 5 €","textCase":"asTyped"}'
```

What survives:

| You send | You get |
|---|---|
| ASCII and the rest of Latin-1 (`ä ö ü ß é à`) | The real accented glyph |
| `°` `€` `…` `– —` `‘ ’ “ ”` | Their own glyphs |
| Latin Extended-A - Polish `ą ć ę ł ń ś ż ź`, Czech `č ď ě ř š ž`, Hungarian `ő ű`, and their capitals | The real accented glyph |
| Cyrillic `А`–`я`, plus `Ё ё Є є І і Ї ї Ґ ґ` | The real glyph, upper and lower case distinct |
| Anything else - Greek, emoji, CJK, Vietnamese | A single `?` placeholder |

One placeholder stands in for one character, so `{"text":"Party 🎉"}` renders as `PARTY ?`.

In `small` an accented capital stands six rows where a bare one stands five: `Ä` and `Č` carry the
mark on top and reach one row lower than `A`, into the row descenders like `g` and `y` use. The row
above the text stays free either way, so a page drawing its own graphics along the top row does not
have to avoid accented capitals. In `large` the mark is fitted into the same seven rows a bare
capital uses, so nothing shifts there.

The global `uppercase` setting and `"textCase":"upper"` work past ASCII: `čerstvý` becomes `ČERSTVÝ`,
`привет` becomes `ПРИВЕТ` - the diacritics are kept, not stripped, for every range AWTRIX maps
(Latin-1, Latin Extended-A, Cyrillic).

Mapping table: [Visual reference → What is mapped](../reference/visuals.md#what-is-mapped).

---

## The fonts

Two, picked per page with the `font` key. You cannot load your own or set a size.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"text":"21°C","font":"large"}'
```

| | `small` (default) | `large` |
|---|---|---|
| Capitals | 5 px tall | 7 px tall |
| Above the text | one row stays free | nothing stays free |

Both cover the same characters, so switching changes how the text looks, never what renders. Take
`large` when the text is the whole page, `small` when the page also draws graphics along the top row.

Most characters are the same width in both fonts, but a handful - `G`, `H`, `I`, `O`, some
punctuation - are not, so a line that just fits in one font can end up scrolling in the other. Try it
if a page sits close to the edge.

Full metrics: [Visual reference → The font](../reference/visuals.md#font).

---

## Related

- [Payload → Text](../reference/payload.md#text) - every text key, with types, ranges and defaults
- [Visual reference → Text](../reference/visuals.md#text) - encoding, font and appearance tables
- [Payload → Colors](../reference/payload.md#colors) - every accepted color form
- [Settings → Global text](../reference/settings.md#global-text) - device-wide `textColor`,
  `uppercase` and `scroll`
- [Effects & overlays](effects.md) - animated backgrounds behind the text
- [Icons & assets](icons.md) - the icon column and how scrolling text interacts with it
