# Scripting guide

Write your own apps for AWTRIX: small programs that draw on the display, fetch data and react to
buttons.

!!! tip "New here?"
    [How the display works](../display.md) shows where things sit on the display and which text
    moves by itself.

## What you get {#your-first-script}

Your first script takes five steps in the web UI:

1. Open the web UI in your browser and go to the **Scripts** tab.
2. Press **+** to start a new app, and type `Hello` into the name field.
3. Replace the text in the editor with this:

    <!-- panel -->
    ```berry
    class Hello
      def draw()
        clear()
        text(1, 6, "hi", 0x00FF00)
      end
    end

    return Hello()
    ```

4. Press **Save** (or **Ctrl-S**).
5. Press **Show on the clock**. The display shows **hi** in green.

`Hello` is an app in the rotation from now on, and it comes back after a reboot. The name you
typed is its **install name**: AWTRIX saves the script under it and tells apps apart by it. See
[Install name](sharing.md#install-name).

!!! tip "Other ways in"
    - **Ready-made scripts:** the [AWTRIX Hub](https://awtrix.de/scripts) collects finished
      scripts from the community. Send one to your clock from its page. The web UI offers later
      versions as an update.
    - **Step by step:** the tutorials, starting with
      [Tutorial 1: Draw something](../../tutorials/first-draw.md), grow one app from a blank
      display to a live weather chart.
    - **Not a programmer?** [Build an app with AI](../ai-prompt.md) gives you a prompt for a
      chatbot. Describe the app in plain words and paste the script it writes into the web UI.

## How it behaves

- **A script is an app.** It takes its turn in the [rotation](../display.md#apps-take-turns) like
  a [pushed app](../pushed-apps.md). Unlike a pushed app, it makes its own content, fetches its
  own data and stays on AWTRIX after a reboot.
- **Your app is a class.** AWTRIX calls its methods, the [hooks](#the-lifecycle), at set moments.
  The file ends with `return YourClass()`, which hands AWTRIX the app to run.
- **`draw()` paints one frame**, about 40 times a second, only while your app is shown. Every
  frame starts black, so `draw()` paints everything each time. Only `draw()` can paint: drawing
  calls in other hooks do nothing.
- **`loop()` runs about once a second, also while your app is hidden.** Fetch, count and build
  your text there, and keep the result in a member such as `self.label`. `draw()` then only
  paints it.
- **`on_show()` runs each time your app appears**, before its first frame. Reset a scroll
  position or a counter there. `on_hide()` runs when your app has left.
- **An error stops only your app.** The display shows `ERR:` in red in its turn, and the
  **Scripts** tab shows the message next to the script. Fix the line it names and save again.
  See [`ERR:` on the display](troubleshooting.md#err-on-the-panel).

[How the display works](../display.md) explains where things sit and which text moves.

## Keep a value between frames

The members of your class keep their values from one call to the next. Declare them with `var`
at the top of the class, give them a starting value in `init()`, and change them in `loop()`:

<!-- panel motion=3 -->
```berry
class Seconds
  var count, label

  def init()
    self.count = 0
    self.label = "0"
  end

  def loop()
    self.count += 1                   # about once a second
    self.label = str(self.count)      # build the text here ...
  end

  def draw()
    clear()
    text(1, 6, self.label, 0xFFFFFF)  # ... and only paint it here
  end
end

return Seconds()
```

The number goes up once a second. `draw()` paints the same text about 40 times in that second.
[Keeping scripts small](../../tutorials/going-easy-on-memory.md) shows why the text is built in
`loop()`.

## React to a button

`on_button(btn)` is called when a button is pressed while your app is shown. `btn` is `"left"`,
`"select"` or `"right"`. Return `true` to keep the press for your app:

```berry
class Pages
  var page, label

  def init()
    self.page = 1
    self.label = "1"
  end

  def on_button(btn)
    if btn == "left"
      self.page -= 1
    elif btn == "right"
      self.page += 1
    else
      return false                    # select works as usual
    end
    self.label = str(self.page)
    return true                       # the display stays on this app
  end

  def draw()
    clear()
    text(1, 6, self.label, 0xFFFFFF)
  end
end

return Pages()
```

A press your app takes stays with it: left and right do not switch apps, select does not dismiss
a notification, and a double press does not switch the display off. Each press your app takes
also gives it its full time on the display again. Return nothing, `false` or `nil`, and the button
works as usual. [Button events](time-buttons-sensors.md#button-events) adds holding and releasing.

## What a script can do {#everything-a-script-can-do-on-one-screen}

Each topic has its own page in the menu. Find the row that matches the app you have in mind, and
follow the link. In the editor, **Ctrl-.** lists every call.

| For | The calls |
|---|---|
| [Being an app](#the-lifecycle) | `draw()` `init()` `setup()` `loop()` `on_show()` `on_hide()` `on_button()` |
| [Skipping a turn](several-apps.md#sitting-a-round-out) or [staying longer](several-apps.md#setting-your-own-dwell-time) | `should_show()` `duration()` |
| [Running as a background script](several-apps.md#running-without-ever-being-shown) or [from the device menu](several-apps.md#start-from-the-device-menu) | `# @headless true`, `# @ondemand` |
| [Drawing](drawing.md#panel-and-drawing) | `clear()` `pixel()` `line()` `rect()` `rect_fill()` `circle()` `circle_fill()` `icon()` `width()` `height()` `rgb()` `hsv()` |
| [Writing text](drawing.md#panel-and-drawing) | `text()` `text_width()` `text_ink_width()` `font()` |
| [Text that moves or shades](drawing.md#styled-and-scrolling-text) | `scroll_text()` `ramp_text()` |
| [Charts and bars](drawing.md#charts-and-progress) | `bar_chart()` `line_chart()` `progress()` |
| [Animated backgrounds](drawing.md#effects-and-overlays) | `effect()` `overlay()` |
<!-- only tc002 -->
| [Text, icon and chart regions](../layouts.md) | `layout.prepare()` `layout.update()` `layout.draw()` `layout.release()` |
<!-- /only -->
| [The clock and the calendar](time-buttons-sensors.md#time) | `hour()` `minute()` `second()` `weekday()` `day()` `month()` `year()` `now_ms()` `epoch_ms()` |
| [Working with numbers](time-buttons-sensors.md#numbers) | `num()` `round()` `clamp()` `min()` `max()` |
| [Doing something later](time-buttons-sensors.md#timers) | `timer.after()` `timer.every()` `timer.cancel()` |
| [Buttons](time-buttons-sensors.md#button-events) | `on_button()` `on_button_event()` |
<!-- only esp32 esp32-s3 -->
| [What the device measures](time-buttons-sensors.md#reading-the-sensors) | `sensor.temperature()` `sensor.humidity()` `sensor.pressure()` `sensor.light()` `sensor.battery()` `sensor.battery_volts()` |
<!-- /only -->
<!-- only tc002 -->
| [The knob](time-buttons-sensors.md#the-knob) | `on_knob()` |
| [The battery](time-buttons-sensors.md#reading-the-sensors) | `sensor.battery()` `sensor.battery_volts()` |
| [Playing with one or two gamepads](../gamepad.md#in-your-own-scripts) | `gamepad.ready(player)` `gamepad.state(player)` `gamepad.down(button, player)` `gamepad.pressed(before, button, player)` `gamepad.dir(player)` `gamepad.axis(which, player)` `gamepad.trigger(which, player)` `gamepad.claim(self, player)`. Without `player`, they use player 1 |
| [Bluetooth LE in scripts](../ble.md) | `import ble` |
<!-- /only -->
| [Remembering across a reboot](storage.md#storage) | `store.get()` `store.set()` |
| [Letting the user change something](storage.md#settings-the-user-can-change) | a `# @config` line, then `store.get()` |
| [Handing values to another app](several-apps.md#talking-to-other-apps) | `shared.set()` `shared.get()` `shared.age()` `shared.keys()` |
| [Sharing code between apps](several-apps.md#sharing-code-between-scripts) | a `# @module` file, then `import` |
| [Fetching from the internet](network.md#http) | `http.get()` `http.post()` `http.put()` `http.patch()` `http.delete()` `http.request()` |
| [Picking a value out of a reply](network.md#regular-expressions) | `re.search()` `re.match()` `re.matchall()`, or `json.load()` |
| [Home automation](network.md#mqtt) | `mqtt.publish()` `mqtt.subscribe()` |
| [Reading a Modbus device](../modbus.md) | `modbus.readHoldingRegisters()` `modbus.readInputRegisters()` `modbus.readCoils()` `modbus.readDiscreteInputs()` |
<!-- only tc002 -->
| [Talking to a line-based service](../tcp.md) | `tcp.connect()` `tcp.send()` `tcp.close()` |
| [Signing in to a service](../oauth.md) | a `# @oauth` line, then `oauth.ready()` `oauth.get()` `oauth.post()` `oauth.request()` |
| [Checksums and signatures](../hashing.md#hashing) | `crypto.sha256()` `crypto.hmac_sha256()` `crypto.pbkdf2_hmac_sha256()` `crypto.random()` |
| [Mining Bitcoin](../hashing.md#proof-of-work) | `crypto.mine()` `crypto.mine_stop()` `crypto.mine_rate()` `crypto.mine_best()` |
| [Making a noise](sound.md#sound) | `sound.play()` `sound.effect()` `sound.stop()` `sound.playing()` `sound.can()` |
| [Music and effects in a game](sound.md#music-and-effects) | `sound.play({'file': 'theme', 'loop': true})` `sound.effect()` |
| [MP3s from the internet](sound.md#mp3s-from-the-internet) | `sound.play({'file': 'https://...'})` |
| [Reading text aloud](sound.md#speech) | `sound.play({'speech': text})` |
| [Music written as text](sound.md#songs-and-synth-effects) | `sound.play({'song': text})` `sound.beat()` |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| [Making a noise](sound.md#sound) | `sound.play()` `sound.stop()` `sound.playing()` `sound.can()` |
<!-- /only -->
<!-- only esp32-s3 -->
| [Music written as text](sound.md#songs) | `sound.play({'song': text, 'loop': true})` |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| [Shipping the sounds you play](sound.md#sounds-for-your-script) | MP3s in the script's own folder |
<!-- /only -->
<!-- only esp32-s3 -->
| [Reacting to the music](sound.md#music) | `music.bands()` `music.level()` `music.beat()` `music.playing()` `music.station()` `music.title()` |
<!-- /only -->
<!-- only tc002 -->
| [Reacting to the music](sound.md#music) | `music.bands()` `music.level()` `music.beat()` `music.playing()` `music.station()` `music.title()` `music.pitch()` |
<!-- /only -->
| [Interrupting with an alert](device.md#notifications) | `notify()` |
| [Moving the rotation along](device.md#driving-the-rotation) | `rotation.show()` `rotation.next()` `rotation.previous()` `rotation.pause()` `rotation.resume()` `rotation.close()` |
| [Turning the display on and off](device.md#display-power) | `display.power()` `display.is_on()` |
| [What the owner configured](device.md#device-settings) | `settings.get()` `settings.set()` `settings.apply_case()` |
| [Working out what went wrong](device.md#logging) | `log()` |
| [Which firmware is running](device.md#which-firmware-is-running) | `version()` |
| [Shipping the icons you draw](drawing.md#the-icons-your-script-needs) | a `# @icons` line, then `icon()` |
| [Naming the scripts yours needs](sharing.md#scripts-your-script-needs) | a `# @requires` line |
| [Saying which clocks it runs on](sharing.md#what-your-script-asks-of-the-clock) | `# @needs` and `# @display` lines |
| [Getting it ready to share](sharing.md#checklist-before-you-share) | the header checklist |

**What needs an `import`.** The drawing, time and number calls are plain functions. `http`, `mqtt`,
`store`, `shared`, `settings`, `sensor`, `display`, `sound`, <!-- only esp32-s3 tc002 -->`music`, <!-- /only -->`rotation`, `timer` and `re`
are ready to use. `modbus`, <!-- only tc002 -->`gamepad`, `ble`, `tcp`, `crypto`, `oauth`, `layout`, <!-- /only -->the general modules
`json`, `string` and `math`, and your own [modules](several-apps.md#sharing-code-between-scripts) need an `import` line at the top of
the file.

## Just enough Berry

Scripts are written in [Berry](https://berry-lang.github.io/), a small language that looks a bit
like Python. You do not need to know it to start: copy the examples and change them. The
[full language guide](https://berry-lang.github.io/) is there when you want more.

**Comments** start with `#` and run to the end of the line.

**Variables** are made with `var`. They have no declared type. These are the kinds of value:

```berry
var count = 3                            # integer: a whole number
var temp = 21.5                          # real: a number with decimals
var name = "kitchen"                     # string: text
var ready = true                         # bool: true or false
var readings = [21, 23, 22]              # list: several values in order
var spec = {"text": "Hi", "hold": true}  # map: values stored under names
var empty = nil                          # nil: nothing yet, or no answer
```

You will also meet a **range** (`0 .. width() - 1` below) and a **function**, which you can pass
around like any value. See [Callbacks](#callbacks). `type(v)` answers `"int"`, `"real"`,
`"string"`, `"bool"` or `"nil"`. Lists and maps both answer `"instance"`, so use
`isinstance(v, list)` or `isinstance(v, map)` for those.

**Do not name a variable like a built-in call.** A variable called `text` hides the call
`text()`, and the next `text(…)` in that method stops your app with `type_error: 'string' value
is not callable`. Pick another name: `var label = "21°"`.

**Numbers** calculate with `+`, `-`, `*`, `/` and `%` (the remainder). **Whole numbers divide
without a remainder:** `7 / 2` is `3`, and `50 / 100` is `0`. Multiply before you divide
(`50 * 8 / 100` is `4`), or make one of the numbers a real (`50 / 100.0` is `0.5`).

**Text** is joined with `+`. Turn a number into text with `str()` first:

```berry
text(1, 6, "room " + name, 0xFFFFFF)   # joining text: fine
text(1, 6, str(temp) + "°", 0xFFFFFF)  # a number needs str() first
```

`format()` puts numbers into a pattern. `%d` is a whole number, `%02d` a whole number with at
least two digits, and `%.1f` a number with one decimal:

```berry
format("%d:%02d", 9, 5)     # "9:05"
format("%.1f°", 21.46)      # "21.5°"
```

**Decisions** use `if` … `elif` … `else`, closed with `end`. Compare with `==`, `!=`, `<`, `>=`
and so on. Combine with `&&` (and) and `||` (or):

```berry
if temp >= 30
  text(1, 6, "HOT", 0xFF0000)
elif temp >= 18 && temp < 30
  text(1, 6, "ok", 0x00FF00)
else
  text(1, 6, "cold", 0x0000FF)
end
```

For a choice between two values, `condition ? a : b` answers `a` when the condition is true and
`b` otherwise:

```berry
var label = temp >= 30 ? "HOT" : "ok"
```

**Repetition** uses `for` over a range or a list, or `while` with a condition. Both close with
`end`:

```berry
for x : 0 .. width() - 1     # x runs 0, 1, ... to the right-hand edge
  pixel(x, 7, 0x202020)
end
```

**Lists:** `[]` makes an empty one, `push()` adds, `remove(i)` deletes by position, `size()`
counts, `l[0]` is the first entry and `l[-1]` the last:

```berry
var readings = []
readings.push(21)
readings.push(23)
text(1, 6, str(readings[-1]), 0xFFFFFF)   # the newest one
```

**Maps:** read a value with `find()`. It answers `nil` when the key is missing. `m["key"]` raises
an error on a missing key, so use it only for keys you wrote yourself. Maps also pass settings to
[`notify()`](device.md#notifications) and [`effect()`](drawing.md#effects-and-overlays):

```berry
var spec = {"text": "Hi", "hold": true}
var t = spec.find("text")                 # "Hi"
var pic = spec.find("icon")               # nil: absent, not an error
var shown = spec.find("icon", "none")     # "none": your own fallback
```

**Modules** add calls that are not built in. Load one with `import` at the top of the file,
above the class. [What needs an `import`](#everything-a-script-can-do-on-one-screen) lists them:

```berry
import json

var data = json.load("{\"temp\": 21.5}")   # a map: {"temp": 21.5}
```

**Errors you expect** are caught with `try` … `except` … `end`. The script keeps running, and the
lines after `except` decide what happens instead:

```berry
var label
try
  label = str(data["current"]["temp"])     # [] raises an error on a missing key
except .. as err, msg
  log(msg)
  label = "--"
end
```

`except ..` catches every error. `err` and `msg` are its name and message. Without `try`, an error
stops your app and the display shows [`ERR:`](troubleshooting.md#err-on-the-panel).

Every `if`, `for`, `while`, `def` and `class` needs its own `end`. When saving reports a
`syntax_error`, look for a missing `end` first. The line the error names can be further down.

### Callbacks {#callbacks}

Some calls take a function and call it later: `http.get()` when the answer arrives,
`timer.after()` when the time is up, `mqtt.subscribe()` for every message. Such a function is a
**callback**. There are two ways to write one:

```berry
# short: / arguments -> one expression
timer.after(3000, / -> self.reset())

# long: def (arguments) ... end, for several lines
mqtt.subscribe("home/door", def (topic, payload)
  self.door = payload
  self.changed = true
end)
```

- A callback written inside a method of your app can use `self`, your app, so it can change your
  app's members.
- The short form holds one expression. Usually it hands everything on to a method of your app:
  `/ body, status -> self.on_body(body, status)`.
- Write callbacks where you start something: in `setup()`, `loop()` or a button handler. Never
  write them in `draw()`.

## Good to know

- **Whole numbers divide without a remainder:** `50 / 100` is `0`. Multiply before you divide, or
  write `100.0`.
- **Do not name a variable like a built-in call.** A variable called `text` hides `text()`, and
  the next call to it fails. Use another name, such as `label`.
- **Build lists, maps and text outside `draw()`.** One built in `draw()` is built again 40 times a
  second. Build it in `init()`, `loop()` or a callback, and keep it in a member.
- **Drawing works only in `draw()`.** A `text()` in `loop()` or `setup()` shows nothing and raises
  no error. Keep the value in a member and paint it in `draw()`.
- **A `syntax_error` when you save** usually means a missing `end`. The line it names can be
  further down.

## Details

### The lifecycle

The hooks are **methods on your class**. Only `draw()` is required. Add the others when you need
them.

| Method | When it is called | Can draw? |
|---|---|---|
| `init()` | once, when AWTRIX creates your app | no |
| `setup()` | once, right after the app loads, before the first frame | no |
| `loop()` | about once a second, **whether or not your app is shown** | no |
| `draw()` | every frame (~40/s) while your app is shown | **yes** |
| `on_show()` | your app has just appeared | no |
| `on_hide()` | your app has just left the display | no |
| `on_button(btn)` | a button was pressed while your app is shown. `btn` is `"left"`, `"select"` or `"right"`. Return `true` to take the press | no |
| `on_button_event(btn, event)` | a press, hold or release of a button your app took. See [Button events](time-buttons-sensors.md#button-events) | no |
<!-- only tc002 -->
| `on_knob(event)` | the knob was turned or pressed while your app is shown. Return `true` to take it. See [The knob](time-buttons-sensors.md#the-knob) | no |
<!-- /only -->
| `should_show()` | the rotation moves on to your app. Return `false` to skip this turn. See [Skip a turn](several-apps.md#sitting-a-round-out) | no |
| `duration()` | your app appears. Return milliseconds to set how long it stays. See [Stay longer](several-apps.md#setting-your-own-dwell-time) | no |

- **`init()` or `setup()`?** Give members their starting values in `init()`. Do first fetches
  and logging in `setup()`. Your [store](storage.md#storage) is ready in both.
<!-- only esp32 esp32-s3 -->
- After a reboot, the apps start their first `loop()` a couple of seconds apart, so they do not
  all fetch in the same second.
<!-- /only -->
- `width()`, `height()` and the text measurements work in every hook.
- A misspelled built-in call (`clesr()`) shows up when you save, as a `syntax_error`. Your own
  methods are looked up when they are called, so `self.helper()` may use a method defined further
  down the class.
- Only the app shown is asked about a button. If `on_button()` raises an error, the press goes
  through as usual, so a broken app cannot lock the buttons.
- The class name is yours. Two scripts may both use `class App`.
- `should_show()`, `duration()` and the header lines that keep an app out of the rotation are in
  [Several apps together](several-apps.md#more-than-one-app).

### The editor

Built-in calls are shown in their own color, so a misspelled `pixel` stays plain. **Ctrl-.**
lists the calls, **Ctrl-S** saves, **Ctrl-/** comments lines in or out, and **Tab** and
**Shift-Tab** indent a selection. The status line shows the cursor position and the size of the
script. Every button of the **Scripts** tab is in [The web UI](../../getting-started/web-ui.md#scripts).

## Related

- [Tutorial 1: Draw something](../../tutorials/first-draw.md): the first of three tutorials that
  build one app, followed by recipes and [Keeping scripts small](../../tutorials/going-easy-on-memory.md)
- [Build an app with AI](../ai-prompt.md): this API as a prompt for a chatbot
<!-- only tc002 -->
- [Layouts](../layouts.md): text, icon, chart and progress regions with `layout.prepare()`,
  `layout.update()` and `layout.draw()`
- [Gamepad](../gamepad.md) and [Bluetooth LE in scripts](../ble.md): the modules for games and sensors
<!-- /only -->
- [Icons](../icons.md): what `icon()` can draw, and how to upload more
- [App & notification payload](../../reference/payload.md): the format `notify()`, `effect()` and
  the charts share
- [Pushed apps](../pushed-apps.md): the simpler way to show your own content, when something
  outside AWTRIX sends it
- [Modbus in scripts](../modbus.md): read an energy meter or inverter over Modbus TCP
<!-- only tc002 -->
- [TCP in scripts](../tcp.md) and [Hashing and mining in scripts](../hashing.md): the modules
  for line-based services, signatures and Bitcoin mining
<!-- /only -->
- [MQTT](../mqtt.md): AWTRIX's own topics
- [Limits](../../reference/limits.md#scripting): every limit a script runs under, in one table
