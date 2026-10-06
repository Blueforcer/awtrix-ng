# Buttons<!-- only tc002 -->, knob<!-- /only --> & clock

<!-- only esp32 esp32-s3 -->
This page shows how to use the buttons on the clock and its menu, and what the built-in clock
shows.
<!-- /only -->
<!-- only tc002 -->
This page shows how to use the buttons, the knob and the menu on the clock, and what the Status
app and the built-in clock show.
<!-- /only -->

## How it behaves

Without any setup, left and right show the previous or next app in the
[rotation](display.md#apps-take-turns), select dismisses the notification shown, and a quick double
press of select switches the display off or on. A script that is shown gets each press first, and
a press it keeps does nothing else
([React to buttons](scripting/time-buttons-sensors.md#button-events)). Holding select for half a
second opens [the menu](#the-menu), where you start a script<!-- only esp32-s3 tc002 --> or a radio
station<!-- /only -->.
<!-- only tc002 -->
The knob sets the brightness and the volume, unless the app shown uses it for itself.
<!-- /only -->
While **Block buttons** under **Display → App rotation** is on, the buttons switch neither apps
nor the display, and the menu stays closed.

## The buttons

<!-- only esp32 -->
The clock has three buttons: left, select (middle) and right. On a DIY build they sit on the
configured pins. They control the apps and are passed to scripts. You can also send each press to a
web address with [`buttonCallback`](../reference/system.md#buttons).
<!-- /only -->
<!-- only esp32-s3 -->
The clock has three buttons on the configured pins: left, select (middle) and right. They control
the apps and are passed to scripts. You can also send each press to a web address with
[`buttonCallback`](../reference/system.md#buttons).
<!-- /only -->
<!-- only tc002 -->
The clock has three buttons on top: left, select (middle) and right. They control the apps and are
passed to scripts. Each press, and the knob, is also sent to
[`buttonCallback`](../reference/system.md#buttons).
<!-- /only -->

What the buttons do:

| Button | Does |
|---|---|
| Left, right | Show the previous or next app |
| Select, once | Dismiss the notification shown |
| Select, twice quickly | Switch the display off or on |
| Select, held for half a second | Open [the menu](#the-menu), or end a script started from it |

## The menu

Hold select for half a second and the menu opens over the current app. It lists what this clock can
start<!-- only esp32-s3 tc002 -->, each list<!-- /only --> in alphabetical order:

- **Scripts**: the scripts marked [`@ondemand`](scripting/several-apps.md#start-from-the-device-menu), such as
  games. They are installed but never take part in the rotation.
<!-- only esp32-s3 -->
- **Radio**: your [stations](radio.md), when the clock plays radio and has stations saved.
<!-- /only -->
<!-- only tc002 -->
- **Radio**: your [stations](radio.md), once you have saved some.
<!-- /only -->

In the menu:

1. **Left** and **right** move through the entries. Hold them to move on quickly.
2. **Press select** to open an entry.
3. **Hold select** for half a second to close the menu. It also closes after 20 seconds without a press.

**Starting a script.** Pick it under **Scripts**. It starts from the beginning and has the display
to itself: the rotation stops, and left and right do not switch apps. Hold select for half a second
to end it. The rotation continues where it was, and the next start begins from scratch. Settings
and anything the script saved in its store stay.

<!-- only esp32-s3 tc002 -->
**Playing the radio.** Pick a station under **Radio**. It plays at once and the menu stays open, so
you can try the next one. The station that plays is green. While one plays, the first entry is
**Stop**.

<!-- /only -->
<!-- only esp32 esp32-s3 -->
The menu shows one entry at a time. A bar on the bottom row shows where you are in the list.
<!-- /only -->
<!-- only tc002 -->
The menu shows two entries at once, the selected one bright.
<!-- /only -->

<!-- only tc002 -->
## The knob

The knob sets brightness and volume without the web UI. An app shown can use the
knob for itself instead, for example to set the volume of what it plays. See
[The knob](scripting/time-buttons-sensors.md#the-knob). Every push and turn also goes to
[`buttonCallback`](../reference/system.md#buttons) and [MQTT](../reference/mqtt.md#state-topics).

1. Turn or press the knob. Two bars appear over the current app. They open on the row you changed
   last, on brightness after a restart.
2. **Turn** to change the row marked with a line, 5 % per click.
3. **Press** briefly to switch between brightness and volume.
4. Leave the knob alone for one and a half seconds. The bars close and the app comes back.

| Row | What it sets |
|---|---|
| Top, amber | [`brightness`](../reference/settings.md#brightness), from 5 % to 100 %. While a mood light is on, it sets the mood light's brightness instead. The icon is a moon up to 25 % and a sun that grows above that. |
| Bottom, cyan | The master volume, [`volume`](sounds.md#volume), from 0 to 100 %. It sets how loud every sound plays: alerts, apps and the radio. An X next to the speaker means silent, at 0. |

**Hold** the knob for half a second to talk to Home Assistant, when
[Home Assistant Voice](voice.md) is set up. Otherwise holding does nothing.

The values are saved like any other setting, so the web UI and Home Assistant show them at once.
The three buttons on top keep their usual jobs.

While **Block buttons** is on, the knob opens no bars and does not start Home Assistant Voice. It
only reports to `buttonCallback` and MQTT, so you can use it for your own automation.

## The Status app

The Status app shows how the clock is doing:

| Where | What it shows |
|---|---|
| Top left | The battery. The fill and the percentage are green, amber below 40 % and red below 20 %. A yellow bolt appears while the clock is on USB power. Below [`lowBatteryThreshold`](power.md) the fill blinks, unless USB power is connected. |
| Top right | The Wi-Fi signal in four bars. |
| Bottom | The clock's address. The four numbers alternate between cyan and white instead of being separated by dots, so every address fits on the display: cyan `192`, white `168`, cyan `178`, white `100` is `192.168.178.100`. |

While the clock joins its network, the bottom line reads `CONNECTING`. Without a network it reads
`NO WIFI`, and the four bars turn dark red.

The Status app is in the [app list](../getting-started/web-ui.md#apps) like every other app, so you
can move it or switch it off there. The battery level is also in the device state and in
[Home Assistant](home-assistant.md).

## The clock

The clock has five faces. Pick one under **Clock face** in the clock settings of
the web UI, or with the [`clockFace`](../reference/settings.md#tc002-clock-faces) setting:

| Face | Shows |
|---|---|
| Calendar sheet | a tear-off sheet with the month and the day, the time and the weekday bar |
| Ring calendar | the sheet on two rings with the day, the time and the weekday bar |
| Split-flap | the sheet with the weekday and the day, the time on flip cards |
| Month sheet | the month as a grid of dots with today in blue, and the time |
| Big clock | the time across the display, the weekday and the date below it |

The date is on the sheet and under the big clock, so there is no separate Date app. Seconds
show on the big clock only.

When the clock appears, and at midnight, the old sheet is torn off. To turn this off, switch off
**Calendar animation** in the clock settings.
<!-- /only -->

<!-- only esp32 esp32-s3 -->
## The clock

The **Time** app shows the clock. Choose its layout in
[Settings › Clock app](../reference/settings.md#clock-app). The **Date** app shows the date.

Which sensor and battery apps are available depends on your hardware. See
[Built-in apps](pushed-apps.md#built-in-apps). See [Power & battery](power.md) for the battery level
and for switching the display off.
<!-- /only -->

## Good to know

- **The menu does not open while the display is off or while Block buttons is on.** Switch the
  display on, or switch **Block buttons** off. Holding select still ends a running script.
<!-- only tc002 -->
- **The first turn or press of the knob only opens the bars.** Turn again to change the value.
- **The knob does nothing while the display is off.** Switch the display on with a double press of
  select first.
<!-- /only -->

## Details

- [Settings → Buttons](../reference/settings.md#buttons): what **Block buttons**
  (`blockNavigation`) stops, and what still works
- [System configuration → Buttons](../reference/system.md#buttons): `buttonCallback`, which
  reports every press to a web address, and `swapButtons`
<!-- only esp32 esp32-s3 -->
- [Settings → Clock app](../reference/settings.md#clock-app): the layouts of the Time app
<!-- /only -->
<!-- only tc002 -->
- [Settings → Clock faces](../reference/settings.md#tc002-clock-faces): every clock face and its
  settings
<!-- /only -->

## Related

- [Scripting guide](scripting/index.md): button handlers in scripts
<!-- only tc002 -->
- [Home Assistant Voice](voice.md): talk to Assist with the knob
- [Volume](sounds.md#volume): the master volume the knob sets, and the three groups
<!-- /only -->
- [Power & battery](power.md): battery level and display blanking
