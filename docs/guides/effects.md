# Effects & overlays

You want an app that does more than sit there in white text - an animated background behind it,
rain falling over it, a colour scheme that matches the rest of your dashboard, or a nicer animation
when AWTRIX moves from one app to the next.

AWTRIX NG gives you four independent tools for that:

| Tool | What it does | Where it goes | Scope |
|---|---|---|---|
| **Background effect** | Animates *behind* text and icons | `effect` on a payload | per app |
| **Weather overlay** | Draws *on top of* the finished page | `overlay` on a payload, or `PATCH /api/v1/display` | per app, or device-wide |
| **Palette** | Recolours a palette-driven effect, or an overlay | `palette` on a payload | per app |
| **Transition** | Animates the change *between* apps | `transitionEffect` in settings | device-wide only |

This guide shows you how to reach for each one. The exhaustive name tables live in
[Visual reference](../reference/visuals.md).

---

## Start here: one app, one effect

Paste this. It creates a pushed app called `demo` with an ocean-wave background behind the text:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
  -H 'Content-Type: application/json' \
  -d '{"text":"HELLO","effect":"Pacifica"}'
```

The app joins the rotation immediately. To take it away again:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/apps/demo
```

Everything else on this page is a variation on that one payload.

Every example here sends `Content-Type: application/json`, which is
[mandatory on every write](../reference/conventions.md#content-type-is-mandatory) - if your app
fails to change, a missing header is the first thing to check.

---

## Pick an effect

**19 background effects** are built into AWTRIX. A few to start from:

| `effect` | What it looks like |
|---|---|
| `Pacifica` | Rolling ocean waves in blue and teal |
| `Plasma` | A colour swirl across the whole panel, sweeping the full hue wheel |
| `PlasmaCloud` | The same, softer and slower, in a narrower band of colour |
| `ColorWaves` | A hue sweep travelling sideways across the panel |
| `Matrix` | Green trails falling down every column |
| `TwinklingStars` | Scattered stars twinkling on and off |

The other thirteen - `TheaterChase`, `Fade`, `MovingLine`, `BrickBreaker`, `PingPong`, `Radar`,
`Checkerboard`, `Fireworks`, `Ripple`, `Snake`, `SwirlIn`, `SwirlOut`, `LookingEyes` - are described
one by one, with which of them honour a palette, in
[Visual reference → Background effects](../reference/visuals.md#background-effects).

Names are matched **case-insensitively** - `Pacifica`, `pacifica` and `PACIFICA` are the same
effect. Omit `effect` (or send an empty string) and the app has no background.

A name AWTRIX does not know is rejected with `422 validationFailed` on the `effect` field, and
**nothing is stored** - the app is not created or changed. For an array payload the whole batch is
rejected, so you never end up with half of it applied. The exact spellings come from
[`GET /api/v1/capabilities`](#discover-the-names-awtrix-accepts).

---

## Tune it

Three keys on the same payload shape how an effect looks, and the palette is the app's own - the
same one its text and charts can paint from:

- **`effectSpeed`** - how fast the animation runs. `1.0` is the effect's own pace.
- **`palette`** - the colours it draws from: a built-in name, a palette file on AWTRIX, or up to
  16 colour stops inline. Left out, the effect keeps its own colours.
- **`paletteBlend`** - `true`, the default, blends between the palette's colours; `false` gives
  16 hard bands.

Types, ranges and defaults are in
[Visual reference → Effect settings](../reference/visuals.md#effect-settings).

All three at once:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
  -H 'Content-Type: application/json' \
  -d '{"text":"HI","effect":"Plasma","effectSpeed":0.4,"palette":"Lava"}'
```

### Slow it down or speed it up

`effectSpeed` multiplies the effect's own pace: `2.0` is twice as fast, `0.5` is half speed. It
means the same thing on **every** effect and on **every overlay**; there is no effect where it means
something else.

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
  -H 'Content-Type: application/json' \
  -d '{"text":"CALM","effect":"PlasmaCloud","effectSpeed":0.25}'
```

A value outside `0.1 … 10` is silently pulled to the nearest bound rather than rejected with an
error, so an animation can neither freeze nor run away.

Palette-painted text is the exception: it moves on its own `paletteSpeed`, so `effectSpeed` does
not affect it.

---

## Recolour an effect with a palette

Until you supply a palette, every effect keeps its own built-in colours. A `palette` hands it a set
of colours to draw from instead.

=== "A built-in palette"

    **8 built-in names** are accepted, case-insensitively: `Cloud`, `Lava`, `Ocean`, `Forest`,
    `Stripe`, `Party`, `Heat`, `Rainbow`. Each one's character is described in
    [Visual reference → Palettes](../reference/visuals.md#palettes).

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
      -H 'Content-Type: application/json' \
      -d '{"text":"HOT","effect":"Plasma","palette":"Heat"}'
    ```

=== "Your own colours inline"

    Up to **16** entries, each accepting the full
    [colour form](../reference/visuals.md#accepted-input-forms):

    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
      -H 'Content-Type: application/json' \
      -d '{"text":"MINE","effect":"Plasma","palette":["#FF0000","#FF8800","#FFFF00","#FFFFFF"]}'
    ```

    The entries are colour **stops**, spread across the whole ramp by interpolation, so four stops
    describe a smooth four-colour gradient. An empty array is rejected.

=== "A palette file on AWTRIX"

    A name is looked up as `/PALETTES/<name>.txt` on the AWTRIX filesystem first - one hex colour
    per line, one to sixteen lines, read as stops exactly like the inline list. See
    [Visual reference → Custom palettes](../reference/visuals.md#custom-palettes).

    Because the file is tried first, a file named after a built-in **replaces** it for as long as
    it exists, and deleting the file brings the built-in back - that is what the web UI's
    [palette editor](palette-editor.md) writes when you change `Heat`.

A `palette` that matches neither a file nor a built-in is a **422 `validationFailed`** on the
`palette` field, not a silent substitution.

### `paletteBlend` needs a palette to do anything

`paletteBlend` interpolates *between palette entries*, so it changes nothing unless the payload
also supplies a `palette`. With the default `true` you get smooth gradients; `false` gives
**16 hard colour bands**.

### Four effects ignore palettes

`BrickBreaker`, `PingPong`, `Matrix` and `LookingEyes` have fixed colours. Sending a palette with
one of them returns success and changes nothing on screen. `GET /api/v1/capabilities` →
`paletteEffects` lists exactly the effects that *do* honour a palette, so you never have to guess.

### Each app keeps its own palette

The palette belongs to the app that sent it. If app A and app B both use `Plasma` and app B sets
`palette: "Lava"`, **app A keeps its own colours** - two differently-coloured `Plasma` apps can sit
side by side. Deleting an app that set a palette leaves no residue behind.

---

## Add weather on top

An overlay draws **over** the finished page - it never clears the canvas, so your text and icons
stay readable underneath. **6 overlays** exist: `rain`, `snow`, `drizzle`, `storm`, `thunder`,
`frost`. What each one looks like is in
[Visual reference → Weather overlays](../reference/visuals.md#weather-overlays).

Per app:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"text":"7C","overlay":"snow"}'
```

Overlay names are matched case-insensitively - `rain`, `Rain` and `RAIN` are all the same overlay.
An unknown name is rejected with `422 validationFailed` on the `overlay` field and the app is not
stored.

Overlays honour the payload's `effectSpeed` **and** `palette`: a supplied palette recolours the
drops, flakes, bolts and frost. Without a palette they keep their stock colours. Heavier weather
falls faster - `storm` is brisker than `drizzle` - and the speed scales whichever one applies.

### Or over everything at once

`PATCH /api/v1/display` sets a device-wide overlay, which also covers the built-in apps:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain"}'
```
<!-- shot: base={"text":"21.5C"} -->

Tune it with `overlaySettings` - `speed`, `palette` and `blend` in one object, because the
device-wide overlay owns its settings rather than borrowing an app's:

```bash
# rain at a third of its pace
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain","overlaySettings":{"speed":0.3}}'
```
<!-- shot: base={"text":"21.5C"} -->

The Web UI has the same knob as an **Overlay speed** slider under **Display → Weather overlay**.

Clear it with `null`, which drops the settings along with the overlay:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":null}'
```

Read the current value back from `GET /api/v1/display`. Both fields are documented in
[Visual reference → Setting the global overlay](../reference/visuals.md#setting-the-global-overlay).

### Which one wins

- **Pushed apps and notifications:** a per-app `overlay` **beats** the global one. The global
  overlay applies only when the payload carries no `overlay` of its own.
- **Built-in apps** (time, date, temperature, humidity, battery): **only** the global overlay
  applies - they have no payload to carry a per-app one.

A per-app overlay runs on that app's own `effectSpeed` and `palette`; the device-wide one runs on
`overlaySettings`, whichever app is on screen.

---

## How it all composes with your text

One page is drawn in this order, bottom to top:

1. **Background effect** - or, when there is no effect, the flat `backgroundColor`.
2. **Text and decorations** (charts, progress bar, draw ops). Decorations draw over the text;
   `textInFront: true` reverses that pair so the text lands on top. The background below is
   unaffected either way.
3. **Icon**, if the app has one.
4. **Overlay**.

Two consequences worth knowing. An effect **replaces** `backgroundColor` rather than sitting on top
of it: when `effect` names a real effect, it paints the whole canvas and your `backgroundColor` is
never applied. Sending both is not an error - the colour is simply ignored. Drop the `effect` key if
you want a flat colour behind your text.

And a busy effect like `Plasma` or `PlasmaCloud` fills the whole canvas, so text over it can
disappear. Slow the effect down (`"effectSpeed":0.3`), give it a dark palette (`"palette":"Ocean"`),
or pick a calmer one. There is no dimming control for backgrounds.

Layering with charts and draw ops is covered in [Charts & drawing](graphics.md#layering).

---

## Change how apps swap: transitions

A transition animates the change from one app's page to the next. It is a **device-wide setting** -
there is no way to give one app its own transition.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"transitionEffect":"Pixelate","transitionDurationMs":600}'
```

`transitionEffect` picks one of **22 animations** and defaults to `Rain`; `transitionDurationMs`
sets how long one takes, `1000` ms out of the box. Set `autoTransition` to `false` if you would
rather AWTRIX stopped rotating through apps altogether. All three are described in
[Settings](../reference/settings.md#transitioneffect-values), and what each animation looks like is
in [Visual reference → Transitions](../reference/visuals.md#transitions).

`Random` re-picks one of the other twenty-one for every transition - it never picks itself.

Names are matched case-insensitively, and an unknown one is rejected with `422 validationFailed`
whose message lists all 22 accepted names.

`Ripple` and `Fade` each exist as a background **effect** *and* as a transition, with entirely
unrelated behaviour: `{"effect":"Ripple"}` draws expanding rings behind your text, while
`{"transitionEffect":"Ripple"}` reveals the next app in a circle. Read the name in the context of
the key that carries it.

---

## Discover the names AWTRIX accepts

To get the exact names AWTRIX accepts, ask it directly:

```bash
curl http://<awtrix-ip>/api/v1/capabilities
```

```json
{"effects":["BrickBreaker","Checkerboard","..."],
 "paletteEffects":["Checkerboard","ColorWaves","..."],
 "transitions":["Random","Slide","..."],
 "overlays":["drizzle","frost","..."],
 "palettes":["Cloud","Lava","Ocean","Forest","Stripe","Party","Heat","Rainbow"]}
```

`effects` and `overlays` are always exactly what AWTRIX can draw, sorted alphabetically
rather than in any meaningful order.

`paletteEffects` lists the effects that actually use the app's `palette` - the scene-style effects
that draw fixed colours (`BrickBreaker`, `PingPong`, `Matrix`, `LookingEyes`) are absent from it.
Build a palette picker from this list rather than from `effects`.

These names are spelled the way the API returns them. You may send any casing you like; AWTRIX
resolves it case-insensitively.

`palettes` lists the 8 built-in names - the ones that always resolve. It does not list your palette
files; read those from `GET /api/v1/files?dir=/PALETTES`. A file may carry a built-in's name and
replaces it while it exists, so a name on this list does not promise the built-in colours.

---

## When nothing happens

| Symptom | Likely cause |
|---|---|
| `422 validationFailed` on `effect` | The name is not in `/api/v1/capabilities` → `effects`. Casing does not matter; spelling does. |
| `415 unsupportedMediaType` | Missing `Content-Type: application/json` - `curl -d` sends a form body. |
| Palette had no visible effect | The effect is not in `/api/v1/capabilities` → `paletteEffects` - e.g. `BrickBreaker`, `PingPong`, `Matrix`, `LookingEyes`. |
| `422 validationFailed` on `palette` | Neither a `/PALETTES/<name>.txt` nor one of the 8 built-in names. |
| A built-in palette shows the wrong colours | A file of that name overrides it. Delete `/PALETTES/<name>.txt`, or use the web UI's restore button. |
| `paletteBlend` changed nothing | No palette was sent alongside it. |
| `422 validationFailed` on `overlay` | The name is not one of the 6 overlays; check the spelling. |
| `backgroundColor` ignored | An `effect` is set - it paints the whole canvas itself. |

Match on the error `code` and `field`, never on the wording - the full error body is written out
under [Errors](../reference/errors.md#the-error-body).

---

## Related

- [Visual reference](../reference/visuals.md) - every effect, overlay, palette and transition, in full.
- [App & notification payload](../reference/payload.md) - every key you can put next to `effect`.
- [Charts & drawing](graphics.md) - bars, lines and draw ops layered over an effect.
- [Settings](../reference/settings.md) - `transitionEffect` and the rest of the device-wide knobs.
