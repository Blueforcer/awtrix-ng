# Several apps together {#more-than-one-app}

Decide when your app takes its turn, run a script in the background, and let your apps share
values and code.

## What you get

```berry
class Bin
  def should_show()
    return weekday() == 2             # Tuesday, 0 is Sunday
  end

  def duration()
    return 15000                      # 15 s instead of the usual time
  end

  def draw()
    clear()
    text(1, 6, "Bin out!", 0x00A0FF)
  end
end

return Bin()
```

On Tuesdays the reminder stays 15 seconds in every round. On the other days the rotation goes
straight past it.

## How it behaves

- **When the rotation moves on to your app**, it asks `should_show()`. `false` skips your app for
  this round. Anything else, or no `should_show()` at all, shows it.
- **When your app appears**, `on_show()` runs, then `duration()` is asked. The milliseconds it
  returns are the time of this turn. Without a number, the turn lasts the device's app time, 7
  seconds unless the owner [changed it](../../reference/settings.md#app-rotation).
- **While your app is shown**, `draw()` paints about 40 times a second. When the time is up, the
  next app slides in, and `on_hide()` runs once your app has left.
- **All the time, shown or not**, `loop()` runs about once a second, and timers and callbacks run
  when they are due.
- **Your app stays longer than its time** while a `scroll_text()` with `{"repeat": 1}` has not run
  through once, and while [`rotation.pause()`](device.md#driving-the-rotation) holds the rotation.
  Each button press your app takes gives it its full time again. Without `repeat`, the next app
  comes after the usual time, even in the middle of a moving text.

What runs depends on the kind of app:

| Your app | `loop()`, timers and callbacks | A turn on the display |
|---|---|---|
| A normal app | run | every round |
| Returns `false` from [`should_show()`](#sitting-a-round-out) | run | skips that round |
| A [background script](#running-without-ever-being-shown) (`@headless true`) | run | never |
| Marked [`@ondemand`](#start-from-the-device-menu) | only while someone started it | only while started |
| Deactivated in the web UI | nothing runs | never |

A deactivated app stays installed and keeps its [store](storage.md#storage). The switch on its row
in the web UI starts it again. [How the display works](../display.md#apps-take-turns) explains the
rotation.

## Skip a turn {#sitting-a-round-out}

Some apps only sometimes have something to show: the bin goes out tomorrow, the door is open.
Return `false` from `should_show()`, and the rotation goes straight on to the next app. Your app
keeps its place in the rotation, and its `loop()`, timers and callbacks keep running.

```berry
class Door
  var open

  def init()
    self.open = false
  end

  def setup()
    mqtt.subscribe("home/door", def (topic, payload)
      self.open = payload == "open"
    end)
  end

  def should_show()
    return self.open                  # a turn only while the door is open
  end

  def draw()
    clear()
    text(1, 6, "DOOR", 0xFF3000)
  end
end

return Door()
```

Once shown, your app stays for the whole turn: `should_show()` is asked only when the rotation
moves on to it. On the **Apps** tab, an app that skipped this round has the label *skipped*.

## Stay longer {#setting-your-own-dwell-time}

`duration()` sets how long your app stays: return a number of milliseconds. It works like a
pushed app's `durationMs`.

```berry
class Slow
  def duration()
    return 15000                      # 15 s
  end

  def draw()
    clear()
    text(1, 6, "read me", 0xFFCC00)
  end
end

return Slow()
```

`duration()` is asked each time your app appears, so the value can change: long while a message is
up, normal otherwise. **Return `0` or nothing to use the device's app time.**

To stay until a moving text was read, pass `{"repeat": 1}` to `scroll_text()`, as
[Drawing](drawing.md#keep-the-app-until-the-text-was-read) shows. Your app then stays at least its
time, and longer while the text has not run through once.

## Background scripts {#running-without-ever-being-shown}

Some scripts never need the display: an MQTT listener that only raises notifications, or a script
that fetches data and publishes it to [`shared`](#talking-to-other-apps) for other apps to show.
Such a **background script** is marked in the header:

```berry
# @name Doorbell
# @headless true

class Doorbell
  def setup()
    mqtt.subscribe("home/doorbell", def (t, p) notify({"text": "Door", "sound": "ding"}) end)
  end
end

return Doorbell()
```

`@headless true` means **this script does not draw**. It runs like any other app, but never gets
a turn on the display. So `draw()`, `should_show()` and `duration()` are never called: leave them
out. The web UI lists it under **In the background**, and the switch on its row stops it and starts
it again. [Recipe: Background doorbell](../../tutorials/recipe-doorbell.md) builds this app step by
step.

A background script is the usual way to
[feed several apps from one source](#choose-how-your-apps-work-together). Keep it active even when
none of its display apps is shown.

## Start from the device menu {#start-from-the-device-menu}

A game or a tool you only want now and then does not belong in the rotation. Mark it `@ondemand`
in the header:

```berry
# @name     Dot
# @ondemand

class Dot
  var x

  def init()
    self.x = 0
  end

  def on_button(btn)
    if btn == "left"
      self.x = (self.x + width() - 1) % width()
    elif btn == "right"
      self.x = (self.x + 1) % width()
    end
    return true
  end

  def draw()
    clear()
    pixel(self.x, height() / 2, 0x00FF00)
  end
end

return Dot()
```

`@ondemand` keeps the script out of the rotation. It is installed, but does not run until someone
starts it: hold select on the clock for half a second, choose **Scripts** and then the script (see
[the menu](../device-controls.md#the-menu)). **▶ Start** on the **Apps** tab starts it too.

A started script has the display to itself:

- It starts from scratch: `init()` and `setup()` run, and nothing is left from the last time.
  Settings and the [store](storage.md#storage) stay.
- The rotation stops, and left and right go to your app only. Handle them in
  [`on_button()` or `on_button_event()`](time-buttons-sensors.md#button-events). Unhandled presses
  do nothing.
- Holding select for half a second ends it. `on_hide()` runs one last time, so save the player's
  progress there. Then the script is unloaded and frees its memory.
- `rotation.close()` ends it from inside, for example from a **Quit** entry in its own menu.

## Choose how your apps work together

| What you need | Use |
|---|---|
| One app shows its own reading | Fetch in that app and keep the result in a member. |
| Several apps show values from the same device or API | One [background script](#running-without-ever-being-shown) fetches. [`shared`](#talking-to-other-apps) passes the results on. |
| Several apps use the same calculation or formatting | A [module](#sharing-code-between-scripts) holds the helper functions. |
| Several apps use the same setting, such as a city | [Module settings](#settings-several-apps-share) let the user change it once. |
| An app needs to remember a value after a reboot | [`store`](storage.md#storage) keeps it for that app. |

For example, one meter reader can supply separate power and voltage apps. Only the reader contacts
the meter. The display apps use the values it publishes. The
[complete example](../modbus.md#share-one-device-across-several-apps) shows all the pieces. The same
works for HTTP data and MQTT messages.

A module shares functions, not results: calling a fetch function from three apps still starts three
fetches. Put the polling in one place, and let each display app check `shared.age()` before it
shows a value. For a single app, a member is simpler and needs no extra script.

## Share values with other apps {#talking-to-other-apps}

`shared` hands values from one app to another. `store` is private and survives a reboot. `shared`
is public and lasts until the next reboot.

This background script, saved as `meter`, publishes the power reading it receives over MQTT:

```berry
# @headless true

class Meter
  def setup()
    mqtt.subscribe("home/meter/power", def (topic, payload)
      var watt = num(payload)
      if watt != nil shared.set("power", watt) end
    end)
  end
end

return Meter()
```

Any app reads it as `meter.power`:

```berry
class Power
  var label

  def init()
    self.label = "--"
  end

  def loop()
    var age = shared.age("meter.power")
    if age == nil || age > 600000       # never published, or 10 minutes old
      self.label = "--"
    else
      self.label = str(shared.get("meter.power")) + " W"
    end
  end

  def draw()
    clear()
    text(1, 6, self.label, 0xFFD000)
  end
end

return Power()
```

- **Writing** takes a plain key and files it under your install name: `shared.set("power", …)` in
  `meter` publishes `meter.power`. No app can overwrite another app's values.
- **Reading** takes the full name, `owner.key`. A bare name reads your own: `shared.get("power")`.
- **Values** are single numbers, booleans or strings.
- **Publish only after a successful update.** Writing an old value again resets its age and makes
  it look fresh.

**Nothing in `shared` survives a reboot.** Deleting or saving a script, which restarts it, also
removes the keys it published. Readers then get `nil` and show their fallback. A value that must
survive a power cut belongs in `store`: publish it again from `setup()`.

The web UI's **Scripts** tab lists every published value under **Shared**.

### Reading a value that stopped being true

A provider can stop updating without disappearing: its HTTP fetch fails quietly, or it stopped with
an error. Its last value stays. Values never expire by themselves, so the reader decides what is
too old. `shared.age()` answers how many milliseconds ago the value was written, or `nil` if it was
never published. The `Power` app above shows `--` once the reading is ten minutes old.

### Finding out what is published

```berry
for k : shared.keys()          # every key, as "owner.key"
  log(k + " = " + str(shared.get(k)))
end
for k : shared.keys("weather") # only what the app weather published
  log(k)
end
```

A dashboard app can find its inputs this way instead of hard-coding names.

## Share code between scripts {#sharing-code-between-scripts}

`shared` passes **values** to other apps. A **module** passes **code**: one file of helpers that any
script loads with `import`, instead of the same lines copied into four apps.

Use a module when two or more scripts need the same calculation, conversion or formatting. A module
does not share results. For live readings several apps show, see
[Choose how your apps work together](#choose-how-your-apps-work-together). A helper only one app
uses can stay in that app.

A module is a script file with `@module` in the header. It ends by returning what it offers,
usually a `module` object with functions:

```berry
# @module
# @desc  Formatting helpers

import string

var m = module("fmt")
m.pct  = def (v) return string.format("%d%%", v) end
m.temp = def (v) return string.format("%.1f°", v) end
return m
```

Save it as `fmt`, and any app can use it:

```berry
import fmt

class Battery
  var label
  def init() self.label = "--" end
  def loop()
    var value = sensor.battery()
    self.label = value == nil ? "--" : fmt.pct(value)
  end
  def draw()
    text(0, 6, self.label, rgb(0, 255, 0))
  end
end

return Battery()
```

Put the `import` line at the **top of the file**, outside the class. Every method can then use the
name.

- **The import name is the file name.** It must be letters, digits and `_`, not starting with a
  digit. For a different name, write it after `@module`: `# @module weather` in a file called
  `weather-lib` is imported as `weather`.
- **Saving a module updates every app that uses it.** Those apps restart with the new code.
- The **Scripts** tab lists modules in their own **Modules** section.

## Settings several apps share

If several apps need your city, a `@config city` in each means typing it in several places.
**A module can have settings.** Put the value in the module, and every app that imports it gets
the same value:

```berry
# @module  location
# @config  city text  "City"   default="Berlin"
# @config  tint color "Color"  default=#FF8800

var location = module("location")
location.city = store.get("city")
location.tint = store.get("tint")
return location
```

```berry
import location

class Greeter
  def draw()
    clear()
    text(1, 6, location.city, location.tint)
  end
end

return Greeter()
```

On the **Apps** tab the module's row has a **⚙** like an app with settings. Change the city there
once, and every app that imports `location` shows it.

- **The module has its own store.** `store.get("city")` above reads `location`'s store, never the
  app's. An app can keep its own `city` without conflict.
- **Read settings at the top of the module, not inside a function.** The top of the file runs when
  the module loads, as the module itself. A `store.get()` inside one of its functions reads the
  store of the **app that calls it**.
- **Values stay current.** Saving the module's settings restarts
  [every app that imports it](#sharing-code-between-scripts), so they redraw with the new values.

Everything else works as for an app: the same [types and options](storage.md#settings-the-user-can-change),
and deleting the module deletes its settings.

## Good to know

- **Show on the clock ignores `should_show()`**, and so do **Show** on the **Apps** tab and the
  API. To test a skip, let the rotation reach your app.
- **`scroll_text()` alone does not keep your app.** Pass `{"repeat": 1}`, or the next app comes
  after the usual time, even in the middle of the text.
- **`shared` is empty after a reboot.** Show a fallback such as `--` while `shared.get()` answers
  `nil`.
- **A module shares code, not results.** A fetch function called from three apps fetches three
  times. Fetch in one background script and pass the results on with `shared`.
- **Read a module's settings at the top of the module.** A `store.get()` inside one of its
  functions reads the store of the app that calls it.

## Details

### Skipping a turn

- **Only `false` skips.** No `should_show()`, one that returns nothing, and a broken app showing
  `ERR:` all keep their turn.
- **Asking for the app by name still shows it.** **Show** in the web UI, the MQTT `apps/switch`
  command and `PUT /api/v1/apps/active` ignore `should_show()`.
- **If every app skips,** the app already shown stays until another app wants a turn.

### The time of a turn

- `duration()` is asked each time your app appears, also when it is shown by name.
- A missing `duration()`, a number below 1 and a broken app all use the device's app time.
- `duration()` only sets how long your app stays. It does not decide whether your app appears
  (that is `should_show()`) or where it sits in the rotation.

### Background scripts and the device menu

- `GET /api/v1/apps` marks a background script `headless` and an `@ondemand` script `ondemand`.
  The web UI lists `@ondemand` scripts under **In the device menu**.
- `PUT /api/v1/apps/active` with its name starts an `@ondemand` script as well.
- `rotation.close()` lets the call it is in run to its end, then closes the script as if select
  had been held. It returns `true`. In any app that is not a started `@ondemand` script, it does
  nothing and returns `false`.
- Another app shown through the API, or `next` and `previous`, end a started script as well.
- `@ondemand` has no effect together with `@module` or `@headless true`.

### Every rule of `shared`

- Keys are 1–24 characters of `A–Z a–z 0–9 _ -`. They may not contain dots.
- Prefer a few named values such as `power` and `voltage` over a whole response. If you really need
  a structure, publish `json.dump(...)` and parse it only when it changes, never on every frame.
- `shared.set(key, nil)` deletes the key. Use it when a value stops being valid.
- `shared.set()` returns `false` when the write is refused: a bad key, a value that is not a single
  number, string or bool, or too little free memory. The old value stays.
- `shared.get(name, fallback)` answers `fallback` when the value is missing:
  `shared.get("meter.power", 0)`.

### Every rule of modules

- A name another module already uses is refused. So are the built-in names `json`, `math`,
  `string`, `modbus`, <!-- only tc002 -->`ble`, `gamepad`, `layout`, `oauth`, `crypto`, `tcp`, <!-- /only -->`global`, `gc`, `strict`, `os`, `sys`, `time`, `debug`,
  `introspect` and `solidify`.
- A module **must end with `return`**. Without it, the file installs with an error, like a broken
  app.
- Modules may import each other, in any order. A module that imports one you have not written yet
  shows an error until you save the missing file. Then it works by itself.
- If you delete a module, its apps show `ERR:` and an error in the app list until you put it back.
- A module is not an app. It never draws and has no `draw()`, `setup()` or `loop()`. The **Apps**
  tab shows only modules with settings and broken ones.
- Put HTTP or Modbus calls inside a function that an app calls from `setup()` or `loop()`, not at
  the top of a module: the callback needs a running app. When a background script calls such a
  helper, the values it publishes to `shared` belong to that background script.

## Related

- [How the display works](../display.md#apps-take-turns): the rotation
- [Drawing](drawing.md#keep-the-app-until-the-text-was-read): keep the app until its text was read
- [Controlling the device](device.md#driving-the-rotation): move the rotation from a script
- [Storage and settings](storage.md): `store` and `@config`
- [Recipe: Background doorbell](../../tutorials/recipe-doorbell.md): a background script, step by
  step
- [Modbus in scripts](../modbus.md#share-one-device-across-several-apps): one meter, several apps
