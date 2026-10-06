# Effects & overlays

This page shows you how to put an animated background behind an app, let rain or snow fall over
it, give an effect new colors, and choose the animation between two apps.

## What you get {#start-here-one-app-one-effect}

Replace `<awtrix-ip>` with the IP address of your clock and run:

<!-- panel motion=2 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
  -H 'Content-Type: application/json' \
  -d '{"text":"HELLO","effect":"Pacifica"}'
```

Ocean waves move behind the text. The app `demo` joins the rotation at once. To remove it:

```bash
curl -X DELETE http://<awtrix-ip>/api/v1/apps/demo
```

Always send `Content-Type: application/json`. Without it, `curl -d` marks the body as a form,
and AWTRIX refuses a `PUT` with that
([Content-Type](../reference/conventions.md#content-type-is-mandatory)). If nothing changes,
check this header first.

## How it behaves

| Tool | What it does | How you set it | For |
|---|---|---|---|
| **Effect** | an animation behind the text and the icon | `effect` | one app |
| **Overlay** | rain, snow and more, over everything | `overlay`, or **Display → Weather overlay** | one app, or all apps |
| **Palette** | new colors for an effect or an overlay | `palette` | one app |
| **Transition** | the animation between two apps | **Display → App rotation**, or `transitionEffect` | all apps |

- An effect fills the whole display, so it takes the place of `backgroundColor`.
- A palette and `effectSpeed` belong to the app that sends them. Another app with the same effect
  keeps its own colors and speed.
- Upper and lower case do not matter in a name. An unknown name is refused with
  `422 validationFailed`, and nothing is saved.
<!-- only tc002 -->
- A pushed app or a notification is drawn at double size, and so is its effect. An overlay uses
  every pixel of the display.
<!-- /only -->

[How the display works](display.md#what-is-drawn-on-top) shows what is drawn over what.

## Pick an effect

AWTRIX has 19 effects. Three of them:

=== "Matrix"

    <!-- panel motion=2 -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
      -H 'Content-Type: application/json' \
      -d '{"text":"HELLO","effect":"Matrix"}'
    ```

    Green trails fall down the columns.

=== "Fireworks"

    <!-- panel motion=3 -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
      -H 'Content-Type: application/json' \
      -d '{"text":"HELLO","effect":"Fireworks"}'
    ```

    Rockets rise and burst into sparks.

=== "TwinklingStars"

    <!-- panel motion=3 -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
      -H 'Content-Type: application/json' \
      -d '{"text":"HELLO","effect":"TwinklingStars"}'
    ```

    Stars light up, fade and come back somewhere else.

All 19 effects, each with what it looks like:
[Visual reference → Background effects](../reference/visuals.md#background-effects). Without
`effect`, or with `"effect":""`, the app has no background effect.

## Slow it down or speed it up

`effectSpeed` multiplies the normal speed: `2.0` is twice as fast, `0.5` half as fast. It works the
same on every effect and every overlay.

<!-- panel -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
  -H 'Content-Type: application/json' \
  -d '{"text":"CALM","effect":"PlasmaCloud","effectSpeed":0.25}'
```

`effectSpeed` goes from `0.1` to `10`. Text painted from a palette has its own speed,
`paletteSpeed`, see [Painting from a palette](text.md#painting-from-a-palette).

## Recolor an effect with a palette {#recolour-an-effect-with-a-palette}

Without a palette, every effect uses its own colors. A `palette` gives it a set of colors to use
instead.

=== "A built-in palette"

    <!-- panel -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
      -H 'Content-Type: application/json' \
      -d '{"text":"HOT","effect":"Plasma","palette":"Heat"}'
    ```

    Eight palettes are built in, for example `Heat`, `Lava` and `Ocean`. All eight and their
    colors: [Visual reference → Palettes](../reference/visuals.md#palettes).

=== "Your own colors in the request"

    <!-- panel -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/demo \
      -H 'Content-Type: application/json' \
      -d '{"text":"MINE","effect":"Plasma","palette":["#FF0000","#FF8800","#FFFF00","#FFFFFF"]}'
    ```

    Up to **16** colors, in any [color form](../reference/visuals.md#accepted-input-forms). They
    are spread evenly and blended, so four colors make a smooth gradient of four colors.

=== "A palette file on the clock"

    A name is first looked up as the file `/PALETTES/<name>.txt` on the clock, with one hex color
    per line. The [palette editor](palette-editor.md) makes these files for you. The format:
    [Visual reference → Custom palettes](../reference/visuals.md#custom-palettes).

`paletteBlend` decides how the colors meet: `true` (default) blends them smoothly, `false` gives
16 sharp color bands.

## Add weather on top

An overlay is drawn **over** the finished app, and your text and icon stay readable underneath. For
one app, put `overlay` into its JSON:

=== "snow"

    <!-- panel motion=3 -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
      -H 'Content-Type: application/json' \
      -d '{"text":"7C","overlay":"snow"}'
    ```

=== "rain"

    <!-- panel motion=2 -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
      -H 'Content-Type: application/json' \
      -d '{"text":"21.5C","overlay":"rain"}'
    ```

=== "storm"

    <!-- panel motion=2 -->
    ```bash
    curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
      -H 'Content-Type: application/json' \
      -d '{"text":"7C","overlay":"storm"}'
    ```

All six overlays, each with what it looks like:
[Visual reference → Weather overlays](../reference/visuals.md#weather-overlays).

The app's `effectSpeed` and `palette` apply to its overlay too. A palette recolors the drops,
flakes, lightning and frost.

## Put weather over every app {#or-over-everything-at-once}

1. Open **Display** in the web UI.
2. Under **Weather overlay**, pick an **Overlay**. **Overlay speed** sets how fast it moves: 100 %
   is the normal speed.
3. Press **Save**.

An app with an `overlay` of its own shows that one instead. Built-in apps show only this one.

### Over the API

`PATCH /api/v1/display` sets one overlay for all apps:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain"}'
```

`overlaySettings` changes its speed and colors. It takes `speed`, `palette` and `blend` in one
object:

```bash
# rain at a third of its normal speed
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":"rain","overlaySettings":{"speed":0.3}}'
```

Switch it off with `null`:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/display \
  -H 'Content-Type: application/json' \
  -d '{"overlay":null}'
```

`GET /api/v1/display` shows the overlay that is set. Details:
[Visual reference → Setting the global overlay](../reference/visuals.md#setting-the-global-overlay).

## Change how apps swap: transitions

A transition is the animation from one app to the next. It is a setting for all apps.

1. Open **Display** in the web UI.
2. Under **App rotation**, pick a **Transition effect** and set the **Transition duration**.
3. Press **Save**.

What each animation looks like: [Visual reference → Transitions](../reference/visuals.md#transitions).

### Over the API {#transitions-over-the-api}

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"transitionEffect":"Pixelate","transitionDurationMs":600}'
```

- `transitionEffect` picks one of **22 animations**. Default: `Rain`.
- `transitionDurationMs` is how long it takes. Default: `1000` ms.
- `autoTransition: false` stops the clock from moving to the next app by itself.

All transition settings: [Settings](../reference/settings.md#transitioneffect-values).

## Discover the names AWTRIX accepts

Ask the clock:

```bash
curl http://<awtrix-ip>/api/v1/capabilities
```

```json
{"effects":["BrickBreaker","Checkerboard","..."],
 "paletteEffects":["Checkerboard","ColorWaves","..."],
 "transitions":["Random","Slide","..."],
 "overlays":["drizzle","frost","..."],
 "palettes":["Cloud","Lava","..."]}
```

`effects`, `overlays`, `transitions` and `palettes` hold every name the clock accepts.
`paletteEffects` lists the effects that use a palette: use it if you build a palette picker. What
each list holds: [Discovering the names](../reference/visuals.md#discovering-the-names).

## Good to know {#when-nothing-happens}

- **`backgroundColor` is ignored.** An effect fills the whole display. Leave out `effect` for a
  plain color behind your text.
- **A palette changes nothing.** `PingPong`, `Matrix` and `LookingEyes` keep their own colors.
  `paletteEffects` lists the effects that take a palette.
- **`paletteBlend` changes nothing.** It needs a `palette` in the same request.
- **A built-in palette shows other colors.** A palette file with its name replaces it. Press
  **Restore the built-in** in the [palette editor](palette-editor.md#change-a-built-in), or delete
  `/PALETTES/<name>.txt`.
- **The text is hard to read over a busy effect.** Slow it down (`"effectSpeed":0.3`), use a dark
  palette (`"palette":"Ocean"`), or pick a calmer effect. An effect cannot be dimmed.

## Details

- [Background effects](../reference/visuals.md#background-effects): all 19 effects, and which
  ones take a palette
- [Effect settings](../reference/visuals.md#effect-settings): `effectSpeed`, `palette` and
  `paletteBlend`, with their ranges
- [Weather overlays](../reference/visuals.md#weather-overlays): all six overlays, the overlay for
  all apps and which one wins
- [Palettes](../reference/visuals.md#palettes): the built-in palettes and palette files
- [Transitions](../reference/visuals.md#transitions): all 22 transitions
- [Render order](../reference/payload.md#render-order): every layer of an app
- [Errors](../reference/errors.md#the-error-body): the error format. Check its `code` and `field`
  in your automations, not the message text.

## Related

- [Visual reference](../reference/visuals.md): every effect, overlay, palette and transition
- [App & notification payload](../reference/payload.md): every key you can use next to `effect`
- [Charts & drawing](graphics.md): charts and drawings over an effect
- [Palette editor](palette-editor.md): make your own palettes in the web UI
- [Settings](../reference/settings.md): `transitionEffect` and the other settings for all apps
