# Settings

The settings are the **saved preferences** of your AWTRIX - brightness, colors,
clock and date formatting, transitions, volume, and the look of the built-in apps.
There are **40 keys**, all camelCase, all readable and writable through one endpoint.

| | |
|---|---|
| **Read** | `GET /api/v1/settings` - returns all 40 keys |
| **Write** | `PATCH /api/v1/settings` - any subset; all keys or none are applied; returns all 40 keys back |
| **Reset** | `POST /api/v1/settings/reset` - clears the stored settings and reboots |

Settings survive a reboot. Durations are integer milliseconds, colors are `"#RRGGBB"` on the way
out and accept several forms on the way in, and a `Content-Type` sent on a write must be
`application/json`. These hold across the whole API and are defined once under
[Conventions](conventions.md).

---

## Reading the settings

```bash
curl http://<awtrix-ip>/api/v1/settings
```

```json
{
  "autoBrightness": false,
  "brightness": 120,
  "autoTransition": true,
  "textColor": "#FFFFFF",
  "transitionEffect": "Rain",
  "transitionDurationMs": 1000,
  "appDurationMs": 7000,
  "timeMode": 1,
  "timeColor": null,
  "buzzerVolume": 80,
  "gamma": 1.9,
  "colorCorrection": null
}
```

*(abbreviated - the real response carries all 40 keys)*

Every value round-trips: what `GET` returns is exactly what `PATCH` accepts.

---

## Updating settings

`PATCH` takes **any subset** of the keys. Omitted keys are left alone.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"brightness": 200, "transitionEffect": "Fade", "timeSeparatorMode": "pulse"}'
```

The whole payload is **validated first**, before anything is written. If any single key is
invalid the request is rejected with `422`, **nothing at all changes**, and the response
names the first offending field:

```json
{ "error": { "code": "validationFailed", "message": "out of range", "field": "brightness" } }
```

A PATCH therefore applies **completely or not at all** - there is no partial write to clean up
after a rejected request. Validation stops at that first failure, and it walks **your payload's
key order**, not the order of the tables below: fix the reported field and re-send to discover
the next one.

The tables below are the complete set of settings keys. Any other key fails validation with `422`
and `"message": "unknown field"`.

A successful `PATCH` responds **`200` with all 40 settings, updated** - use that response instead
of a follow-up `GET`. Every change takes effect at once; no setting needs a reboot.

---

## Resetting to defaults

```bash
curl -X POST http://<awtrix-ip>/api/v1/settings/reset
```

This erases the stored settings and **reboots AWTRIX**, which comes back up on the
defaults listed in this page. Wi-Fi credentials, the panel wiring, the GPIO map and your scripts
are not touched - that is `POST /api/v1/device/factory-reset`, documented in
[HTTP API v1](http.md).

---

## Brightness

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `autoBrightness` | boolean | - | `false` | - | Let the light sensor (LDR) drive panel brightness. |
| `brightness` | integer | 0–255 | `120` | raw level | Panel brightness. **Not a percent.** Ignored as an input while `autoBrightness` is `true`. |

When `autoBrightness` is on, the measured light level sets the brightness and the
`brightness` key is not consulted. The light level is measured and reported either way - see
[Brightness & sensors](../guides/brightness.md).

---

## Panel

These four keys change how the matrix looks, not what the apps draw. They apply to everything on
the panel at once - apps, icons, notifications, the moodlight and Art-Net frames alike - and they
do not change the framebuffer [`GET /api/v1/display/screen`](http.md) returns.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `saturation` | integer | 0-100 | `100` | % | How colourful the panel is. `100` leaves colours as they are, `0` shows everything in greys. |
| `gamma` | number | > 0 | `1.9` | - | Gamma correction for the panel. Must be **strictly** positive - `0` is rejected. No upper bound. |
| `colorCorrection` | color or `null` | - | `null` | - | Per-channel multiplier applied to the whole panel. `null` = off. |
| `colorTint` | color or `null` | - | `null` | - | A second per-channel multiplier (an RGB tint), applied on top of `colorCorrection`. `null` = off. |

`colorTint` is an **RGB color, not a Kelvin value**. Pass it the tint you want multiplied
into every pixel (for example `"#FFD6AA"` to warm the panel), not `2700`. `null` turns the
tint off.

Panel size and wiring - `panelWidth`, `panels`, `panelStart`, `panelWiring`, `panelSerpentine`,
`mirror` and `rotate` - are system configuration, not settings: see
[Panel and orientation](system.md#panel-and-orientation).

---

## Global text

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `textColor` | color | - | `"#FFFFFF"` | - | The global fallback color. **Required - not nullable**; `null` is rejected. Every nullable per-app color inherits it. |
| `uppercase` | boolean | - | `true` | - | Uppercase the text of pushed apps and notifications before rendering. A payload's own `textCase` overrides it per app. |
| `scroll` | object | see below | - | - | Device-wide text motion for pushed apps and notifications. A payload's own `scroll` overrides it field by field. |

### `scroll`

Every field is concrete here - this is what a payload inherits when it leaves the matching key out.

| Field | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `mode` | string | `static` · `wrap` · `loop` · `bounce` | `"wrap"` | - | `static` never moves and clips the overflow · `wrap` runs the text off the far edge and restarts it · `loop` is a continuous marquee with no empty seam · `bounce` sweeps back and forth, holding at both ends. |
| `direction` | string | `left` · `right` | `"left"` | - | Travel direction. `right` mirrors the whole geometry, not just the velocity. |
| `entry` | string | `inline` · `offscreen` | `"inline"` | - | `offscreen` starts the text outside the panel and skips the initial hold. |
| `whenFits` | string | `static` · `scroll` | `"static"` | - | Whether text that already fits the panel still animates. |
| `speed` | integer | ≥ 0 | `100` | percent | Percentage of the 21 px/s base rate. `0` freezes the text, higher is faster, and there is no upper bound. `200` is the sharpest a scroll gets on an 8 px panel; above it legibility drops. |
| `gap` | integer | ≥ 0 | `8` | pixels | `loop` only - the space left between one repetition and the next. |
| `holdMs` | integer | ≥ 0 | `1000` | ms | How long the text rests before it starts moving, and at each `bounce` turning point. `0` removes the pause. |

`PATCH` merges field by field, so `{"scroll":{"mode":"loop"}}` changes the mode and keeps the configured
speed. A bare string is shorthand for the mode: `{"scroll":"loop"}` means the same thing. An unknown
field, an unknown value or a negative number is rejected with `422 validationFailed` and the offending
key in `field`, such as `scroll.speed`.

Changing a field here also moves apps that have already been pushed, unless their own payload set
that field.

```bash
# Continuous marquee everywhere, a little slower than normal
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"scroll": {"mode": "loop", "speed": 80}}'
```

---

## App rotation

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `autoTransition` | boolean | - | `true` | - | Advance through the apps automatically. When `false` the rotation only moves on a button press or an API call. |
| `appDurationMs` | integer | ≥ 0 | `7000` | ms | How long each app is shown before the rotation advances. Also the default lifetime of a notification. No upper bound. |
| `transitionEffect` | string | see below | `"Rain"` | - | The animation played when the rotation changes app. |
| `transitionDurationMs` | integer | 0–2147483647 | `1000` | ms | Length of that animation. `0` = instant. |

Auto-rotation needs **at least two apps** in the list - with a single app there is nothing to
rotate to and AWTRIX sits on it regardless of `autoTransition`.

### `transitionEffect` values

A **name string**, not a number, and it is **case-insensitive** - `"Ripple"`, `"ripple"` and
`"RIPPLE"` all select the same transition. There are 22 of them; the names, and what each one
looks like, are in [Visual reference → Transitions](visuals.md#transitions).
`GET /api/v1/capabilities` returns the same list at runtime.

Anything else is rejected with a message that begins `must be one of:` and then **lists the valid
names**.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"transitionEffect": "Ripple", "transitionDurationMs": 800}'
```

---

## Which apps rotate

No setting decides that. The rotation is one list, set by `PUT /api/v1/apps/order`, and it treats
the five built-ins (**Time**, **Date**, **Temperature**, **Humidity**, **Battery**) exactly like a
pushed app or a script: name it in the order to keep it, omit it to switch it off. Until an order
has ever been set, everything AWTRIX has rotates.
See [Pushed apps - Reordering, switching off and duplicating](../guides/pushed-apps.md#reordering-switching-off-and-duplicating).

**Humidity** and **Battery** need the hardware: on a board with no humidity element or no battery
pin, the app does not exist at all - it is absent from `GET /api/v1/apps` and cannot be ordered
into the loop. See [Power & battery](../guides/power.md).

The keys below shape those apps once they are on screen; they never add or remove one.

---

## Clock app

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `timeMode` | integer | 0–6 | `1` | - | The clock's visual style. See the table below. |
| `timeColor` | color or `null` | - | `null` | - | Clock text color. `null` = inherit `textColor`. In mode 5 this is the **background** the digits are punched out of. |
| `calendarHeaderColor` | color | - | `"#FF0000"` | - | The header bar of the calendar box. Drawn in `timeMode` 1 and 2 only. |
| `calendarTextColor` | color | - | `"#000000"` | - | The day-of-month digits inside the calendar box (`timeMode` 1–4). |
| `calendarBodyColor` | color | - | `"#FFFFFF"` | - | The calendar box background (`timeMode` 1–4). |

### `timeMode` styles

Only these seven exist; anything outside the range is rejected with `422` `"out of range"`.

| Value | Style |
|---|---|
| `0` | Centered time, full-width weekday bar |
| `1` | Calendar box (day of month) with a header bar, time on the right, weekday bar at the bottom |
| `2` | Like `1`, weekday bar on top |
| `3` | Calendar box with corner notches (no header bar), weekday bar at the bottom |
| `4` | Like `3`, weekday bar on top |
| `5` | Big clock - large digits punched black out of a colored field |
| `6` | Binary clock - six bits each for hours (red), minutes (green), seconds (blue) |

The narrow layouts cannot fit everything: `timeMode` 1–4 reserve the left nine columns for
the calendar box and **force `timeShowSeconds` and `timeShowAmPm` off**, and modes `5` and
`6` ignore both entirely. Those keys keep their stored values and read back unchanged - they
simply have no effect in those modes.

---

## Clock text

These shape the time string in `timeMode` 0–4, and - where they fit - the big clock.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `time24h` | boolean | - | `true` | - | 24-hour clock. `false` = 12-hour (midnight and noon show as `12`). |
| `timeLeadingZero` | boolean | - | `true` | - | Pad the **hour** to two digits. Minutes and seconds are always two digits. |
| `timeShowSeconds` | boolean | - | `false` | - | Append `:SS`. |
| `timeShowAmPm` | boolean | - | `false` | - | Append ` AM` / ` PM`. Requires `time24h: false`, and is dropped while seconds are shown. |
| `timeSeparatorMode` | string | `steady` \| `blink` \| `pulse` | `"pulse"` | - | How the `:` between hours and minutes behaves. |

A dropped `timeShowAmPm` is not an error: the key keeps its stored value and is simply not
rendered.

### `timeSeparatorMode` values

Case-insensitive, exactly one of:

| Value | Behavior |
|---|---|
| `steady` | Always at full brightness. |
| `blink` | Hard on/off toggle, once per second. |
| `pulse` | A continuous fade up and back down, one full breath every two seconds. |

The colon keeps its width even while invisible, so the digits never shift as it blinks.

---

## Date text

These shape the built-in Date app.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `dateOrder` | string | `dayMonthYear` \| `monthDayYear` \| `yearMonthDay` | `"dayMonthYear"` | - | Field order of the date. |
| `dateSeparator` | string | `dot` \| `slash` \| `dash` | `"dot"` | - | The character between the fields: `.` `/` `-`. Ignored when `dateMonthNames` is `true`. |
| `dateYearMode` | string | `none` \| `twoDigit` \| `fourDigit` | `"twoDigit"` | - | `none` omits the year, `twoDigit` shows `26`, `fourDigit` shows `2026`. |
| `dateShowWeekday` | boolean | - | `false` | - | Prefix a three-letter English weekday (`Sun`…`Sat`) and a space. |
| `dateMonthNames` | boolean | - | `false` | - | Use three-letter English month names (`Jan`…`Dec`), space-joined, instead of a number: `31 Dec` rather than `31.12`. |
| `dateColor` | color or `null` | - | `null` | - | Date text color. `null` = inherit `textColor`. |

All three enum values are case-insensitive. Day and month numbers are always two digits.

With `dateOrder: "dayMonthYear"`, `dateSeparator: "dot"` and `dateYearMode: "none"`, the date
renders with a **trailing dot** - `31.12.` - which is the correct German short form. No other
combination adds one.

```bash
# Sat 31/12/2026
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"dateShowWeekday": true, "dateSeparator": "slash",
       "dateYearMode": "fourDigit"}'
```

---

## Weekday bar

The seven-segment bar drawn under (or over) the clock and the date.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `weekdayBar` | object | see below | - | - | The whole weekday bar: whether it is drawn, where the week starts, which days are weekend, and the four colors. |

### `weekdayBar`

Every field is concrete here. A segment is colored on **two independent axes** - weekend or
workday, today or not - which is what the four colors cover.

| Field | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `show` | boolean | - | `true` | - | Draw the bar at all. On the clock its position follows `timeMode` - bottom for 0, 1 and 3, top for 2 and 4, and modes 5 and 6 draw no bar. On the Date app it is always at the bottom. |
| `startOnMonday` | boolean | - | `true` | - | Monday is the first segment. `false` starts the week on Sunday. This only rotates the **display order**; it never changes which calendar day a segment stands for. |
| `weekendDays` | array of strings | `sunday` · `monday` · `tuesday` · `wednesday` · `thursday` · `friday` · `saturday` | `["sunday","saturday"]` | - | Which calendar days count as weekend. Any subset, in any order; `[]` means no weekend at all. Lowercase names only. |
| `activeColor` | color | - | `"#FFFFFF"` | - | Today, when today is a workday. Not nullable. |
| `inactiveColor` | color | - | `"#666666"` | - | Any other workday. Not nullable. |
| `weekendActiveColor` | color | - | `"#FFFFFF"` | - | Today, when today is a weekend day. Not nullable. |
| `weekendInactiveColor` | color | - | `"#666666"` | - | Any other weekend day. Not nullable. |

`PATCH` merges field by field, so `{"weekdayBar":{"weekendDays":["friday","saturday"]}}` moves the
weekend and leaves the other six fields alone. An unknown field, a wrong type or a weekday name
that is not one of the seven is rejected with `422 validationFailed` and the offending key in
`field`, such as `weekdayBar.weekendDays` or `weekdayBar.startOnMonday` - the whole request is
refused and nothing is applied.

Weekend membership is decided on the **calendar day**, not on the column number, so `startOnMonday`
never changes which days are weekend. `weekendDays` is read back in calendar order, Sunday first.

The two weekend colors start out identical to the two workday colors, so the bar looks uniform
until you set them yourself.

```bash
# A Friday/Saturday weekend, picked out in amber
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"weekdayBar": {"weekendDays": ["friday", "saturday"],
                      "weekendActiveColor": "#FFAA00",
                      "weekendInactiveColor": "#664400"}}'
```

`weekdayBar.show` is not the same setting as [`dateShowWeekday`](#date-text): the first draws the
seven-segment bar, the second prefixes a three-letter weekday name to the date *text*.

---

## Sensor apps

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `useCelsius` | boolean | - | `true` | - | Show temperature in °C. `false` converts to °F and switches the suffix. Affects the Temperature app only. |
| `temperatureColor` | color or `null` | - | `null` | - | Temperature app text color. `null` = inherit `textColor`. |
| `humidityColor` | color or `null` | - | `null` | - | Humidity app text color. `null` = inherit `textColor`. |
| `batteryColor` | color or `null` | - | `null` | - | Battery app text color. `null` = inherit `textColor`. |

---

## Sound

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
One gain per output, each on the same 0–100 scale. Which of them a panel actually has is
reported by `audio` in [`GET /api/v1/capabilities`](http.md#get-apiv1capabilities), and the web UI
shows a slider only for the outputs that are there.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `soundEnabled` | boolean | - | `true` | - | Mute switch for one-shot sounds. A radio stream keeps playing. |
| `buzzerVolume` | integer | 0–100 | `80` | % | Melodies on the buzzer at `pinBuzzer`. |
| `dfplayerVolume` | integer | 0–100 | `80` | % | Tracks on a DFPlayer Mini. |
| `mp3Volume` | integer | 0–100 | `70` | % | Stored MP3s on the I2S speaker. |
| `radioVolume` | integer | 0–100 | `60` | % | Internet radio on the I2S speaker. |
| `radioMeta` | boolean | - | `true` | - | Show the station on tune-in and each new track title on the panel. Off leaves the rotation untouched while the radio plays. |

`mp3Volume` and `radioVolume` drive the same amplifier but are applied per source, so a station
turned down to sit in the background does not also turn down the doorbell.

With `soundEnabled: false`, sound and melody commands are still **accepted and answered with a
success response** - they simply produce no sound. A mute is not an error. The switch covers
one-shot sounds only: a stream is something you asked for out loud, a notification arrives
uninvited. See [Sound](../guides/sounds.md).

---

## Buttons

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `blockNavigation` | boolean | - | `false` | - | When `true`, the physical left/right buttons do not change apps. |

---

## Value types

### Colors

Colors come back as uppercase `"#RRGGBB"`. On input, every color key accepts any
of the forms listed under [Conventions → Colors](conventions.md#colors) - `"RRGGBB"`,
`"#RGB"` shorthand, `[r, g, b]`, `["HSV", h, s, v]`, or a packed integer.

Two kinds of color key exist:

* **Required colors** - `textColor`, `calendarHeaderColor`, `calendarTextColor`,
  `calendarBodyColor` and all four colors inside `weekdayBar`. A `null` is rejected
  with `422`.
* **Nullable colors** - `timeColor`, `dateColor`, `temperatureColor`, `humidityColor`,
  `batteryColor` (where `null` means *inherit `textColor`*) and `colorCorrection`,
  `colorTint` (where `null` means *off*). All seven default to `null`.

Both extremes round-trip. `"#000000"` on a nullable text color is stored as black and reads back as
`"#000000"`, not `null`; `"#FFFFFF"` on `colorCorrection` or `colorTint` is stored as white and
reads back as `"#FFFFFF"`, which looks the same as *off* because white multiplies every channel by
1. Only an explicit JSON `null` means *inherit `textColor`* or *correction off*.

```bash
# Give the clock its own color, and let the date fall back to textColor again
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"timeColor": "#00AAFF", "dateColor": null}'
```

### Name strings

`transitionEffect`, `timeSeparatorMode`, `dateOrder`, `dateSeparator` and `dateYearMode` are
**name strings**, never numbers, and all of them resolve **case-insensitively** - `"pulse"`,
`"Pulse"` and `"PULSE"` are the same value. `GET /api/v1/capabilities` lists the spelling of
`transitionEffect`; the other four are spelled out in the tables on this page. In every case it is
the spelling `GET /api/v1/settings` returns. Passing a number gives you the same error as passing a
wrong name:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"timeSeparatorMode": 1}'
```

```json
{ "error": { "code": "validationFailed",
             "message": "must be one of: steady blink pulse",
             "field": "timeSeparatorMode" } }
```

The weekday names in `weekdayBar.weekendDays` are the exception: they are matched exactly, in
lowercase.

### Numbers

Integer keys reject JSON floats and booleans - `{"brightness": 120.5}` and
`{"brightness": true}` both fail with `"must be an integer"`. `gamma` is the one key that
takes a fractional number; it accepts a plain integer too, so `2` is a valid gamma.

An upper bound of `2147483647` is the platform integer maximum rather than a meaningful ceiling.

---

## Validation errors

Every failure is a `422` with the standard error body and the offending key in `field`. Unknown keys
are rejected too, so a typo fails loudly rather than being ignored - and nothing in the body is
applied. A body that is not valid JSON fails earlier, with `400 invalidJson`.

Every message this route produces:
[Errors - PATCH /api/v1/settings](errors.md#patch-apiv1settings).
