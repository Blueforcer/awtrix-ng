# Time, buttons and sensors

Animate with the time, show the clock, do something later, and react to the buttons<!-- only esp32 esp32-s3 --> and the sensors<!-- /only --><!-- only tc002 -->, the knob, the battery and a gamepad<!-- /only -->.

## What you get

<!-- panel motion=5 -->
```berry
class Countdown
  var start, steps

  def init()
    self.start = 0
    self.steps = ["3", "2", "1", "GO"]   # built once
  end

  def on_show()
    self.start = now_ms()
  end

  def on_button(btn)
    if btn != "select" return false end
    self.start = now_ms()
    return true
  end

  def draw()
    clear()
    var s = self.steps[min((now_ms() - self.start) / 1000, 3)]
    text((width() - text_ink_width(s)) / 2, 6, s, 0x00FF00)
  end
end

return Countdown()
```

A countdown from 3 to GO, one step a second. It starts again each time the app appears, and when
you press select.

## How it behaves

- **`draw()` paints about 40 times a second** while your app is shown. **`loop()` runs about once
  a second**, also while another app is shown.
- **`now_ms()` counts milliseconds since the clock started**, at a steady pace however fast the
  frames come. `on_show()` runs each time your app appears: keep `now_ms()` in a member there,
  and `now_ms() - self.start` counts from the moment your app appeared. Your animation then
  starts from the beginning on every turn.
- **The clock calls, `hour()`, `minute()` and the others, answer `-1` until the device knows the
  time.** Right after a boot, that is the case in `init()` and `setup()`.
- **A timer calls your function later**, also while another app is shown.
- **The buttons go to the app shown.** Return `true` from `on_button()` to keep a press. Otherwise
  the button does what it always does: left and right switch apps.

[The lifecycle](index.md#the-lifecycle) lists every hook and when it runs.

## Show the time {#time}

<a id="time-numbers-and-timers"></a>

`hour()`, `minute()` and the other clock calls read the time of the device. This app shows it as
`9:05`:

```berry
class Clock
  var label

  def init()
    self.label = "--:--"
  end

  def loop()
    if hour() >= 0                    # -1 until the device knows the time
      self.label = format("%d:%02d", hour(), minute())   # "9:05"
    end
  end

  def draw()
    clear()
    text(4, 6, self.label, 0xFFFFFF)
  end
end

return Clock()
```

`loop()` builds the text once a second, and `draw()` only paints it.

**Right after a boot, `init()` and `setup()` have no clock yet.** The device has not fetched the
time, so every clock call except `now_ms()` answers `-1` there. Give members their starting values
in `init()` and read the time in `loop()` or `draw()`. If `setup()` needs the date, check first:

```berry
  def setup()
    if hour() >= 0
      # the clock is up: this is a save, not a boot
    end
  end
```

**`epoch_ms()`** is the date and time in milliseconds since 1 January 1970, in UTC. It also answers
`-1` until the device has the time. It tells you how old a timestamp from elsewhere is:

```berry
var age = (epoch_ms() - num(self.updated_at)) / 1000    # seconds old
```

## Animate with the time

An animation that reads the time moves at the same speed, however fast the frames come:

<!-- panel motion=4 -->
```berry
class Sweep
  var start

  def init()
    self.start = 0
  end

  def on_show()
    self.start = now_ms()
  end

  def draw()
    clear()
    var ms = (now_ms() - self.start) % 2000        # 0 to 1999, again every two seconds
    rect_fill(0, height() - 1, ms * width() / 2000, 1, 0x2266FF)
  end
end

return Sweep()
```

A bar runs along the bottom row every two seconds. It starts at the left edge each time the app
appears.

`ms * width() / 2000` multiplies first. Berry divides whole numbers without a remainder, so
`ms / 2000` is `0` for every `ms` below 2000, and `ms / 2000 * width()` would never draw a bar.

**In step with the clock:** `epoch_ms() % 1000` is the position inside the current second, and
`% 60000` inside the minute. This bar refills on every tick of the clock:

```berry
  def draw()
    clear()
    rect_fill(0, height() - 1, (epoch_ms() % 1000) * width() / 1000, 1, 0x2266FF)
  end
```

[How the display works](../display.md#when-text-moves) moves a word the same way.

## Do something later {#timers}

`timer.after(ms, callback)` runs your function once, after a delay. `timer.every(ms, callback)`
runs it again and again. Both return at once, so the display keeps running. No `import` is
needed.

This app shows **GO** for three seconds when you press the select (middle) button:

```berry
class Reminder
  var message, pending

  def init()
    self.message = "READY"
  end

  def reset()
    self.message = "READY"
    self.pending = nil
  end

  def on_button(btn)
    if btn != "select" return false end
    timer.cancel(self.pending)
    self.message = "GO"
    self.pending = timer.after(3000, / -> self.reset())
    if self.pending == nil self.reset() end
    return true
  end

  def draw()
    text(0, 6, self.message)
  end
end
return Reminder()
```

The callback takes no arguments. `/ -> self.reset()` is a short [callback](index.md#callbacks) that
calls a method of your app. Change values or start actions in callbacks, and draw only in
`draw()`. Start timers in `setup()`, a button handler or another callback, not on every frame in
`draw()`.

Timers keep running while another app is shown and while the display is off. They do not survive
a restart of the device: create the ones you need again in `setup()`.

**Every minute, without a timer:** `loop()` runs about once a second, so counting its calls is the
simplest way. Keep the counter in a member (`var ticks`, set to `0` in `init()`):

```berry
  def loop()
    if self.ticks <= 0
      self.ticks = 60    # loop() runs about once a second, so this is roughly a minute
      # ... do the thing ...
    end
    self.ticks -= 1
  end
```

## Round, limit and convert numbers {#numbers}

`round()`, `clamp()` and `num()` turn the values you get into the values you show:

```berry
var y = 7 - clamp(round(pct * 7 / 100.0), 0, 7)   # a 0–100 % value as a bar height
self.label = str(round(self.watt)) + "W"           # 5945.4 becomes "5945W"
```

`round()` without `digits` returns an `int`. Use it before `str()`, which otherwise prints every
decimal, or use [`format()`](index.md#just-enough-berry): `format("%.1f", v)`.

**A value that arrives as text is text.** An MQTT payload, or a value you cut out of an HTTP
answer, is a string. That is fine for showing it. To compare, calculate or store it, convert it
with `num()`:

```berry
mqtt.subscribe("pv/watt", def (topic, payload)
  self.watt = num(payload)      # "5945" becomes 5945
end)
```

`num()` turns `"5945"` and `"876.6"` into numbers. Text that holds no number, such as `"876,6"`
or `"876.6 W"`, gives `nil`, so a formatting problem shows as your "no data" state instead of a
wrong number.

## React to buttons {#button-events}

<a id="buttons-and-sensors"></a><a id="buttons-knob-battery-and-gamepad"></a><a id="buttons-sensors-and-gamepad"></a>

For most apps, [`on_button(btn)`](index.md#the-lifecycle) is enough, as in
[What you get](#what-you-get). To react to holding and releasing a button, add
`on_button_event(btn, event)`. `btn` is `"left"`, `"select"` or `"right"`. Return `true` for the
`"press"` event to take the button: the rest of that press then comes to your app.

This app counts right-button presses, and keeps counting while you hold the button:

```berry
class Counter
  var count, label

  def init()
    self.count = 0
    self.label = "0"
  end

  def on_button_event(btn, event)
    if btn != "right" return false end
    if event == "press" || event == "repeat"
      self.count += 1
      self.label = str(self.count)
    end
    return true
  end

  def draw()
    text(0, 6, self.label)
  end
end
return Counter()
```

`"long"` arrives after you held the button for 500 ms, and `"repeat"` every 150 ms after it, for
left and right only. Holding select always belongs to the clock: after half a second it opens
[the menu](../device-controls.md#the-menu).

Each button your app takes gives it its full time again, so the clock does not switch to the next
app while someone is using yours.

<!-- only tc002 -->
## React to the knob {#the-knob}

An app shown can take the knob with `on_knob(event)`. `event` is `"left"` or `"right"` for one
detent, `"press"`, `"long"` or `"release"`. Without `on_knob()`, or when it does not return
`true`, the knob sets brightness and volume as usual.

<!-- panel -->
```berry
class Dial
  var level, label

  def init()
    self.level = 50
    self.label = "50"
  end

  def on_knob(event)
    if event == "left" self.level = max(self.level - 5, 0)
    elif event == "right" self.level = min(self.level + 5, 100)
    elif event == "press" self.level = 50
    else return true
    end
    self.label = str(self.level)
    return true
  end

  def draw()
    text(1, 6, self.label)
    rect_fill(0, height() - 1, self.level * width() / 100, 1, 0x00AAFF)
  end
end
return Dial()
```

Turning sets a level from 0 to 100, and a press sets it back to 50. The bar in the bottom row
shows it.

A press you take is yours until release: the clock does not switch rows and does not start Home
Assistant Voice. Each part of the knob you take gives your app its full time again.
<!-- /only -->

<!-- only esp32 esp32-s3 -->
## Read the sensors {#reading-the-sensors}

`sensor.temperature()`, `sensor.humidity()` and the other sensor calls give the readings the
built-in Temperature and Humidity apps show. **Each one answers `nil` when the board has no such
sensor**, not `0`. Check before you use the value:

```berry
class Inside
  var label

  def init()
    self.label = "--"
  end

  def loop()
    var t = sensor.temperature()
    self.label = t == nil ? "--" : str(round(t)) + "°"
  end

  def draw()
    clear()
    text(1, 6, self.label, 0xFF8800)
  end
end

return Inside()
```

The readings update on their own schedule, not every frame, so reading them once a second in
`loop()` misses nothing.

Temperature is always Celsius. `settings.get("useCelsius")` tells you what the user wants to see.
Convert yourself: `f = c * 9 / 5 + 32`.
<!-- /only -->
<!-- only tc002 -->
## Read the battery {#reading-the-sensors}

`sensor.battery()` answers the charge in percent. **It answers `nil` when the clock reports no
battery**, not `0`. Check before you use the value:

```berry
class Battery
  var label

  def init()
    self.label = "--"
  end

  def loop()
    var b = sensor.battery()
    self.label = b == nil ? "--" : str(b) + "%"
  end

  def draw()
    clear()
    text(1, 6, self.label, 0x00CC44)
  end
end

return Battery()
```

The readings update on their own schedule, not every frame, so reading them once a second in
`loop()` misses nothing.

For a microphone visualizer, use [`music.level()` and `music.bands()`](sound.md#music). Choose
Automatic, Playback or Microphone under **System → Audio** in the web UI. To hear which note is
sung or played, use [`music.pitch()`](sound.md#hearing-a-note).

## Play with a gamepad {#gamepad}

`import gamepad` lets a game read a Bluetooth gamepad, or a phone used as the gamepad (see
[Gamepad](../gamepad.md#your-phone-as-the-gamepad)). Pair the gamepad under **System → Gamepad**.
Every script reads the same one. Check `gamepad.ready()`, show what `gamepad.state()` says while it
is not ready, and claim the gamepad with `gamepad.claim(self)` while your game is played, so one
button press starts only one game. The [Gamepad](../gamepad.md#in-your-own-scripts) guide lists
every call, with an example.

## Talk to Bluetooth LE devices {#bluetooth-le}

`import ble` reads Bluetooth LE sensors and offers values of its own to phones and other devices.
A script meant for clocks without Bluetooth imports it inside
[`try`](../ble.md#scripts-for-every-model) and shows an "unavailable" state when the import fails.
Start scans and connections once, in `setup()`, `loop()` or a callback, and never in `draw()`.
Stop what you no longer need. [Bluetooth LE in scripts](../ble.md) lists every call, callback,
filter, limit and error, with a [heart-rate example](../../examples/heart-rate.ax).
<!-- /only -->

## Good to know

- **Whole numbers divide without a remainder:** `7 / 2` is `3`, and `50 / 100` is `0`. Multiply
  before you divide: `ms * width() / 2000`, not `ms / 2000 * width()`.
- **The time is `-1` right after a boot.** Read it in `loop()` or `draw()`, and check
  `hour() >= 0` before you use it in `setup()`.
- **A value from MQTT is text.** Convert it with `num()` before you compare or calculate.
- **Do not name a variable like a built-in call.** A variable called `minute` hides `minute()`, and
  the next call to it fails. Pick another name, such as `var m = minute()`.
- **A held button never passes to the next app**, and no `"release"` follows when your app leaves.
  Reset a "pressed" state in `on_hide()` as well as on `"release"`.

## Details

### Time calls

| Call | Answer |
|---|---|
| `hour()` | 0–23 |
| `minute()` | 0–59 |
| `second()` | 0–59 |
| `weekday()` | 0–6, 0 = Sunday |
| `day()` | 1–31 |
| `month()` | 1–12 |
| `year()` | for example 2026 |
| `now_ms()` | milliseconds since boot |
| `epoch_ms()` | milliseconds since 1 January 1970 UTC |

- The time calls work in every hook and callback.
- Until the device knows the time, every call except `now_ms()` answers `-1`. `width()`,
  `height()` and the text measurements work normally then.
- `now_ms()` works everywhere, `setup()` included, and starts again at `0` after every reboot.
- `epoch_ms()` is in UTC. For the local hour, use `hour()`.

### Number calls

| Call | Does |
|---|---|
| `num(v, dflt?)` | turns a value into an `int` or `real`, else returns `dflt` |
| `round(v, digits?)` | rounds half away from zero: `round(2.5)` → `3`, `round(876.64, 1)` → `876.6` |
| `clamp(v, lo, hi)` | keeps a value inside a range |
| `min(a, b)`, `max(a, b)` | the smaller or the larger of two values |

- `round()` without `digits` returns an `int`, with `digits` a `real`.
- `clamp`, `min` and `max` compare with `<`, so they also work on strings.
- `num(v)` accepts numbers, unchanged, and text holding a number: `"5945"`, `"876.6"`, and the
  quoted form `"\"876.6\""` some brokers send. Everything else returns `nil`, or your fallback:
  `num(payload, 0)`.

### Timer calls

| Call | What it does |
|---|---|
| `timer.after(ms, callback)` | Calls your function once after the delay. |
| `timer.every(ms, callback)` | Calls your function repeatedly, starting after the first interval. |
| `timer.cancel(id)` | Stops one of your app's timers. Returns `true` if it was still waiting. |

- The delay is a whole number of milliseconds, from **25 to 86,400,000** (one day).
- Starting a timer returns its ID, or `nil` if the arguments are invalid or no timer is free.
- Each app can have **8 timers**, with **32 across all apps**.
- `timer.cancel(nil)` and canceling a finished timer return `false`, without an error.
- Timers run while another app is shown and while the display is off.
- Deactivating an app stops its timers. When you activate it again, an overdue timer runs once,
  and repeating timers continue from there.
- When the device is busy a timer can run late. Missed repeats are not made up in a burst.
- Changing the clock does not affect timers.
- Saving, deleting or restarting an app clears its timers. So does a script error.
- Timers do not survive a device restart.

### Every button event

`btn` is `"left"`, `"select"` or `"right"`, taking the device's button swap and rotation settings
into account.

| Event | When it arrives |
|---|---|
| `"press"` | Once when the button is pressed. Return `true` to take this press. |
| `"long"` | Once after holding a button you took for 500 ms. Left and right only. |
| `"repeat"` | Every 150 ms after `"long"`, while still held. Left and right only. |
| `"release"` | Once when that button is released. |

- Only `true` returned for `"press"` takes the button. Other return values are ignored. If you do
  not take it, AWTRIX calls your `on_button(btn)`, and then does the usual navigation if that does
  not take it either.
- Taking the select button also turns off its dismiss-notification and double-press actions for
  that press.
- Holding select always belongs to the clock. An app that took the press gets no `"release"` for
  it then, and never `"long"` or `"repeat"` for select. So a `"release"` of select is always a
  short press.
- Events go only to the app that took the press, and only while it is shown. Switching apps,
  deactivating, saving or deleting the app ends it. A held button never passes to the next app,
  and no `"release"` follows.
- Keep handlers short and draw in `draw()`.

<!-- only tc002 -->
### Every knob event

| Event | When it arrives |
|---|---|
| `"left"` / `"right"` | One detent turned to the left or right. Return `true` to take it. |
| `"press"` | Once when the knob is pressed. Return `true` to take this press. |
| `"long"` | Once after holding a press you took for 500 ms. |
| `"release"` | Once when that press ends. |

- While the clock shows its own brightness and volume control, or Home Assistant Voice is
  listening, the knob stays with the clock.
- Every turn and press still goes to [`buttonCallback`](../../reference/system.md#buttons) and
  MQTT.
<!-- /only -->

### Sensor calls

<!-- only esp32 esp32-s3 -->
| Call | Answer |
|---|---|
| `sensor.temperature()` | °C |
| `sensor.humidity()` | relative humidity in % |
| `sensor.pressure()` | hPa |
| `sensor.light()` | ambient brightness |
| `sensor.battery()` | charge in %, a whole number |
| `sensor.battery_volts()` | battery voltage |

Each one answers `nil` when the board has no such sensor.
<!-- /only -->
<!-- only tc002 -->
| Call | Answer |
|---|---|
| `sensor.battery()` | charge in %, a whole number |
| `sensor.battery_volts()` | battery voltage |

Each one answers `nil` when the clock reports no battery.
<!-- /only -->

Every limit a script runs under is in [Limits](../../reference/limits.md#scripting).

## Related

- [How the display works](../display.md): frames, and how a word moves without `scroll_text()`
- [The lifecycle](index.md#the-lifecycle): when each hook runs
- [Buttons<!-- only tc002 -->, knob<!-- /only --> & clock](../device-controls.md): what the buttons do when your app does not take them
- [Tutorial 2: Give it a memory](../../tutorials/state-and-time.md): an app that changes by itself, step by step
