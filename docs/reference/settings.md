# Settings

Settings are your **saved preferences**: brightness, colors, clock and date format, transitions,
volume, and the look of the built-in apps. This page lists every settings key.

| | |
|---|---|
| **Read** | `GET /api/v1/settings` – returns all settings keys |
| **Write** | `PATCH /api/v1/settings` – any subset; either all keys are applied or none; returns all settings keys |
| **Reset** | `POST /api/v1/settings/reset` – deletes the stored settings and restarts |

Settings survive a restart. Durations are whole milliseconds. Colors come back as `"#RRGGBB"` and
can be sent in several forms. If you send a `Content-Type`, it must be `application/json`. These
rules apply to the whole API and are described once under [Conventions](conventions.md).

Restoring a backup skips settings that this clock does not have.

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
  "volume": 60,
  "gamma": 1.9,
  "colorCorrection": null
}
```

*(shortened – the real response contains all settings keys)*

You can send back exactly what `GET` returns: every value `GET` returns is accepted by `PATCH`.

---

## Updating settings

Send **any subset** of the keys with `PATCH`. Keys you leave out stay as they are.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"brightness": 200, "transitionEffect": "Fade", "timeSeparatorMode": "pulse"}'
```

AWTRIX checks the whole request **before** it changes anything. If one key is not valid, it answers
`422`, **nothing changes**, and the answer names the first wrong key:

```json
{ "error": { "code": "validationFailed", "message": "out of range", "field": "brightness" } }
```

So a `PATCH` is applied **completely or not at all**. AWTRIX reports only the first problem, in the
order of the keys in your request. Fix it and send again to find the next one.

The tables below list every settings key. Any other key is rejected with `422` and
`"message": "unknown field"`.

A successful `PATCH` answers **`200` with all settings**, already updated – you do not need a
second `GET`. Every change takes effect at once; no setting needs a restart.

---

## Resetting to defaults

```bash
curl -X POST http://<awtrix-ip>/api/v1/settings/reset
```

This deletes the stored settings and **restarts AWTRIX** with the defaults listed on this page.
Wi-Fi<!-- only esp32 esp32-s3 -->, display wiring, pin map<!-- /only --> and your scripts stay. To reset everything, use
`POST /api/v1/device/factory-reset` – see [HTTP API](http.md).

---

## Brightness

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
<!-- only esp32 esp32-s3 -->
| `autoBrightness` | boolean | - | `false` | - | Let the light sensor (LDR) set the brightness. No effect on a device without a light sensor. |
| `brightness` | integer | 0–255 | `120` | raw level | Display brightness. **Not a percentage.** Ignored while `autoBrightness` is `true` on a device with a light sensor. |

With `autoBrightness` on, the measured light sets the brightness and `brightness` is ignored. The
light level is measured and reported in both cases – see
[Brightness & sensors](../guides/brightness.md). On a device without a light sensor
(`sensors.light` is `false` in [`GET /api/v1/capabilities`](http.md#get-apiv1capabilities)) you
can still set and read `autoBrightness`, but the display always uses `brightness`.
<!-- /only -->
<!-- only tc002 -->
| `autoBrightness` | boolean | - | `false` | - | No effect: the clock has no light sensor. You can still set and read it. |
| `brightness` | integer | 0–255 | `120` | raw level | Display brightness. **Not a percentage.** The knob sets it too. |
<!-- /only -->

---

## Color {#panel}

These four keys change how the whole display looks – apps, icons, notifications<!-- only esp32 esp32-s3 -->, the mood light and
Art-Net alike<!-- /only --><!-- only tc002 --> and the mood light alike<!-- /only -->. They do not change the picture that
[`GET /api/v1/display/screen`](http.md) returns.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `saturation` | integer | 0-100 | `100` | % | How colourful the display is. `100` keeps colors as they are, `0` shows everything in gray. |
| `gamma` | number | > 0 | `1.9` | - | Gamma correction for the display. Must be **greater than** `0` – `0` is rejected. No upper limit. |
| `colorCorrection` | color or `null` | - | `null` | - | A color that every pixel is multiplied with. `null` = off. |
| `colorTint` | color or `null` | - | `null` | - | A second color that every pixel is multiplied with, on top of `colorCorrection`. `null` = off. |

`colorTint` is an **RGB color, not a Kelvin value**. To make the display warmer, send for example
`"#FFD6AA"`, not `2700`. `null` switches the tint off.

What each of these looks like on the display is described under
[Display colors](visuals.md#display-color-pipeline).

<!-- only esp32 esp32-s3 -->
Panel size and wiring – `panelWidth`, `panels`, `panelStart`, `panelWiring`, `panelColorOrder`,
`panelSerpentine`, `mirror` and `rotate` – are part of the system configuration, not settings. See
[Panel and orientation](system.md#panel-and-orientation).
<!-- /only -->

---

## Global text

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `textColor` | color | - | `"#FFFFFF"` | - | The default text color. **Cannot be `null`.** Every per-app color that is `null` uses this one. |
| `uppercase` | boolean | - | `true` | - | Show the text of pushed apps and notifications in capitals. A payload's own `textCase` overrides it.<!-- only tc002 --> The clock face ignores it and always writes weekday and month names in capitals.<!-- /only --> |
| `scroll` | object | see below | - | - | How text moves in pushed apps and notifications. A payload's own `scroll` overrides it field by field. |
<!-- only tc002 -->
| `enlargeApps` | boolean | - | `true` | - | `true`: pushed apps and notifications are drawn at double size, as if the display were 26×8. An app or a notification with an icon bigger than 26×8 keeps its size. Apps and notifications with a [layout](../guides/layouts.md), scripts and the built-in apps keep their size too. |
<!-- /only -->

### `scroll`

These are the values a payload uses when it leaves out the matching field.

| Field | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `mode` | string | `static` · `wrap` · `loop` · `bounce` | `"wrap"` | - | `static` never moves and cuts off what does not fit · `wrap` scrolls the text off the edge and starts again · `loop` scrolls continuously without a gap at the end · `bounce` moves back and forth and pauses at both ends. |
| `direction` | string | `left` · `right` | `"left"` | - | Scroll direction. `right` mirrors the whole movement. |
| `entry` | string | `inline` · `offscreen` | `"inline"` | - | `offscreen` starts the text outside the display and skips the first pause. |
| `whenFits` | string | `static` · `scroll` | `"static"` | - | Whether text that fits on the display still moves. |
| `speed` | integer | ≥ 0 | `100` | percent | Percentage of the base speed of 21 px/s. `0` stops the text, higher is faster, no upper limit. <!-- only esp32 esp32-s3 -->On an 8 px high display, text stays sharp up to about `200`.<!-- /only --><!-- only tc002 -->In an app drawn at double size (`enlargeApps`), a pixel is a 2×2 square, so the text crosses 42 display pixels per second in steps of two.<!-- /only --> |
| `gap` | integer | ≥ 0 | `8` | pixels | `loop` only – the space between one repetition and the next.<!-- only tc002 --> At double size each of these pixels is two display pixels wide.<!-- /only --> |
| `holdMs` | integer | ≥ 0 | `1000` | ms | How long the text pauses before it starts moving, and at each end in `bounce`. `0` = no pause. |

`PATCH` changes only the fields you send: `{"scroll":{"mode":"loop"}}` changes the mode and keeps
the speed. A plain string sets only the mode: `{"scroll":"loop"}` means the same. An unknown field,
an unknown value or a negative number is rejected with `422 validationFailed`, and `field` names
the key, for example `scroll.speed`.

A change here also affects apps you have already pushed, unless their payload set that field.

```bash
# Continuous scrolling everywhere, a little slower than normal
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"scroll": {"mode": "loop", "speed": 80}}'
```

---

## App rotation

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `autoTransition` | boolean | - | `true` | - | Switch to the next app automatically. With `false` the app changes only on a button press or an API call. |
| `appDurationMs` | integer | ≥ 0 | `7000` | ms | How long each app is shown. Also the default duration of a notification. No upper limit. |
| `transitionEffect` | string | see below | `"Rain"` | - | The animation between two apps. |
| `transitionDirection` | string | `normal` · `reverse` | `"normal"` | - | Reverses the movement of transitions that have a direction. The app order stays the same. |
| `transitionDurationMs` | integer | 0–2147483647 | `1000` | ms | Length of the animation. `0` = instant. |

The apps rotate only when there are **at least two** in the list. With one app, AWTRIX stays on it
whatever `autoTransition` says.

### `transitionEffect` values

A **name**, not a number. Upper and lower case do not matter – `"Ripple"`, `"ripple"` and
`"RIPPLE"` are the same. There are 22 transitions; their names and what they look like are in
[Visual reference → Transitions](visuals.md#transitions). `GET /api/v1/capabilities` returns the
same list.

Any other value is rejected with a message that starts with `must be one of:` and then **lists the
valid names**.

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"transitionEffect": "Ripple", "transitionDirection": "reverse", "transitionDurationMs": 800}'
```

With `normal`, automatic and **Next** transitions move in their usual direction. `reverse` flips
that movement; **Previous** always moves the other way. Transitions without a direction, such as
`Fade`, look the same either way.

---

## Which apps rotate

No setting decides that. The rotation is one list, set with `PUT /api/v1/apps/order`. Built-in apps
(<!-- only esp32 esp32-s3 -->**Time**, **Date**, **Temperature**, **Humidity**, **Battery**<!-- /only --><!-- only tc002 -->**Time** and **Status**<!-- /only -->) are treated exactly like pushed apps and scripts: `order` sets the order, and `disabled`
switches an app off. Enabled apps missing from `order` are added at the end. Until you change it,
all available apps rotate. See
[Pushed apps – Reordering, switching off and duplicating](../guides/pushed-apps.md#reordering-switching-off-and-duplicating).

<!-- only esp32 esp32-s3 -->
**Humidity** and **Battery** need the hardware. On a board without a humidity sensor or without a
battery pin, the app does not exist: it is missing from `GET /api/v1/apps` and cannot be added to
the rotation. See [Power & battery](../guides/power.md).
<!-- /only -->

The keys below change how these apps look. They never add or remove an app.

---

## Clock app

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
<!-- only esp32 esp32-s3 -->
| `timeMode` | integer | 0–6 | `1` | - | The clock style. See the table below. |
| `timeColor` | color or `null` | - | `null` | - | Clock text color. `null` = use `textColor`. In mode 5 this is the **background** color around the black digits. |
| `calendarHeaderColor` | color | - | `"#FF0000"` | - | The top bar of the calendar box in `timeMode` 1 and 2. |
| `calendarTextColor` | color | - | `"#000000"` | - | The day number in the calendar box (`timeMode` 1–4). |
| `calendarBodyColor` | color | - | `"#FFFFFF"` | - | The calendar box background (`timeMode` 1–4). |
<!-- /only -->
<!-- only tc002 -->
| `clockFace` | string | `sheet` \| `ring` \| `flap` \| `month` \| `big` | `"sheet"` | - | The clock face. See [Clock faces](#tc002-clock-faces). |
| `timeMode` | integer | 0–6 | `1` | - | No effect. `clockFace` picks the face. |
| `timeColor` | color or `null` | - | `null` | - | Clock text color. `null` = use `textColor`. |
| `calendarHeaderColor` | color | - | `"#FF0000"` | - | The top of the sheet, the weekends in the month grid and the weekday on the `big` face. |
| `calendarTextColor` | color | - | `"#000000"` | - | The day on the sheet and the workdays in the month grid. |
| `calendarBodyColor` | color | - | `"#FFFFFF"` | - | The paper of the sheet. |
| `calendarAnimation` | boolean | - | `true` | - | `true`: the calendar sheet is torn off when the clock appears and at midnight. `false`: the sheet shows today right away. |
<!-- /only -->

<!-- only esp32 esp32-s3 -->
### `timeMode` styles

Only these seven exist. Any other number is rejected with `422` `"out of range"`.

| Value | Style |
|---|---|
| `0` | Time in the middle, weekday bar across the full width |
| `1` | Calendar box (day of month) with a top bar, time on the right, weekday bar at the bottom |
| `2` | Like `1`, weekday bar at the top |
| `3` | Calendar box with notched corners (no top bar), weekday bar at the bottom |
| `4` | Like `3`, weekday bar at the top |
| `5` | Big clock – large black digits on a colored background |
| `6` | Binary clock – six bits each for hours (red), minutes (green) and seconds (blue) |

Not everything fits in every style. `timeMode` 1–4 use the left nine columns for the calendar box,
so **seconds and AM/PM are not shown** there. Modes `5` and `6` also ignore `timeShowSeconds` and
`timeShowAmPm`. The keys keep their stored values; they simply have no effect in these modes.
<!-- /only -->
<!-- only tc002 -->
### Clock faces {#tc002-clock-faces}

The clock is made for its 52×16 display. `clockFace` picks one of five faces:

| Value | Face |
|---|---|
| `sheet` | A tear-off calendar sheet with month and day, the time next to it and the weekday bar below the time. With the weekday bar off, the sheet shows the weekday instead of the month. |
| `ring` | The same sheet on two rings, with the day but without the month. |
| `flap` | The sheet with weekday and day; hours and minutes on two split-flap cards that flip at each full minute. |
| `month` | The whole month as a grid of dots on the sheet: weekends in the header color, past days dimmed, today in blue. |
| `big` | The time across the whole display, with weekday and date below. |

`timeShowSeconds` shows seconds on the `big` face only; next to a sheet there is no room.
`timeShowAmPm` and `dateShowWeekday` have no effect on the clock faces. The calendar colors paint
every sheet. The `big` face shows the weekday in `calendarHeaderColor` and the date in `dateColor`,
formatted with `dateOrder`, `dateSeparator`, `dateYearMode` and `dateMonthNames`. If the date is too
wide, it drops the century first, then the year. Weekday and month names are always in capitals,
whatever `uppercase` says.

When the clock appears, the sheet slides in showing yesterday and then falls off to show today.
This also happens when the clock comes back after a notification, the mood light or a dark
display, and at midnight. Switch off **Calendar animation** (`calendarAnimation`) and the sheet
shows today right away.
<!-- /only -->

---

## Clock text

<!-- only esp32 esp32-s3 -->
These keys format the time in `timeMode` 0–4 and, where there is room, on the big clock.
<!-- /only -->
<!-- only tc002 -->
These keys format the time on the [clock faces](#tc002-clock-faces), where there is room.
<!-- /only -->

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `time24h` | boolean | - | `true` | - | 24-hour clock. `false` = 12-hour clock (midnight and noon show as `12`). |
| `timeLeadingZero` | boolean | - | `true` | - | Show the **hour** with two digits. Minutes and seconds always have two digits. |
| `timeShowSeconds` | boolean | - | `false` | - | Add `:SS`. |
| `timeShowAmPm` | boolean | - | `false` | - | Add ` AM` / ` PM`. Needs `time24h: false`, and is not shown while seconds are shown. |
| `timeSeparatorMode` | string | `steady` \| `blink` \| `pulse` | `"pulse"` | - | How the `:` between hours and minutes behaves. |

If AM/PM is not shown for one of these reasons, that is not an error: the key keeps its value.

### `timeSeparatorMode` values

Upper and lower case do not matter. One of:

| Value | Behavior |
|---|---|
| `steady` | Always fully on. |
| `blink` | On and off, once per second. |
| `pulse` | Fades smoothly down and up again, once every two seconds. |

The colon keeps its space while it is dark, so the digits do not move.

---

## Date text

These keys format the <!-- only esp32 esp32-s3 -->built-in Date app<!-- /only --><!-- only tc002 -->date on the `big` clock face<!-- /only -->.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `dateOrder` | string | `dayMonthYear` \| `monthDayYear` \| `yearMonthDay` | `"dayMonthYear"` | - | Order of day, month and year. |
| `dateSeparator` | string | `dot` \| `slash` \| `dash` | `"dot"` | - | The character between the parts: `.` `/` `-`. Ignored when `dateMonthNames` is `true`. |
| `dateYearMode` | string | `none` \| `twoDigit` \| `fourDigit` | `"twoDigit"` | - | `none` hides the year, `twoDigit` shows `26`, `fourDigit` shows `2026`. |
| `dateShowWeekday` | boolean | - | `false` | - | Put a three-letter English weekday (`Sun`…`Sat`) and a space in front. |
| `dateMonthNames` | boolean | - | `false` | - | Use three-letter English month names (`Jan`…`Dec`) with spaces instead of a number: `31 Dec` instead of `31.12`. |
| `dateColor` | color or `null` | - | `null` | - | Date text color. `null` = use `textColor`. |

Upper and lower case do not matter for the three name values. Day and month always have two digits.

With `dateOrder: "dayMonthYear"`, `dateSeparator: "dot"` and `dateYearMode: "none"`, the date ends
with a **dot** – `31.12.` – the usual German short form. No other combination adds one.

```bash
# Sat 31/12/2026
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"dateShowWeekday": true, "dateSeparator": "slash",
       "dateYearMode": "fourDigit"}'
```

---

## Weekday bar

The row of seven small segments <!-- only esp32 esp32-s3 -->below (or above) the clock and the date<!-- /only --><!-- only tc002 -->below the time<!-- /only -->, one per weekday.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `weekdayBar` | object | see below | - | - | The clock's weekday bar.<!-- only esp32 esp32-s3 --> A `PATCH` with it sets the Date app's bar too.<!-- /only --> |
<!-- only esp32 esp32-s3 -->
| `dateWeekdayBar` | object | see below | - | - | The Date app's weekday bar. A `PATCH` with it sets only this bar, also when `weekdayBar` is in the same request. |
<!-- /only -->
<!-- only tc002 -->
| `dateWeekdayBar` | object | see below | - | - | No effect: the clock has no Date app. |
<!-- /only -->

### `weekdayBar` and `dateWeekdayBar`

Each segment is either a workday or a weekend day, and either today or not. The four colors cover
these four cases.

| Field | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `show` | boolean | - | `true` | - | Show the bar. <!-- only esp32 esp32-s3 -->On the clock its position follows `timeMode`: bottom for 0, 1 and 3, top for 2 and 4, no bar in modes 5 and 6. On the Date app it is always at the bottom.<!-- /only --><!-- only tc002 -->The clock shows it below the time on the `sheet` and `ring` faces.<!-- /only --> |
| `startOnMonday` | boolean | - | `true` | - | Monday is the first segment. `false` starts the week on Sunday. This changes only the **order** shown, not which day a segment stands for. |
| `weekendDays` | array of strings | `sunday` · `monday` · `tuesday` · `wednesday` · `thursday` · `friday` · `saturday` | `["sunday","saturday"]` | - | Which days are weekend. Any selection, in any order; `[]` means no weekend. Lowercase names only. |
| `activeColor` | color | - | `"#FFFFFF"` | - | Today, when today is a workday. Cannot be `null`. |
| `inactiveColor` | color | - | `"#666666"` | - | Other workdays. Cannot be `null`. |
| `weekendActiveColor` | color | - | `"#FFFFFF"` | - | Today, when today is a weekend day. Cannot be `null`. |
| `weekendInactiveColor` | color | - | `"#666666"` | - | Other weekend days. Cannot be `null`. |

`PATCH` changes only the fields you send: `{"weekdayBar":{"weekendDays":["friday","saturday"]}}`
moves the weekend and keeps the other six fields. An unknown field, a wrong type or a day name that
is not one of the seven is rejected with `422 validationFailed`, and `field` names the key, for
example `weekdayBar.weekendDays` or `weekdayBar.startOnMonday`. Nothing in the request is applied.

`startOnMonday` never changes which days are weekend. `weekendDays` is always returned in calendar
order, Sunday first.

The weekend colors start with the same values as the workday colors, so the bar looks the same
for all days until you change them.

```bash
# A Friday/Saturday weekend, shown in amber
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"weekdayBar": {"weekendDays": ["friday", "saturday"],
                      "weekendActiveColor": "#FFAA00",
                      "weekendInactiveColor": "#664400"}}'
```

<!-- only esp32 esp32-s3 -->
`dateWeekdayBar.show` is not the same as [`dateShowWeekday`](#date-text): the first shows the segment
bar in Date, the second puts a weekday name in front of the date *text*.
<!-- /only -->

---

## Sensor apps

<!-- only esp32 esp32-s3 -->
| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `useCelsius` | boolean | - | `true` | - | Show temperature in °C. `false` shows °F. Affects the Temperature app only. |
| `temperatureColor` | color or `null` | - | `null` | - | Temperature app text color. `null` = use `textColor`. |
| `humidityColor` | color or `null` | - | `null` | - | Humidity app text color. `null` = use `textColor`. |
| `batteryColor` | color or `null` | - | `null` | - | Battery app text color. `null` = use `textColor`. |
<!-- /only -->
<!-- only tc002 -->
`useCelsius`, `temperatureColor`, `humidityColor` and `batteryColor` are in the settings, but
have no effect: the clock has no temperature, humidity or battery app.
<!-- /only -->

---

## Sound

The clock has one master volume and <!-- only esp32 -->two<!-- /only --><!-- only esp32-s3 tc002 -->three<!-- /only --> groups. Who plays a sound decides its group:

<!-- only esp32-s3 tc002 -->
- **radio**: every internet radio station.
<!-- /only -->
- **app**: everything a script plays.
<!-- only esp32 esp32-s3 -->
- **alert**: notification sounds, `POST /api/v1/audio/play` and `cmd/audio/play`.
<!-- /only -->
<!-- only tc002 -->
- **alert**: notification sounds, `POST /api/v1/audio/play` and `cmd/audio/play`,
  `POST /api/v1/audio/clip`, the boot sound and the answer of Home Assistant Voice.
<!-- /only -->

What you hear is `volume × group volume / 100`. With
`volume` at 50 and `alertVolume` at 60, an alert plays at 30. A change applies at once, also to
sounds that are playing. `0` is silence, and `volume: 0` silences every sound.

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `volume` | integer | 0–100 | <!-- only esp32 esp32-s3 -->`60`<!-- /only --><!-- only tc002 -->`90`<!-- /only --> | % | The master volume. Every group is a share of it.<!-- only tc002 --> The knob sets it.<!-- /only --> |
<!-- only esp32-s3 tc002 -->
| `radioVolume` | integer | 0–100 | `80` | % | The radio group: internet radio. |
<!-- /only -->
| `appVolume` | integer | 0–100 | `100` | % | The app group: everything a script plays. |
| `alertVolume` | integer | 0–100 | `100` | % | The alert group: notification sounds and the other alerts listed above. |
<!-- only tc002 -->
| `bootSound` | boolean | - | `true` | - | The sound when the clock starts. It plays at the alert volume. |
| `musicSource` | string | `auto`, `playback`, `microphone` | `auto` | — | What music visualizers and pitch react to. `playback` = what the speaker plays, `microphone` = what the microphone hears, `auto` = the speaker while something plays, otherwise the microphone. |
<!-- /only -->

<!-- only esp32 -->
The web UI shows the three volumes as the **Mixer** on the Audio tab.
<!-- /only -->
<!-- only esp32-s3 -->
The web UI shows the four volumes as the **Mixer** on the Audio tab. The **Radio** slider shows
only when the clock can play radio: the I2S pins are set and the board has PSRAM.
<!-- /only -->
<!-- only tc002 -->
The web UI shows the four volumes as the **Mixer** on the Audio tab.
<!-- /only -->

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"volume":50,"alertVolume":40}'
```

See [Sound](../guides/sounds.md#volume).

---

## Buttons

| Key | Type | Range | Default | Units | Meaning |
|---|---|---|---|---|---|
| `blockNavigation` | boolean | - | `false` | - | With `true`, the left and right buttons do not change apps, a double press on select does not switch the display, and holding select does not open the [menu](../guides/device-controls.md#the-menu).<!-- only tc002 --> The [knob](../guides/device-controls.md#the-knob) sets neither brightness nor volume and does not start Home Assistant Voice.<!-- /only --> Presses still reach scripts, `buttonCallback` and MQTT<!-- only tc002 -->, and knob turns still reach `buttonCallback` and MQTT<!-- /only -->. |

---

## Value types

### Colors

Colors come back as uppercase `"#RRGGBB"`. When you send a color, you can use any form listed
under [Conventions → Colors](conventions.md#colors): `"RRGGBB"`, `"#RGB"`, `[r, g, b]`,
`["HSV", h, s, v]`, or a number.

There are two kinds of color keys:

* **Required colors** – `textColor`, `calendarHeaderColor`, `calendarTextColor`,
  `calendarBodyColor` and the four colors in `weekdayBar`. `null` is rejected with `422`.
* **Optional colors** – `timeColor`, `dateColor`, `temperatureColor`, `humidityColor`,
  `batteryColor` (`null` means *use `textColor`*) and `colorCorrection`, `colorTint` (`null` means
  *off*). All seven default to `null`.

Black and white are real colors, not "unset". `"#000000"` on an optional text color is stored as
black and read back as `"#000000"`, not `null`. `"#FFFFFF"` on `colorCorrection` or `colorTint` is
stored as white and looks the same as *off*, because white changes nothing. Only JSON `null` means
*use `textColor`* or *off*.

```bash
# Give the clock its own colour, and let the date use textColor again
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H "Content-Type: application/json" \
  -d '{"timeColor": "#00AAFF", "dateColor": null}'
```

### Name strings

`transitionEffect`, `transitionDirection`, `timeSeparatorMode`, `dateOrder`, `dateSeparator` and
`dateYearMode` take **names**, never numbers. Upper and lower case do not matter – `"pulse"`,
`"Pulse"` and `"PULSE"` are the same. `GET /api/v1/capabilities` lists the names for
`transitionEffect`; the others are in the tables on this page. `GET /api/v1/settings` always returns
that spelling. A number gets the same error as a wrong name:

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

The day names in `weekdayBar.weekendDays` are different: they must be lowercase, exactly as listed.

### Numbers

Integer keys reject decimals and booleans – `{"brightness": 120.5}` and `{"brightness": true}` both
fail with `"must be an integer"`. `gamma` is the only key that takes a decimal number. It also
accepts a whole number, so `2` is a valid gamma.

An upper limit of `2147483647` just means "no real limit".

---

## Validation errors

Every problem is answered with `422`, the standard error body and the wrong key in `field`. Unknown
keys are rejected too, so a typo fails instead of being silently ignored – and nothing in the
request is applied. A request that is not valid JSON gets `400 invalidJson`.

All messages this route can return:
[Errors – PATCH /api/v1/settings](errors.md#patch-apiv1settings).

## Related

- [Conventions](conventions.md) – color formats and rules for the whole API
- [Visual reference](visuals.md) – transitions, effects and how the display colors look
- [System configuration](system.md) – Wi-Fi, MQTT, time zone, display wiring and other device keys
- [Brightness & sensors](../guides/brightness.md)
- [Sound](../guides/sounds.md)
