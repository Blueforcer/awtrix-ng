# AWTRIX scripting

Pushed apps get you text, an icon and a colour. Scripting gets you those same
marks - charts, effects, overlays, styled text, even notifications - *and* the
pixels underneath them, worked out on AWTRIX from your own logic and data.

A **script** is a small program you upload. It becomes an app in
the rotation exactly like a [pushed app](pushed-apps.md), and one difference
decides which of the two you want:

- a **pushed app** shows what something outside sent it, and is gone after a reboot;
- a **script** works out its own content - drawing frame by frame, fetching its own
  data, keeping its own state - and its source stays on AWTRIX, so it comes
  back by itself.

The language is [Berry](https://berry-lang.github.io/): a small, Python-shaped
language built for microcontrollers. Nothing is compiled and nothing is flashed.
You paste source into the web UI, hit save, and the app is on the panel a moment
later.

!!! tip "Not a programmer?"
    [**Build an app with AI**](ai-prompt.md) is this page compressed into a system
    prompt you hand a chatbot. Paste it in, describe the app you want in plain
    words, and paste the script it hands back into the web UI. Everything below
    still applies - you just are not the one typing it.

---

## Your first script

An app is a **class**. Its methods are the lifecycle hooks, its state lives in members, and the file ends by handing back an instance. Open the web UI, go to the **Scripts** tab, create a script called `Hello`, and paste this:

```berry
class Hello
  def draw()
    clear()
    text(1, 6, "hi", 0x00FF00)
  end
end

return Hello()
```
<!-- shot:begin id=hello hash=d6cd03db -->
![The panel showing "hi" in green](../assets/shots/scripting/hello.png){ .shot }
<!-- shot:end -->


Save it. `Hello` is now in the app rotation, and when it comes round the panel says **hi** in green.

That is the whole minimum: a class with **`draw()` - the only method a script must define - and a final `return YourClass()`.** `draw()` is called on every frame - about 40 times a second - while your app is the one on screen, and whatever it paints is the frame.

The frame arrives already blank, so `clear()` is not strictly required - but starting with it is the habit worth having, and `clear(color)` is how you lay down a background other than black.

Now make it do something:

```berry
class Hello
  def draw()
    clear()
    var w = text_ink_width("hi")
    text((width() - w) / 2, 6, "hi", 0x00FF00)
    pixel(0, second() % 8, 0xFF0000)
  end
end

return Hello()
```
<!-- shot:begin id=hello-2 hash=add6df2b -->
![The panel showing the Hello script](../assets/shots/scripting/hello-2.gif){ .shot }
<!-- shot:end -->


Centred text, and a red pixel walking down the left edge once a second.

Wrapping each app in a class keeps its methods and state to itself, and the `return` hands AWTRIX the instance to run. The class name is yours and never collides: two scripts may both be `class App` without either noticing.

**The editor knows this API.** Builtins are highlighted in their own colour and offered
by completion (**Ctrl-Space**, or just keep typing), so a misspelled `pixel` stays plain
and is visible before you save, and the list always matches whichever AWTRIX you are
talking to. **Ctrl-S** saves, **Ctrl-/** toggles comments, **Tab** / **Shift-Tab** indent a
selection, and the status line counts the bytes you have left of what AWTRIX accepts
(8 KB by default).

If the panel shows **`ERR:`** in red, your script hit an error - a typo, a bad index,
anything. Nothing is harmed: open the **Scripts** tab and the message is right there
next to your script; fix the line it names and save again. The full story is under
[`ERR:` on the panel](#err-on-the-panel).

!!! tip "No AWTRIX in front of you? The simulator runs scripts too"
    The [simulator](../advanced/simulator.md) is a much faster place to iterate than
    hardware on the other side of the room. Drawing, time, storage, icons and `http.get()`
    all work there. `mqtt.publish()` and `mqtt.subscribe()` work too, against a real
    broker - point the simulator at one with `mqttHost` (see
    [the simulator's MQTT note](../advanced/simulator.md#mqtt)).

---

## Everything a script can do, on one screen

The rest of this page explains each of these properly. This is the map: find the
row that sounds like the app you have in mind, and follow it.

| For | The calls |
|---|---|
| [Being an app](#the-lifecycle) | `draw()` `setup()` `loop()` `on_show()` `on_hide()` `on_button()` `should_show()` `duration()` |
| [Drawing](#panel-and-drawing) | `clear()` `pixel()` `line()` `rect()` `rect_fill()` `circle()` `circle_fill()` `icon()` `width()` `height()` `rgb()` `hsv()` |
| [Writing text](#panel-and-drawing) | `text()` `text_width()` `text_ink_width()` `font()` |
| [Text that moves or shades](#styled-and-scrolling-text) | `scroll_text()` `ramp_text()` |
| [Charts and bars](#charts-and-progress) | `bar_chart()` `line_chart()` `progress()` |
| [Animated backgrounds](#effects-and-overlays) | `effect()` `overlay()` |
| [The clock and the calendar](#time) | `hour()` `minute()` `second()` `weekday()` `day()` `month()` `year()` `now_ms()` `epoch_ms()` |
| [Working with numbers](#numbers) | `num()` `round()` `clamp()` `min()` `max()` |
| [Remembering across a reboot](#storage) | `store.get()` `store.set()` |
| [Letting the user change something](#settings-the-user-can-change) | a `# @config` line, then `store.get()` |
| [Fetching from the internet](#http) | `http.get()` `http.post()` `http.put()` `http.patch()` `http.delete()` |
| [Picking a value out of a reply](#regular-expressions) | `re.search()` `re.match()` `re.matchall()`, or `json.load()` |
| [Home automation](#mqtt) | `mqtt.publish()` `mqtt.subscribe()` |
| [Handing values to another app](#talking-to-other-apps) | `shared.set()` `shared.get()` `shared.age()` `shared.keys()` |
| [Sharing code between apps](#sharing-code-between-scripts) | a `# @module` file, then `import` |
| [Interrupting with an alert](#notifications) | `notify()` |
| [Making a noise](#sound) | `sound.play()` `sound.rtttl()` `sound.stop()` `sound.playing()` |
| [What the device measures](#reading-the-sensors) | `sensor.temperature()` `sensor.humidity()` `sensor.pressure()` `sensor.light()` `sensor.battery()` |
| [What the owner configured](#device-settings) | `settings.get()` `settings.set()` `settings.apply_case()` |
| [Moving the rotation along](#driving-the-rotation) | `rotation.show()` `rotation.next()` `rotation.previous()` `rotation.pause()` `rotation.resume()` |
| [Working out what went wrong](#logging) | `log()` |
| [Which firmware is running](#which-firmware-is-running) | `version()` |

None of it needs an `import`. Only `json`, `string` and `math` do, and one line at
the top of the file is the whole ceremony.

Three habits carry most of it: keep what you fetched in a member and let
[`draw()`](#the-lifecycle) only paint it, ask [`width()`](#panel-and-drawing)
instead of assuming 32 columns, and put anything the next person might want to
change in a [`@config`](#settings-the-user-can-change) line rather than in the
source. A [complete app doing exactly that](#a-real-one-weather) is at the end of
the page.

---

## Just enough Berry

You do not need to know Berry to use this page - the examples are meant to be
copied and bent. (The [full language
tour](https://berry-lang.github.io/) exists when you outgrow it.)

**Comments** start with `#` and run to the end of the line.

**Variables** are made with `var` and hold whatever you put in them. Nothing is
declared with a type, and these are all the kinds of value there are:

```berry
var count = 3                            # integer - a whole number
var temp = 21.5                          # real - a number with decimals
var name = "kitchen"                     # string - text
var ready = true                         # bool - true or false
var readings = [21, 23, 22]              # list - several values in order
var spec = {"text": "Hi", "hold": true}  # map - values stored under names
var empty = nil                          # nil - nothing yet; many calls return it for "no answer"
```

Two more appear without you ever writing them down: a **range**, which is what
`0 .. 31` below is, and a **function**, which is a value like any other - that is
how the callback handed to [`http.get()`](#http) travels. `type(v)` answers
`"int"`, `"real"`, `"string"`, `"bool"` or `"nil"` for the first five; lists and
maps both answer `"instance"`, so ask `isinstance(v, list)` or
`isinstance(v, map)` for those.

**Text** is joined with `+`, and numbers must be turned into text first with
`str()`:

```berry
text(1, 6, "room " + name, 0xFFFFFF)   # joining text: fine
text(1, 6, str(temp) + "°", 0xFFFFFF)  # a number needs str() first
```

**Decisions** are `if` … `elif` … `else`, closed with `end`. Comparisons are
`==`, `!=`, `<`, `>=` and friends; combine them with `&&` (and), `||` (or):

```berry
if temp >= 30
  text(1, 6, "HOT", 0xFF0000)
elif temp >= 18 && temp < 30
  text(1, 6, "ok", 0x00FF00)
else
  text(1, 6, "cold", 0x0000FF)
end
```

**Repetition** is `for` over a range or a list, or `while` with a condition -
also closed with `end`:

```berry
for x : 0 .. width() - 1     # x runs 0, 1, ... to the right-hand edge
  pixel(x, 7, 0x202020)
end
```

**Working a list:** `[]` makes an empty one, `push()` appends, `remove(i)` deletes
by index, `size()` counts, `l[0]` is the first entry and `l[-1]` the last:

```berry
var readings = []
readings.push(21)
readings.push(23)
text(1, 6, str(readings[-1]), 0xFFFFFF)   # the newest one
```

**Working a map:** read a value with `find()`, which answers `nil` when the key is
not there. `m["key"]` *raises* on a missing key, so save that for keys you wrote
yourself a line earlier. Maps are also how settings reach
[`notify()`](#notifications) and [`effect()`](#effects-and-overlays):

```berry
var spec = {"text": "Hi", "hold": true}
var t = spec.find("text")                 # "Hi"
var icon = spec.find("icon")              # nil - absent, not an error
var shown = spec.find("icon", "none")     # "none" - your own fallback
```

That is the whole survival kit. One habit to keep: every `if`, `for`,
`while`, `def` and `class` is closed with its own `end` - when a save fails with
a `syntax_error`, a missing `end` is the first thing to look for.

---

## The lifecycle

The hooks are **methods on your class**. Only `draw()` is required; define the ones you need and leave the rest out. `init()` is Berry's own constructor and runs when the instance is created - use it to set up your members.

| Method | When | Canvas? |
|---|---|---|
| `init()` | once, as the instance is created (Berry's constructor) | no |
| `setup()` | once, right after the app loads, before the first frame | no |
| `loop()` | about once a second, **whether or not your app is on screen** | no |
| `draw()` | every frame (~40/s) while your app is on screen | **yes** |
| `on_show()` | your app has just been rotated in | no |
| `on_hide()` | your app has just been rotated out | no |
| `on_button(btn)` | a button was pressed while your app is on screen; `btn` is `"left"`, `"select"` or `"right"` | no |
| `should_show()` | the rotation has reached you - return `false` to let it pass you by | no |
| `duration()` | the rotation has reached you - return ms to override how long you stay | no |

`init()` vs `setup()`: `init()` is a plain constructor and runs the moment `YourClass()` is evaluated, so use it to give members a starting value. `setup()` runs just after, once the app is wired in, and is the first place with a restored [store](#storage). When in doubt, initialise members in `init()` and do first-fetch/logging work in `setup()`.

`loop()` running while you are hidden is the point of it. It is where you poll, count down and refresh, so that when the rotation comes back to you the data is already there and `draw()` has nothing to do but paint.

After a reboot each app's first `loop()` starts a couple of seconds after the previous app's, so a device full of pollers does not fire every first fetch in the same second.

`draw()` is the only place with a canvas. The drawing functions still exist everywhere else - they simply do nothing, so calling `pixel()` from `loop()` is harmless and invisible rather than an error. Asking questions is fine anywhere: `width()`, `height()` and the text measurements answer the same in every hook.

`on_show()` runs before the first `draw()` of that appearance, so it is the place to reset anything that should start over each time your app comes round - a scroll position, an animation step, a counter.

State lives in **instance members** (`self.x`), declared with `var` at the top of the class. They persist between calls:

```berry
class Frames
  var frames

  def init()
    self.frames = 0
  end

  def draw()
    clear()
    self.frames += 1
    text(1, 6, str(self.frames), 0xFFFFFF)
  end
end

return Frames()
```
<!-- shot:begin id=frames hash=fadab4c1 -->
![The panel showing the Frames script](../assets/shots/scripting/frames.gif){ .shot }
<!-- shot:end -->


A typo in a builtin (`clesr()`) fails at install time with a `syntax_error`, rather
than when that line finally runs. Methods on your own class are looked up when they are
called, so `self.helper()` may refer to a method defined further down the class.

`on_button` is a notification, not a capture: your hook fires, and then left/right still
rotate to the next app as usual. `select` has no other job in the rotation, so it is the
one to use for an action.

### Sitting a round out

Some apps only sometimes have something to say: the bin goes out tomorrow, the train is late, the sensor stopped answering. Rather than show an empty panel for seven seconds, say so - `should_show()` is asked as the rotation arrives at you, and `false` sends it on to the next app in one transition. You keep your slot and your position; nothing is removed and nothing needs putting back.

```berry
class Bin
  var day        # weekday the bin goes out, nil until the fetch lands

  def should_show()
    return self.day != nil && self.day == weekday()
  end

  def draw()
    clear()
    text(1, 6, "Bin out!", 0x00A0FF)
  end
end

return Bin()
```
<!-- shot:begin id=bin hash=d3528868 -->
![The panel showing "Bin out!" in cyan](../assets/shots/scripting/bin.png){ .shot }
<!-- shot:end -->


Four rules worth knowing:

- **Only an outright `false` hides you.** No `should_show()` at all, a `should_show()` that falls off its end without a `return`, or a script that is broken and showing `ERR:` - all of those keep their turn. A missing `return` never costs you your app.
- **Once you are on, you stay on.** The question is asked when the rotation picks you, not again while you are drawn. Your app is not going to vanish mid-sentence.
- **Asking for you by name still works.** The web UI's ▶ button, the MQTT `apps/switch` command and `PUT /api/v1/apps/active` ignore the veto - someone asking for this app specifically means it.
- **If everyone declines, the panel does not go dark.** The app already on screen stays there until somebody wants a turn again.

`loop()` keeps running while you are being skipped - that is exactly how you get back the thing that makes you want to be seen. A skipped app is marked with a dimmed chip on its row in the web UI, so "my app never comes up" is a question with an answer.

Sitting a round out is not the same as being **deactivated** in the app-order editor. Skipping is
your decision and you keep running; deactivating is the user's, and it stops your code - see below.

### Running without ever being shown

Two separate things can be true of your app. It **runs** - `loop()` ticks, HTTP answers arrive, MQTT
messages reach you - and it is **in the rotation**, drawn in its turn. Deactivating an app in the
web UI takes away the first, and with it the second: nothing of a deactivated script runs, though it
stays installed and keeps its store.

Some scripts genuinely have no reason to be on the panel: an MQTT listener that only raises
notifications, or a provider that fetches once a minute and publishes to
[`shared`](#talking-to-other-apps) for other apps to draw. Those say so in the header:

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

`@headless true` means **this script does not draw**. It runs like any other app and is never given
a turn on the panel, so `draw()`, `should_show()` and `duration()` are never called - leave them
out. The web UI lists it under **Background**, and the flag shows up as `headless` in
`GET /api/v1/apps`, so a script working invisibly is something you can see rather than guess at.

Switching it off is the same gesture as for anything else: **−** in the Background card deactivates
it, **+ Activate** starts it again.

### Setting your own dwell time

Every app is shown for the global app-time (7000 ms out of the box) before the rotation moves on. `duration()` overrides that for your app alone: return a number of milliseconds and the rotation stays on you for exactly that long this turn. It is the script equivalent of a pushed app's `durationMs` field.

```berry
class Slow
  def duration()
    return 15000        # linger for 15 s
  end

  def draw()
    clear()
    text(1, 6, "read me", 0xFFCC00)
  end
end

return Slow()
```
<!-- shot:begin id=slow hash=9ef9b2b7 -->
![The panel showing "read me" in yellow](../assets/shots/scripting/slow.png){ .shot }
<!-- shot:end -->


`duration()` is asked each time the rotation arrives at you, so you can return a different value depending on what there is to show - a long dwell while a message is up, the global time otherwise. **Return `0` or nothing to keep the global app-time**; a missing hook, a non-positive number and a broken app all fall back to it, so you never have to answer unless you want to. The value only sets *how long* you stay - it does not change *whether* you appear (`should_show()`) or *where* you sit in the loop.

### Reading the sensors

What the device measures, the same readings the built-in Temperature and Humidity apps draw:

| Call | Answer |
|---|---|
| `sensor.temperature()` | °C |
| `sensor.humidity()` | relative humidity in % |
| `sensor.pressure()` | hPa |
| `sensor.light()` | ambient brightness |
| `sensor.battery()` | charge in %, a whole number |
| `sensor.battery_volts()` | cell voltage |

**Every one answers `nil` when the board has no such sensor.** A board without a temperature sensor
gives you `nil` for temperature, not a convincing `0` - so check before you draw:

```berry
var t = sensor.temperature()
if t == nil
  text(1, 6, "--", 0x666666)
else
  text(1, 6, str(int(t)) + "°", 0xFF8800)
end
```

Temperature is always Celsius and humidity always a percentage. The raw value stays raw;
`settings.get("useCelsius")` tells you what the user wants to see, and converting is yours:
`f = c * 9 / 5 + 32`.

The readings refresh on the device's own schedule, not once per frame - reading them in `draw()` is
cheap, but nothing changes between two frames.

### Driving the rotation

The `rotation` module lets a script move the rotation itself, rather than wait for the clock:

```berry
rotation.next()      # advance to the next app now
rotation.previous()  # step back to the previous app
rotation.show()      # bring the rotation to THIS app now
rotation.pause()     # freeze the auto-advance clock
rotation.resume()    # let it run again
```

`rotation.show()` is for when you have something to say and do not want to wait a full cycle for
your turn. It takes no argument - a script can only summon itself - and returns `false` if your app
is not in the rotation. Any pause you set survives it, so `show()` then `pause()` puts you on screen
and keeps you there.

`rotation.pause()` stops the automatic advance so the display stays where it is - useful while you are mid-animation or waiting on something. It does **not** trap the user: `rotation.next()`/`rotation.previous()` still move even while paused, so a paused app can step through a sequence itself, and **any move the user makes - a button, the web UI, the API - clears the pause and returns to normal rotation.** Call `rotation.resume()` when your reason to hold has passed; if you forget, the next user action recovers it anyway.

---

## The API

Everything below is callable from any of your class's methods, with nothing to import: the drawing, time and number calls are plain global functions, and `http`, `mqtt`, `store`, `shared`, `settings`, `sound`, `rotation` and `re` are ready-made objects. Only the general-purpose modules - `json`, `string`, `math` - want one `import` line at the top of the file, and the [HTTP example](#http) shows it in place.

The short examples in this section show a single method for brevity - read them as living inside your class, alongside `draw()` and a `return YourClass()` at the end of the file.

### Panel and drawing

The panel is a grid eight pixels high and - on a Ulanzi and most others - thirty-two
wide. **`x` runs from `0` at the left to `width() - 1`, `y` runs 0–7 from the
top** - so `(0, 0)` is the top-left corner and a *larger* `y` is *lower* on the
panel. Anything you draw off the edge is simply clipped, never an error.

Ask `width()` rather than writing `32`: someone running two or four panels in a
row has 64 or 128 columns, and an app that measures fills them instead of
huddling in the first quarter. The height is always 8.

Both answer while a frame is being drawn, which is where layout belongs anyway -
in `setup()`, before there is a frame, they say `0`.

**A colour is one number.** Write it as `0x` followed by the same six hex digits
you know from HTML colour codes: `#FF0000` on the web is `0xFF0000` here - two
digits red, two green, two blue. `0xFFFFFF` is white, `0x000000` black, and any
web colour picker's hex value works with the `#` swapped for `0x`. If you would
rather not think in hex at all, the `rgb()` and `hsv()` calls in the table below
build the same number from plain channel values.

| Call | Does | Example |
|---|---|---|
| `width()` | panel width in pixels - 32 as a rule, more on a chained panel | `var w = width()` |
| `height()` | panel height in pixels (always 8) | `var h = height()` |
| `clear(color?)` | fill the frame; black when omitted | `clear()` |
| `pixel(x, y, color)` | one pixel | `pixel(0, 0, 0xFF0000)` |
| `line(x0, y0, x1, y1, color)` | a line | `line(0, 0, width() - 1, 7, 0x00FF00)` |
| `rect(x, y, w, h, color)` | rectangle outline | `rect(0, 0, 32, 8, 0x333333)` |
| `rect_fill(x, y, w, h, color)` | filled rectangle | `rect_fill(0, 6, 10, 2, 0x0000FF)` |
| `circle(cx, cy, r, color)` | circle outline | `circle(4, 4, 3, 0xFFFFFF)` |
| `circle_fill(cx, cy, r, color)` | filled circle | `circle_fill(4, 4, 3, 0xFFD700)` |
| `text(x, y, str, color?)` | draw text, returns the advance in pixels | `var adv = text(1, 6, "hi", 0xFFFFFF)` |
| `text_width(str)` | how far the pen moves - use it to chain runs and to space repeats | `var w = text_width("hi")` |
| `text_ink_width(str)` | how wide the lit pixels are - use it to fit and to centre | `var w = text_ink_width("hi")` |
| `font(name)` | switch to `"small"` or `"large"` for the rest of the frame | `font("large")` |
| `icon(name, x, y)` | an 8×8 icon by name; `false` if it could not be drawn | `icon("1234", 0, 0)` |
| `rgb(r, g, b)` | pack a colour from channels (0–255) | `pixel(0, 0, rgb(255, 128, 0))` |
| `hsv(h, s, v)` | pack a colour from hue/sat/val (h 0–360, s/v 0–100) | `hsv(second() * 6, 100, 100)` |

`y` in `text()` is the **baseline**, not the top. `6` puts a normal five-pixel line neatly inside the eight-pixel panel. The return value is the advance, so text can be chained:

```berry
def draw()
  clear()
  var adv = text(1, 6, "CPU ", 0x888888)
  text(1 + adv, 6, "42%", 0x00FF00)
end
```

Leave the colour off and the text takes the device's own `textColor`, the same one the built-in
apps use.

<a id="text-in-several-colours"></a>
**Text in several colours** - anywhere a call takes a string it also takes a list of pieces,
each one `[text, colour]`:

```berry
def draw()
  clear()
  text(1, 6, [["CPU ", 0x888888], ["42%", 0x00FF00]])
end
```

The pieces are one line: they measure, centre and scroll together, so `scroll_text()` and the
measuring calls take the same list. A piece written as a plain string, or as `["text"]` with no
colour of its own, uses the colour of the call, and `font("large")` applies to all of them. It is
the same thing a [pushed app](../reference/payload.md#colored-fragments) does when its `text` is
an array rather than a string.

`icon()` draws from the same `/ICONS` folder the rest of AWTRIX uses - see [Icons & assets](icons.md). Give it the bare name, no path and no extension. **An animated GIF animates** - draw the same icon each frame and it plays, on the same schedule a [pushed app](pushed-apps.md)'s icon uses. A handful of icons stay cached, so cycling through a small set is cheap while fanning out over many costs a read each time.

`icon()` returns `false` for a name AWTRIX does not have, and briefly also when it is
short on memory - that second case heals itself within a few seconds, but the icon draws
nothing meanwhile. If a hole would be worse than a placeholder, paint one:

```berry
if !icon(self.ic, 0, 0)
  rect_fill(0, 0, 8, 8, 0x222222)   # or a pixel-drawn glyph
end
```

**Write script text in UTF-8.** `text()`, `text_width()`, `text_ink_width()` and `ramp_text()` read UTF-8, the same as the rest of the API. Type `"21°C"` and `"Grüße"` straight into your source. They measure glyphs, so a `°` counts once. Which characters have glyphs is in [Text & colors](text.md#umlauts-accents-and-other-languages).

`font("large")` applies to `text()`, `ramp_text()` **and** both measuring calls for the rest of
the frame, so centring and fitting stay correct across a switch. The choice resets every frame, so
set it in `draw()` rather than once in `setup()`. `large` is seven rows tall and reaches the
top row - see [Text & colors](text.md#the-fonts).

### Styled and scrolling text

The plain `text()` draws one solid colour. These draw the same styled and moving
text a [pushed app](pushed-apps.md) gets. `ramp_text()` returns the pixel advance
and chains exactly like `text()`; `scroll_text()` owns its own placement and
returns the number of times the text has run through.

| Call | Does | Example |
|---|---|---|
| `ramp_text(x, y, str, palette, span?, speed?)` | text painted from a palette, per pixel column | `ramp_text(0, 6, "HOT", [0xFFFF00, 0xFF0000])` |
| `scroll_text(str, color?, opts?)` | a moving line across the whole panel | `scroll_text(self.headline)` |
| `scroll_text(x, y, w, str, color, opts?)` | the same, inside the columns you name | `scroll_text(9, 6, width() - 9, self.line, 0xFFFFFF)` |

<a id="palettes"></a>
**A palette** is a built-in or uploaded palette name, or a list of up to 16 colour
stops. Plain stops are spread evenly across the whole ramp, so
`[0xFFFF00, 0xFF0000]` is a full yellow-to-red gradient. A stop may instead be
`[colour, pos]` with `pos` in `0-100`, placing it at that percentage:
`[[0xFFFF00, 0], [0xFF0000, 30]]` reaches red a third of the way along and stays
there. Positions are all or nothing within one list, and a palette name AWTRIX
does not have draws nothing. The [charts](#charts-and-progress) and
[effects](#effects-and-overlays) take the same palette argument in place of
their colour.

For `ramp_text()`, `span` is the pixels per full pass (`0`, the default,
stretches one pass across the string) and `speed` is passes per second (`0`
holds still).

`scroll_text()` moves a line the same way a pushed app does. A string that fits
stands still and centred; one that overflows travels, and **your app keeps the
panel until it has run through once** - you do not have to work out how long that
takes.

```berry
def draw()
  clear()
  scroll_text("a headline too long to fit on the panel", 0x00AAFF)
end
```

Left out, the colour is the device's own `textColor`, and the line sits on the
baseline every other app uses. In place of the string you may pass a
[list of coloured pieces](#text-in-several-colours); it travels as one line.

**Give it columns of its own** when your app draws something beside it. The
second form takes the box the text may travel in - nothing is painted outside it,
so an icon of your own stays untouched:

```berry
def draw()
  clear()
  icon("1234", 0, 0)
  scroll_text(9, 6, width() - 9, self.line, 0xFFFFFF, {"mode": "bounce"})
end
```

**`opts`** is a map, and it is the same vocabulary a pushed app sends under
[`scroll`](../reference/payload.md#scrolling) - plus `repeat`:

| Key | Value | Meaning |
|---|---|---|
| `mode` | `"static"`, `"wrap"`, `"loop"`, `"bounce"` | how the line moves |
| `speed` | percent, `100` = 21 px/s | how fast |
| `gap` | pixels | space between repeats in `loop` |
| `holdMs` | milliseconds | pause before it sets off, and at each end in `bounce` |
| `direction` | `"left"`, `"right"` | which way |
| `entry` | `"inline"`, `"offscreen"` | start in place, or slide in from the edge |
| `whenFits` | `"static"`, `"scroll"` | whether a short line moves at all |
| `repeat` | count | runs to complete before the rotation may move on; `0`, the default, never holds |

Every key you leave out follows the device's own scroll settings, so two scripts
can look completely different while both still respect what the owner configured.

An app with several lines to show gives each one a turn of its own, rather than
timing them itself:

```berry
def draw()
  clear()
  scroll_text(self.lines[self.i], 0xFFFFFF)
end

def on_hide()
  self.i = (self.i + 1) % size(self.lines)
end
```

The return value counts completed runs, for an app that wants to act on one.

### Time

| Call | Range |
|---|---|
| `hour()` | 0–23 |
| `minute()` | 0–59 |
| `second()` | 0–59 |
| `weekday()` | 0–6, 0 = Sunday |
| `day()` | 1–31 |
| `month()` | 1–12 |
| `year()` | e.g. 2026 |
| `now_ms()` | milliseconds since boot |
| `epoch_ms()` | milliseconds since 1 January 1970 UTC |

```berry
def draw()
  clear()
  text(4, 6, str(hour()) + ":" + str(minute()), 0xFFFFFF)
end
```

The clock is attached to every hook: `draw()`, `loop()`, `on_show()`, `on_hide()`, `on_button()`, `should_show()` and every HTTP or MQTT callback all read the same wall-clock time.

**One place has no clock: `init()` and `setup()` while AWTRIX is starting up.** Your app is loaded before the device has fetched the time, so every call in the table above answers `-1` there - not a wrong time, an impossible one. Only the clock is missing: `width()`, `height()`, `text_width()` and `text_ink_width()` all answer properly there, so measuring text and sizing to the panel in `init()` is safe. It costs nothing as long as you do not build something from the clock: give members their starting values in `init()` and read the time in `loop()` or `draw()`, where it is always right. If `setup()` genuinely needs the date, check first:

```berry
  def setup()
    if hour() >= 0
      # the clock is up - this is a re-save, not a cold boot
    end
  end
```

`now_ms()` is the exception. It counts from boot rather than from the calendar, so it answers properly everywhere, `setup()` included.

`now_ms()` is a tick counter, not a date. It counts milliseconds from boot and resets to 0 on every reboot, so a deadline built from it survives only until AWTRIX restarts.

Animation runs on it: a value derived from `now_ms()` advances at a steady rate whatever the frame rate happens to be doing.

```berry
def draw()
  clear()
  var t = (now_ms() % 2000) / 2000.0    # 0.0 → 1.0, one sweep every two seconds
  rect_fill(0, 0, round(t * width()), 8, 0x2266FF)
end
```

`epoch_ms()` is the date and time in milliseconds, counted from 1 January 1970 UTC. It returns `-1` until the device has fetched the time, so check for that before you calculate with it.

Time zones are offset by whole minutes, so `epoch_ms() % 1000` is the position inside the current second and `% 60000` the position inside the minute. That is the phase an animation needs to land on the tick of the clock:

```berry
def draw()
  clear()
  rect_fill(0, 7, round((epoch_ms() % 1000) * width() / 1000), 1, 0x2266FF)   # refills on the tick
end
```

It also puts a timestamp that arrived from somewhere else next to your own clock:

```berry
var age = (epoch_ms() - num(self.updated_at)) / 1000    # seconds old
```

`epoch_ms()` is UTC. Read the local hour from `hour()`.

For coarse periodic work in `loop()` - refreshing a value every minute, say - counting calls is simpler than comparing deadlines. Keep the counter in a member (`self.ticks`, declared `var ticks` and set to 0 in `init()`):

```berry
  def loop()
    if self.ticks <= 0
      self.ticks = 60    # loop() runs ~1x/s, so this is roughly a minute
      # ... do the thing ...
    end
    self.ticks -= 1
  end
```

### Numbers

| Call | Does |
|---|---|
| `num(v, dflt?)` | value → `int`/`real`, else `dflt` |
| `round(v, digits?)` | half away from zero; `round(2.5)` → `3`, `round(876.64, 1)` → `876.6` |
| `clamp(v, lo, hi)` | pin a value into a range |
| `min(a, b)`, `max(a, b)` | the smaller / larger of two values |

`round()` without `digits` returns an `int`; with `digits` a `real` - handy before `str()`, which would otherwise print every decimal a `real` carries. `clamp`, `min` and `max` order with `<`, so they take anything comparable, strings included.

```berry
var y = 7 - clamp(round(pct * 7 / 100.0), 0, 7)   # a 0–100 % value as a bar height
text(10, 6, str(round(self.watt)) + "W", 0xFFD000)
```

**A value that arrives as text is text.** An MQTT payload or a field picked out of
an HTTP response can stay a string as long as you only display it; the moment you
compare, calculate or persist it, put it through `num()`:

```berry
mqtt.subscribe("pv/watt", / t, payload -> self.set(num(payload)))
```

`num(v)` returns an `int` or `real` for anything that holds one: numbers pass
through untouched, `"5945"` and `"876.6"` parse, and so does `"\"876.6\""` - the
quoted form a broker publishing string-typed states emits. Everything else
returns `nil`, or the fallback if you give one: `num(payload, 0)`. `"876,6"`,
`"876.6 W"` and plain garbage all come back `nil` instead of a silently wrong
number, so a formatting surprise upstream shows on the panel as your no-data
state rather than as a value that looks plausible and is not.

### Charts and progress

The pushed-app decorations, drawn imperatively. Each spans the full panel width
(a script owns its canvas - there is no reserved icon column) and takes the same
values a pushed app's `bar` / `line` / `progress` keys do.

| Call | Does | Example |
|---|---|---|
| `bar_chart(list, color?, autoscale?)` | a bar per value; negatives hang below zero | `bar_chart([3,5,2,8,6], 0x00FF00)` |
| `line_chart(list, color?, autoscale?)` | a polyline across the values | `line_chart(self.history, 0x00AAFF)` |
| `progress(pct, color?, bg?)` | a bottom-row progress bar, 0–100 | `progress(64)` |

`color` defaults to white for the charts; `progress` defaults to a green fill on
a white track, the pushed-app defaults. All three take a
[palette](#palettes) in place of the colour, which paints each bar or segment by
its value: `bar_chart(vals, "Heat")`. `autoscale` (default `true`) scales the
chart to the data's own min/max; `false` fixes the range at 0–8. Both charts are
capped at 16 values, extras dropped - the same cap the pushed-app payload has.

```berry
class Cpu
  var samples

  def init()
    self.samples = []
  end

  def loop()
    self.samples.push(second() % 8)         # <- your own data source (0..7 here)
    if size(self.samples) > 16 self.samples.remove(0) end
  end

  def draw()
    clear()
    line_chart(self.samples, 0x00FF00)
    # progress() takes 0-100, so scale the 0..7 sample up to a percentage
    progress(self.samples[-1] * 100 / 7, 0x00AAFF, 0x101010)
  end
end

return Cpu()
```
<!-- shot:begin id=cpu hash=1244574b -->
![The panel showing the Cpu script](../assets/shots/scripting/cpu.png){ .shot }
<!-- shot:end -->


### Effects and overlays

`effect(name)` paints one of AWTRIX's animated backgrounds across your
canvas; `overlay(name)` paints a weather overlay on top of whatever is already
there. Same names, same look, same animation as a [pushed
app](pushed-apps.md) - the [effect and overlay lists](../reference/payload.md#effects)
are shared. Both return `false` for an unknown name, so you can tell.

| Call | Does |
|---|---|
| `effect(name)` / `effect(name, settings)` | animated background across the canvas |
| `overlay(name)` / `overlay(name, settings)` | weather overlay, drawn on top |

`settings` is an optional map: `{"speed": 0.5, "palette": "Lava", "blend": true}`,
where `palette` takes the [same names and colour stops](#palettes) as everywhere
else. Because you call them in order, the layering is yours: run `effect()` first
as the background, draw your content, then `overlay()` last.

**Build that map once, in `init()`, and keep it in a member.** Written inside
`draw()` it is a brand-new map forty times a second, which is the one habit that
turns a pretty app into a greedy one. And pass it **every** frame you want it to
apply: a bare `effect("Plasma")` runs the effect on its own default settings, so
leaving the map off on some frames makes the background flicker between two
looks.

```berry
class Clock
  var fx

  def init()
    self.fx = {"speed": 0.4, "palette": "Ocean"}
  end

  def draw()
    effect("Plasma", self.fx)
    text(6, 6, str(hour()) + ":" + str(minute()), 0xFFFFFF)
    overlay("snow")
  end
end

return Clock()
```
<!-- shot:begin id=clock hash=bffc0176 -->
![The panel showing the Clock script](../assets/shots/scripting/clock.gif){ .shot }
<!-- shot:end -->


An effect background is bright and busy. Turn the speed down and pick a darker
palette when text has to stay readable on top of it.

### Storage

Two calls, and the values survive a reboot:

```berry
store.set("count", 0)
var count = store.get("count", 0)   # 0 if never written
var maybe = store.get("count")      # nil if never written
```

Values may be integers, reals, strings, booleans, lists and maps - anything that survives a JSON round trip. Each app gets its own store, keyed by its install name; apps cannot read each other's. Handing a value to another app is what [`shared`](#talking-to-other-apps) is for.

Reads are cheap. Writes are collected in RAM and reach flash at most once every five seconds - several writes in that window become one - so a `store.set()` per second is fine and will not wear the part out. The trade is that a power cut can cost up to five seconds of writes.

**Everything one app keeps has to fit in 2 KB together.** That is roughly two thousand characters of text, or a few dozen numbers - plenty for what an app needs to remember, and not enough for a whole API response. Store the finished value, never the raw body you got it out of.

Going over does not raise an error, and that is the part worth knowing: the write is dropped, the app carries on with the value it has in memory, and the panel looks entirely correct **until the next reboot** - when the value comes back as whatever fitted last. The log says `store not saved` with the size it refused, so the Scripts tab console is where this shows up rather than on the panel.

Editing and re-saving a script keeps its store: the reloaded instance starts with exactly the keys the old one had, so `init()` and `setup()` see them straight away.

To reset a store, delete the script and upload it again - deleting takes the store with the source. Re-saving does not.

### Settings the user can change

Everything above assumes *you* are the one editing the script. Often you are not: you hand it to
someone who just wants their own city in it, or their own colour. Declaring a **setting** puts that
value in the web UI instead of the source.

A setting is one line in the header, next to `@name` and `@desc`:

```berry
# @name    Greeter
# @config  who   text   "Your name"   default="World"
# @config  tint  color  "Colour"      default=#FF8800

class Greeter
  def draw()
    clear()
    text(1, 6, store.get("who"), store.get("tint"))
  end
end

return Greeter()
```
<!-- shot:begin id=greeter hash=b2e7f7f4 -->
![The panel showing the Greeter script](../assets/shots/scripting/greeter.png){ .shot }
<!-- shot:end -->


On the **Apps** tab the row now has a **⚙** button. It opens the settings under the row, with a
text box for the name and a colour picker for the colour - the picker carries its hex value across
it, so you can read the exact colour without opening anything. Change them, hit Save, and the app
comes back with the new values.

**A setting is just a stored value.** You read it with `store.get("who")`, exactly like anything else
you keep. The default belongs in the header and nowhere else - `store.get` already answers with it on
the very first frame, so there is no need to repeat it in your code. It works the other way round
too: if your script calls `store.set("who", "Ada")`, the settings panel shows *Ada* next time
somebody opens it.

The line reads: **`# @config <key> <type> "<label>" <options…>`**. Only the key and the type are
required; without a label the key is the label.

| Type | Shows as | Extras |
|---|---|---|
| `bool` | a switch | |
| `text` | a text box | `maxlen=` (up to 256) |
| `number` | a number box | `min=` `max=` `step=` `unit=` |
| `slider` | a slider | `min=` `max=` `step=` `unit=` (0–100 if you leave them out) |
| `select` | a dropdown | `options=a,b,c` - required |
| `color` | a colour picker | |

Every type also takes `default=` and `help=` (a line of explanation under the label). A value with
a space in it goes in quotes: `default="New York"`, `help="Where you live"`.

```berry
# @config  city   text   "City"       default="Berlin" help="Where the weather comes from"
# @config  metric bool   "Celsius"    default=true
# @config  every  number "Refresh"    default=15 min=1 max=60 unit=min
# @config  mode   select "Show"       default=now options=now,today,week
# @config  bright slider "Brightness" default=80 min=0 max=100 unit=%
# @config  tint   color  "Colour"     default=#FF8800
```

A few things worth knowing:

- **A colour is a number**, the same kind `text()` and `pixel()` want. Write the default the way you
  would write it in HTML - `default=#FF8800` - and `store.get("tint")` hands you `0xFF8800` ready to
  draw with.
- **Twelve settings per script.** Anything past the twelfth is ignored.
- **A typo does not break the app.** A `@config` line AWTRIX cannot make sense of is skipped, and
  says so at the top of the settings panel - so you find out where to look instead of wondering
  why a field never turned up.
- **Saving restarts the app**, so `init()` and `setup()` run again with the new values. A running
  animation starts over; everything you stored survives.
- **Removing a setting removes its value.** Take a `@config` line out and save, and the value goes
  with it - no invisible leftovers eating the 2 KB every script has for storage. Only settings are
  cleaned up this way; anything your code put there with `store.set()` is never touched. The flip
  side: comment a `@config` line out while you are debugging, and whatever the user had chosen is
  gone at the next save.
- **[Modules](#sharing-code-between-scripts) have settings too**, and that is how several apps come
  to share one value - see [settings several apps share](#settings-several-apps-share).

### Talking to other apps

`store` is private and durable. `shared` is the other axis: public and volatile - the place apps hand each other values.

```berry
shared.set("temp", 21.5)              # publishes as prov.temp, prov being your install name
var t = shared.get("prov.temp", 0)    # read another app's value
var mine = shared.get("temp")         # a bare name reads your own
var old = shared.age("prov.temp")     # ms since it was last written, nil if absent
```

Writing takes a **bare** key and files it under your install name, so a value's origin is always exactly who wrote it and no app can quietly overwrite another's numbers. Reading takes a **qualified** name, `owner.key`. Keys may not contain dots, which is what keeps the two halves apart.

Values are scalars: integers, reals, booleans and strings. Publish `json.dump(...)` if you need structure.

`shared.set(key, nil)` erases the key, which is how a provider retracts a value it can no longer stand behind.

**Nothing here survives a reboot,** and nothing survives its author: removing a script - or re-saving it, which restarts it - takes its published keys with it. Readers see `nil` again and fall back to their default. A value that should outlive a power cut belongs in `store`, republished from `setup()`.

#### Reading a value that stopped being true

A provider can stop updating without stopping - an HTTP fetch that quietly fails, or an app that stopped with an error. The value stays where it was, and a naive reader shows an hour-old temperature with total confidence. That is what `shared.age()` is for:

```berry
def draw()
  var age = shared.age("weather.temp")
  if age == nil || age > 600000       # never published, or last written 10+ min ago
    text(0, 6, "--", rgb(80, 80, 80))
  else
    text(0, 6, str(shared.get("weather.temp")), rgb(255, 255, 255))
  end
end
```

Nothing expires on its own. The reader decides what counts as too old, because only the reader knows.

#### Finding out what is published

```berry
for k : shared.keys()          # every key, as "owner.key"
  log(k + " = " + str(shared.get(k)))
end
for k : shared.keys("weather") # only one app's
```

A dashboard app can discover its inputs at runtime this way instead of hard-coding names read out of someone else's source.

Each app may publish **8 keys** and **256 bytes** - key names plus string values; numbers cost only their key. Keys are 1–24 characters of `A–Z a–z 0–9 _ -`. `shared.set()` returns `false` when a write is refused - a malformed key, a value that is not a single number/string/bool, or no room left - and a refused write changes nothing, so the previous value survives.

### Sharing code between scripts

`shared` hands other apps a **value**. A module hands them **code**: one file of helpers that any
script can pull in with `import`, instead of the same twenty lines pasted into four apps.

A module is an ordinary script file with `@module` in the header. It ends by returning what it wants
to hand out - usually a `module` object with functions on it:

```berry
# @module
# @desc  Formatting helpers

import string

var m = module("fmt")
m.pct  = def (v) return string.format("%d%%", v) end
m.temp = def (v) return string.format("%.1f°", v) end
return m
```

Save that as `fmt` and any app can use it:

```berry
import fmt

class Battery
  def draw()
    text(0, 6, fmt.pct(battery()), rgb(0, 255, 0))
  end
end

return Battery()
```
<!-- shot:begin id=battery hash=a2a3b4fd -->
![The panel showing the Battery script](../assets/shots/scripting/battery.png){ .shot }
<!-- shot:end -->


The `import` line goes at the **top of the file**, outside the class, and every method can use the
name from there on.

**The import name is the file name.** `fmt` is imported as `fmt`, so the name has to be one `import`
can take: letters, digits and `_`, not starting with a digit. If you would rather the file were
called something else, name the import yourself - `# @module weather` in a file called
`weather-lib` is imported as `weather`. A name already taken by another module is refused, and so
are the built-in ones (`json`, `math`, `string`, `global`, `gc`, `strict`, `os`, `sys`, `time`, `debug`, `introspect`, `solidify`), so `import json` never
stops meaning the built-in `json`.

A module **must end with `return`**. Without one there is nothing to hand out, and the file installs
with an error on it exactly like a broken app.

Modules may import each other. Order does not matter: a module that imports one you have not written
yet shows an error until you save the missing file, and settles by itself the moment you do.

**Saving a module updates the apps that use it.** Every app that imports it restarts with the new
code, so the display changes as soon as you press save - there is no round of re-saving each app by
hand. Deleting a module works the same way in reverse: its users restart, fail to find it, and say
so with `ERR:` on the panel and an error in the app list. Put the file back and they recover on
their own.

A module is not an app. It never draws, never takes a turn in the rotation, and has no `draw()`,
`setup()` or `loop()` - the web UI keeps modules in their own **Modules** section on the Scripts tab.
The Apps tab lists only the modules there is something to do about there: the ones with settings, and
any that are broken. It does share everything else with the apps: the same file list, the same
editor, the same memory, and one slot each in the [script limit](#the-caps).

### Settings several apps share

One app wants your city. So does the next one. Declaring `@config city` in each of them means typing
it twice, and changing it twice when you move.

**A module has settings of its own.** Put the value where the shared code already is, and every app
that imports the module gets the same answer:

```berry
# @module  location
# @config  city text  "City"   default="Berlin"
# @config  tint color "Colour" default=#FF8800

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
<!-- shot:begin id=greeter-2 hash=dec387e0 -->
![The panel showing the Greeter script](../assets/shots/scripting/greeter-2.png){ .shot }
<!-- shot:end -->


On the **Apps** tab the module's row now has the same **⚙** an app with settings gets. Change the
city there once, and every app that imports `location` shows it.

**The module's store is the module's own.** `store.get("city")` in the file above reads
`location`'s store, not the store of whatever app happens to be using it - the two never mix, and an
app can keep a `city` of its own without colliding.

Two things make this work, and both are worth knowing:

- **Read at the top, not inside a method.** The lines above run once, when the module loads, and that
  is the only place the module is *itself* rather than a helper inside somebody else's app. A
  `store.get()` inside one of the module's functions reads the **calling app's** store, because that
  is whose code is running.
- **The value cannot go stale.** Saving a module's settings reinstalls it, and
  [that restarts every app that imports it](#sharing-code-between-scripts) - so the values read at
  the top are re-read, and the apps redraw with them. This is the same mechanism that already
  updates your apps when you edit a module's code.

Everything else is exactly as it is for an app: the same
[types and attributes](#settings-the-user-can-change), the same twelve settings, the same 2 KB
store, and deleting the module takes its settings with it.

### HTTP

```berry
http.get(url, def (body, status)
  # body is a string, or nil if no response arrived at all
  # status is the HTTP code, or 0 when nothing came back
end)
```

`http.get()` returns immediately and **never blocks the panel**. The request runs on its own task; your callback fires between frames, once, some time later.

`status` is `0` and `body` is `nil` when no response arrived - no Wi-Fi, DNS miss, refused connection, too many requests already in flight, or no answer within 30 seconds. Any real response reaches your callback, **including 4xx and 5xx**: that is where an API explains what it did not like, so `body` carries it.

Only `http://` and `https://` URLs are accepted. Response bodies are kept up to 8 KB.

A `GET` follows redirects by itself, so a shortened link or a moved endpoint reaches the right place. A `POST` or `PUT` does not, quite: depending on how the server phrases the redirect, your callback either gets the redirect response itself, or the request arrives at the new address as a `GET` with the body dropped. Neither is what you meant, so **send anything with a body straight to its final URL.**

Pace your requests. A bare `http.get()` in `loop()` fires one a second, which will run you into the eight-in-flight cap and annoy whoever runs the API.

For the first ~15 seconds after the device joins Wi-Fi - at boot or after a reconnect - `https://` requests are held back while the network services settle. They run as queued once the window ends, still within the 30-second answer window, so the first result of a poller simply arrives a few seconds later than usual. A request that finds memory too tight for a TLS connection is retried for about 20 seconds before your callback hears a failure.

#### Methods, headers and a request body

```berry
http.get(url, cb, opts)
http.post(url, body, cb, opts)
http.put(url, body, cb, opts)
http.patch(url, body, cb, opts)
http.delete(url, cb, opts)
http.request(method, url, cb, opts)   # opts may carry 'body'
```

`opts` is optional everywhere and holds `headers`, a plain map:

```berry
http.get("https://api.example.com/v1/me", / b, st -> self.on_body(b, st),
         {'headers': {'Authorization': "Bearer " + self.token,
                      'Accept': "application/json"}})

http.post("https://hooks.example.com/panel", json.dump({'state': "up"}),
          / b, st -> self.on_sent(st),
          {'headers': {'Content-Type': "application/json"}})
```

Four headers are set by AWTRIX itself and are ignored when a script supplies them: `Host`, `Content-Length`, `Transfer-Encoding` and `Connection`. Everything else is yours.

A request that breaks a rule - an unknown method, a body over 2 KB, a malformed or oversized header - never goes out. It fails the same way a network error does, immediately: `cb(nil, 0)`.

#### Picking one field out of a big answer

Some APIs answer with far more than the 8 KB AWTRIX keeps - a status
endpoint that embeds a base64 icon, a document with your one number at byte
50 000. `find` turns that cap into a search: it scans the
body as it arrives and keeps a small window starting at the first
occurrence, instead of blindly keeping the first 8 KB.

```berry
http.get(url, / b, st -> self.on_body(b, st),
         {'find': "\"followerCount\":", 'keep': 64})
```

`b` is then the `keep` bytes starting **at** the match - the needle included,
so the usual slice-after-key parsing works on it unchanged. `keep` defaults
to 256 and is capped at 8 KB; `find` is capped at 64 bytes. Because only the
window is ever stored, the document's size stops mattering: a field a
megabyte in works as well as one at the start.

If the needle never appears, the callback gets `(nil, status)` with the
**real** status code - distinguishable from transport failure's `(nil, 0)`:
the server answered, but not with the field you asked for. Headers, methods
and a request body combine with `find` freely.

#### A complete fetch

```berry
import json

class Api
  var value, ticks

  def init()
    self.value = nil
    self.ticks = 0
  end

  def on_body(body, status)
    if status == 401 log("token rejected") return end
    if body == nil return end
    var data = json.load(body)
    if !isinstance(data, map) return end
    self.value = data.find("temperature")
  end

  def loop()
    if self.ticks <= 0
      self.ticks = 60                               # once a minute is plenty
      http.get("https://example.com/api", / b, st -> self.on_body(b, st))
    end
    self.ticks -= 1
  end

  def draw()
    clear()
    if self.value != nil text(1, 6, str(self.value), 0xFFFFFF) end
  end
end

return Api()
```

The callback is `/ b, st -> self.on_body(b, st)` - a small closure that captures `self`, so the method it calls updates this instance's members. (You can also write the callback inline as `def (body, status) ... end`; either way it captures `self`.)

!!! warning "A token in a script is only as private as AWTRIX"
    Script source is stored in plain text and served back by `GET /api/v1/apps/script/<name>`. That route sits behind [HTTP authentication](../reference/http.md#authentication) - but only once you have turned it on with `authEnabled`, and the shipping default is no authentication at all. Without a login, anyone who can reach AWTRIX can read the credential you put in a script.

    A login is worth setting, and it is not encryption. Basic auth travels over plain HTTP: on an untrusted network both the password and the script body are readable in transit. The same goes for a config backup and for the flash itself.

    Outbound TLS has its own limit: `https://` traffic from a script is encrypted, but the certificate is **not checked**. That is protection against a passive eavesdropper, not against someone who controls the network path.

    So: set a login, and use a token scoped to exactly what the script reads and revocable on its own.

### Regular expressions

For the extraction jobs `string.find` gets awkward at - a value whose
surroundings vary, several occurrences, HTML without an API - `re` is there
with nothing to import, like `http` and `store`.

| Call | Does |
|---|---|
| `re.search(pattern, text)` | first match anywhere: `nil`, or a list - `[0]` the whole match, `[1..]` the groups |
| `re.match(pattern, text)` | the same, but the match must start at the first byte |
| `re.matchall(pattern, text)` | every non-overlapping match, full matches only, as a list |

```berry
var m = re.search("\"followerCount\":(\\d+)", body)
if m != nil
  self.count = num(m[1])
end

re.matchall("\\d+", "a1 b22 c333")   # ["1", "22", "333"]
```

A group that took no part in the match is `nil` in the list, and an invalid
pattern makes every call answer `nil` - nothing raises, so a typo in a
pattern shows as your no-data state, not as `ERR:` on the panel.

The supported syntax: literals, `.`, `[a-z0-9]` / `[^...]` classes,
`\d \D \w \W \s \S`, `(...)` capturing groups, `|`, `^`, `$`, and `* + ?`
with lazy variants `*? +? ??`. No `{n,m}`, no backreferences, no lookaround.
Patterns are capped at 256 bytes and 7 capturing groups.

Matching runs in time proportional to the length of the text, so a pattern
cannot hang the panel - even a pathological one like `(a*)*b` returns in a
single pass. Remember `\` is an escape in Berry strings too, so the pattern
`\d` is written `"\\d"`.

### MQTT

```berry
mqtt.publish("home/panel/status", "up")

mqtt.subscribe("sensor/#", def (topic, payload)
  # topic is the CONCRETE topic the broker delivered on
end)
```

Both are silent no-ops when MQTT is not configured, so a script with an MQTT branch still runs where no broker is set up. Topics are arbitrary - they need not sit under AWTRIX's own [MQTT prefix](mqtt.md#the-prefix).

Wildcards work. `+` matches one level, `#` matches the rest. The callback is handed the **concrete** topic the message arrived on, not the filter you subscribed with, so a subscriber to `sensor/#` can tell which sensor spoke:

```berry
class Sensor
  var last

  def init()
    self.last = "-"
  end

  def setup()
    mqtt.subscribe("sensor/+/temp", / topic, payload -> self.set(payload))
  end

  def set(payload)
    self.last = payload
  end

  def draw()
    clear()
    text(1, 6, self.last, 0xFFFFFF)
  end
end

return Sensor()
```
<!-- shot:begin id=sensor hash=a8d1204d -->
![The panel showing the Sensor script](../assets/shots/scripting/sensor.png){ .shot }
<!-- shot:end -->


Subscribing to a topic you already hold replaces the callback rather than adding a second one. There is no unsubscribe - deleting or re-saving the script drops its subscriptions.

**Eight subscriptions per script**, and a ninth is quietly ignored - no error, the callback simply never fires. If you find yourself near that number, subscribe once to `sensor/+/temp` instead of ten times to one sensor each, and tell them apart by the topic your callback is handed.

Payloads are strings in both directions; `mqtt.publish()` converts whatever you hand it. A payload you want to calculate with goes through [`num()`](#numbers) first.

### Notifications

A script draws its own app, but some things belong to AWTRIX as a whole, not to
one page in the rotation: an alert that interrupts, a wake from a blanked
screen. Those are **notifications**, and `notify()` posts one. (For a noise on
its own, without interrupting anything, use [`sound`](#sound).)

```berry
notify({"text": "Doorbell", "icon": "1234", "soundRtttl": "d:d=4,o=5,b=120:c,e,g"})
```

The argument is a map in the [notification payload
schema](../reference/payload.md) - the very object `POST /api/v1/notifications`
takes - so everything a notification can do is here: `hold`, `stack`, `wakeup`,
`sound`, `soundRtttl`, `soundLoop`, an `effect`, an `overlay`, colours, charts. The
sound setting, the queue limit and wake-from-off all apply exactly as they do to a
notification from the API. Colours may be plain integers - `rgb()`, `hsv()` and
`0xRRGGBB` literals all work:

```berry
def on_button(btn)
  if btn == "select"
    notify({"text": "ARMED", "textColor": rgb(255, 0, 0), "hold": true,
            "soundLoop": true, "soundRtttl": "s:d=8,o=5,b=200:c,g,c,g"})
  end
end
```

`notify()` returns `true` when AWTRIX accepted the notification, `false` on
a malformed payload or a full queue. This is the one script call
that reaches past your own app - use it for events, not for your regular frame.

### Sound

`sound` plays a file or a melody without dressing it up as a notification:

```berry
def on_button(btn)
  if btn == "select" sound.rtttl("beep:d=16,o=6,b=200:c") end
end
```

| Call | Does |
|---|---|
| `sound.play(name)` | plays a stored sound - a [clip](sounds.md#clips) on boards with a speaker, else a melody, or a DFPlayer track |
| `sound.rtttl(melody)` | plays an [RTTTL](sounds.md#writing-rtttl) string |
| `sound.stop()` | stops whatever is playing |
| `sound.playing()` | `true` while the device is making sound - an MP3 clip, a melody or a DFPlayer track alike |

`play`, `rtttl` and `stop` return `true` when the request was **accepted**, which
is not the same as "a file of that name exists" or "you will hear it": the
request is queued and AWTRIX answers it a frame later. If sound is switched off
device-wide, your call is accepted and nothing plays -
`settings.get("soundEnabled")` is how you find out beforehand.

`sound.playing()` lets a script wait for one sound to finish before starting
the next, or hold a frame while an alarm is still sounding. A button that plays
a clip wants exactly that, or a quick double press stacks two of them:

```berry
  def on_button(btn)
    if btn == "select" && !sound.playing()
      sound.play("doorbell")            # /CLIPS/doorbell.mp3, else the melody
    end
  end
```

Playing one after another belongs in `loop()`, never in `draw()` - the panel
must keep painting while the queue drains:

```berry
  var queue                             # ["chime", "alarm"], filled elsewhere

  def loop()
    if size(self.queue) > 0 && !sound.playing()
      sound.play(self.queue.pop(0))
    end
  end
```

Use `notify()` instead when the sound belongs to an *event* that should also
interrupt the rotation and show something. Use `sound` when you only want the
noise.

### Device settings

A script owns its canvas completely, which is freedom with a catch: nothing you
draw inherits the device-wide look. Draw white text next to a built-in app
tinted amber and yours is the one that looks wrong. `settings` is how you match
it - and how you change it:

```berry
def draw()
  text(1, 6, settings.apply_case("Zug 12"), settings.get("textColor"))
end
```

| Call | Answer |
|---|---|
| `settings.get(key)` | the configured value, or `nil` |
| `settings.set(key, value)` | `true` when the change was accepted |
| `settings.apply_case(str)` | the device's uppercase rule applied to your string |

The keys are exactly the ones
[`PATCH /api/v1/settings`](../reference/settings.md) takes - `brightness`,
`textColor`, `appDurationMs`, `useCelsius`, `time24h`, `soundEnabled`, all of
them. One vocabulary for the REST API, MQTT and your script, spelled the same
way in all three. Case matters: `textColor`, not `textcolor`.

Values arrive in the shape the API uses. Numbers are numbers, switches are
`true`/`false`, colours are `0xRRGGBB` integers - the same thing `rgb()` and
`hsv()` return - and the handful of settings the API names by word
(`timeSeparatorMode`, `dateOrder`, `transitionEffect`, …) are strings:

```berry
settings.get("brightness")          # 120
settings.get("autoBrightness")      # false
settings.get("timeSeparatorMode")   # "pulse"
settings.get("gamma")               # 1.9
settings.get("textColor")           # 16777215
```

Everything you read is what the user **configured**: `settings.get("brightness")`
is the setting, not what auto-brightness has the panel lit at this second.

The five accent colours answer `nil` when the user never picked one, and `nil`
means "fall back to `textColor`" - exactly what the built-in apps do. Written
out, that is:

```berry
var col = settings.get("temperatureColor")
if col == nil col = settings.get("textColor") end
```

`nil` is also what an unknown key answers, so a typo reads as "not set" rather
than raising.

#### Writing

`settings.set()` takes the same route a REST caller does, validation included.
It returns `false` - and changes nothing at all - for an unknown key, a value of
the wrong type, a number out of range or a word the setting does not know:

```berry
settings.set("brightness", 40)              # true
settings.set("brightness", 999)             # false, 0-255
settings.set("timeSeparatorMode", "blink")  # true
settings.set("timeSeparatorMode", "wobble") # false
settings.set("textColor", "#FF8800")        # true
settings.set("timeColor", nil)              # true, clears the accent colour
```

`true` means accepted, not applied: the change lands on the next frame, the same
way a `PATCH` from the network does, and it is persisted from there. Setting a
value that is already in place is free - it returns `true` without queueing
anything, so a `set()` in `draw()` costs nothing on the frames where nothing
changed.

The nested `scroll` and `weekdayBar` groups are not reachable by a flat key;
`get` answers `nil` for them and `set` answers `false`. Use the REST API for
those.

Be sparing. The device belongs to whoever is holding it, and a script that
quietly rewrites brightness or turns off sound is a script nobody can debug from
the web UI. Change what your app genuinely needs, change it back when you are
done, and prefer reading over writing.

AWTRIX's uppercase switch is applied to a *pushed app's* text, never to what a
script draws - the canvas is yours. `settings.apply_case()` is how you opt in,
and it cases your text exactly the way the app next to it does.

### Logging

```berry
log("fetched " + str(n) + " rows")
```

Goes to the AWTRIX log, tagged `[script:<name>]`, and shows up in the web UI console. It accepts any value, not just strings.

### Which firmware is running

```berry
log("running on " + version())        # e.g. "1.0.14"
```

`version()` is the firmware version as a string, the same one the web UI shows. Use it when a script wants to say which build it ran on, or to skip something that only newer firmware can do:

```berry
  def setup()
    if version() >= "1.0.14"
      # ...
    end
  end
```

Berry compares strings character by character, so this only reads correctly while the parts stay one digit. Comparing for equality against a known release is the safer test.

---

## A real one: weather

**`weather.ax`** is the showcase - the current temperature from Open-Meteo, which needs no API key, next to a sky symbol drawn from plain rectangles and circles. Upload it, put your coordinates in the web UI, and nobody has to open the source at all. The blocks below are the whole file, in order.

```berry
# @name    Weather
# @desc    Current temperature via Open-Meteo (no API key)
# @author  awtrix-ng
# @version 1.0
# @config  lat text "Latitude"  default="52.52" help="Decimal degrees, north positive"
# @config  lon text "Longitude" default="13.40" help="Decimal degrees, east positive"

import json

class Weather
  var url            # built once from the configured coordinates
  var temp           # last reading in °C, nil until the first success
  var label          # the finished string draw() paints, built once per fetch
  var sky            # 0 clear, 1 cloud, 2 rain, 3 snow, 4 storm
  var ticks          # loop() calls left until the next fetch
  var in_flight      # true while a request is outstanding
```

The header is the [sharing convention](#sharing-a-script), plus the two [settings](#settings-the-user-can-change) that make this file worth passing on: latitude and longitude get a **⚙** on the Apps tab, so whoever receives it puts in their own place without touching Berry.

Every member is declared with `var` at the top and given its value in `init()`. The comments are the point of the list: `sky` is a bucket rather than a raw weather code, and `label` is a finished string - both are decisions made once per fetch so that `draw()` has nothing left to work out.

```berry
  def init()
    self.url = "https://api.open-meteo.com/v1/forecast?current_weather=true" +
               "&latitude=" + store.get("lat") + "&longitude=" + store.get("lon")
    self.temp = store.get("temp")
    self.sky = store.get("sky", 0)
    self.label = self.temp == nil ? nil : self.format(self.temp)
    self.ticks = 0
    self.in_flight = false
  end
```

`init()` runs once as the instance is created, **after** the stored values are back. So the panel shows the last known reading the instant AWTRIX boots, rather than `...` until the network comes up and the first request lands. `store.get(key)` with no default yields `nil`, which is what the draw code checks for - while `lat` and `lon` always answer, because the header gave them defaults. Never repeat those defaults in the code; the header is the one place they belong.

Changing the location in the web UI restarts the app, so `init()` builds a fresh `url` from the new coordinates by itself.

```berry
  # --- fetch ---

  def format(t)
    var half = t >= 0 ? 0.5 : -0.5
    return str(int(t + half)) + "°"
  end

  def classify(code)
    if code >= 95 return 4 end
    if code >= 85 return 3 end
    if code >= 80 return 2 end
    if code >= 71 && code <= 77 return 3 end
    if code >= 51 return 2 end
    if code >= 2  return 1 end
    return 0
  end
```

`int()` truncates towards zero, so the rounding half has to follow the sign - otherwise −3.4 °C displays as −3 in one direction and −2 in the other. `classify()` folds the WMO weather codes into the five buckets the panel can actually tell apart. Both run once per fetch, never per frame.

```berry
  def on_body(body, status)
    self.in_flight = false
    if body == nil return end

    var data = json.load(body)
    if !isinstance(data, map) return end
    var cw = data.find("current_weather")
    if !isinstance(cw, map) return end
    var t = cw.find("temperature")
    if t == nil return end

    self.temp = t
    self.sky = self.classify(int(cw.find("weathercode", 0)))
    self.label = self.format(t)
    store.set("temp", t)
    store.set("sky", self.sky)
  end
```

Four habits worth copying. **One `nil` check covers every failure** - offline, refused, garbage, or nothing at all within thirty seconds. This endpoint needs no credentials, so the `status` argument is ignored here; a script behind a token would branch on it. **`find()` rather than `[]`**, because indexing a map with a key that is not there raises, and an API that changes its shape should not leave your app stuck broken. **The store is written only once the data is good**, so a bad response cannot poison the value that survives the next reboot. And **the display string is built here**, not in `draw()`.

`json.load()` is safe on this endpoint because the answer is a few hundred bytes. An API that replies with kilobytes wants [`find` and `keep`](#picking-one-field-out-of-a-big-answer) instead, so only the interesting window is ever kept.

`on_body` updates the instance members declared at the top of the class. They keep their values between calls, and are private to this app. Passing `/ b, st -> self.on_body(b, st)` to `http.get()` is what lets the async callback reach them.

```berry
  def loop()
    if self.ticks <= 0
      self.ticks = 300
      if !self.in_flight
        self.in_flight = true
        http.get(self.url, / b, st -> self.on_body(b, st))
      end
    end
    self.ticks -= 1
  end
```

`loop()` runs whether or not the app is on screen, so this keeps polling in the background and the data is already fresh when the rotation arrives. 300 calls at roughly one a second is close enough to five minutes for weather. The `in_flight` guard means a slow network cannot stack up requests against the eight-in-flight cap - a stalled request delays the next attempt instead of adding to it.

```berry
  # --- draw ---

  def glyph()
    if self.sky == 0
      circle_fill(3, 3, 2, 0xFFAA00)
      pixel(3, 0, 0xFF6600) pixel(0, 3, 0xFF6600)
      pixel(6, 3, 0xFF6600) pixel(3, 6, 0xFF6600)
      return
    end

    circle_fill(2, 4, 1, 0x8899AA)
    circle_fill(5, 3, 2, 0x8899AA)
    rect_fill(1, 4, 6, 2, 0x8899AA)

    if self.sky == 2
      pixel(2, 7, 0x3388FF) pixel(4, 6, 0x3388FF) pixel(6, 7, 0x3388FF)
    elif self.sky == 3
      pixel(2, 7, 0xCCEEFF) pixel(4, 6, 0xCCEEFF) pixel(6, 7, 0xCCEEFF)
    elif self.sky == 4
      pixel(4, 6, 0xFFDD00) pixel(3, 7, 0xFFDD00)
    end
  end
```

The symbol is drawn rather than loaded, in the leftmost eight columns. That is deliberate: [`icon()`](#panel-and-drawing) can only draw what its owner has installed, so a shared script that names an icon arrives broken on most devices. Rectangles and circles always work, cost no memory, and the cloud is reused by three of the five states with a couple of pixels changed underneath it.

```berry
  def draw()
    clear()

    if self.label == nil
      text(1, 6, "...", 0x666666)
      return
    end

    self.glyph()

    var c = 0x00FF00
    if self.temp >= 28
      c = 0xFF4000
    elif self.temp <= 0
      c = 0x00AAFF
    end
    text(9 + (width() - 9 - text_ink_width(self.label)) / 2, 6, self.label, c)
  end
```

`draw()` renders **only from cached state**. It never fetches, never parses and never waits - at forty frames a second it cannot afford to, and it does not need to, because `loop()` has already done the work. This is the shape almost every network-backed script wants.

The rest is what makes it readable across a room. Nothing has arrived yet is a dimmed `...`, never a blank panel. Colour carries the meaning there is no room to spell out: blue at or below freezing, red from 28 °C, green in between. And the number is centred in the columns the symbol left over - measured with `text_ink_width()`, never guessed, because `-13°` is wider than `5°`.

```berry
  def on_button(btn)
    if btn == "select"
      self.ticks = 0
    end
  end
end

return Weather()
```

Select forces a refresh on the next `loop()` call. Left and right are left alone, and go on rotating apps as usual. The class closes and the file ends with `return Weather()` - the instance AWTRIX runs.

---

## Sharing a script

A script is one file, and the file is the whole thing - no manifest, no build step. Send it, and the person on the other end pastes it in. The one thing that travels with it is a [module](#sharing-code-between-scripts) it imports, which is another file to send along.

The convention that makes that pleasant is a comment header on the first lines:

```berry
# @name    Weather
# @desc    Current temperature via Open-Meteo
# @author  blueray
# @version 1.0
```

Four keys, all optional, all just comments - the script runs identically without them. What they buy you is that the web UI reads them for its list and its export, so a shared script arrives with a description and an author rather than a filename.

Two more keys change what the file *is* rather than how it is presented: [`@headless true`](#running-without-ever-being-shown) for a script that never draws, and [`@module`](#sharing-code-between-scripts) for one that other scripts import.

The header is also where a script that is *meant* to be shared earns its keep. Every value the next person would otherwise have to hunt down in your source - a city, a colour, a refresh interval - can be an [`@config` line](#settings-the-user-can-change) instead, and then it is a field in the web UI. That is the difference between a script somebody can use and one they have to read first.

Only the **leading** comment block is read; the scan stops at the first line of real code, so an `# @name` inside a function is ordinary source. Unknown keys are ignored.

The **install name** is separate from `@name`. It is what you called the script when you saved it, it is the app's id in the rotation, and it must match `[A-Za-z0-9_-]{1,32}` because it becomes a filename. `@name` is only ever presentation.

### Backing up and restoring over the API

A script is an app, so it lives in the app collection. The web UI is a client of these routes; a backup script can be another:

```bash
# read one script back, verbatim
curl http://<awtrix-ip>/api/v1/apps/script/Weather

# install or replace (the body is Berry source, not JSON)
curl -X PUT http://<awtrix-ip>/api/v1/apps/script/Weather \
  -H 'Content-Type: text/plain' --data-binary @weather.ax

# every app on the device, with origin, compile state and metadata
curl http://<awtrix-ip>/api/v1/apps

# the settings a script offers, with their current values
curl http://<awtrix-ip>/api/v1/apps/Weather/config

# change one of them (the app restarts with the new value)
curl -X PATCH http://<awtrix-ip>/api/v1/apps/Weather/config \
  -H 'Content-Type: application/json' -d '{"lat":"48.14"}'
```

Because `GET` returns the source verbatim and `PUT` takes it verbatim, the pair round-trips: backing up every script you have is a shell loop.

One thing to check on a `PUT`: **a script that fails to compile is still a successful install.** You get `200`, the app appears in the rotation, and the `error` field in the answer carries the compiler's message and the line it failed on. A typo otherwise looks exactly like a working upload.

Every route, field and status code: [HTTP API - Scripts](../reference/http.md#scripts).

---

## Limits, and what breaks

A script runs on a clock radio, next to the code that keeps the clock running. The limits below are what make "a broken script cannot take AWTRIX down" true rather than aspirational.

### The instruction limit

**Every entry into script code is capped at 200 000 interpreter instructions.** That covers one `draw()`, one `loop()`, one button press, one HTTP callback - each starts again at the full 200 000.

Overrun it and the script stops with `runtime_error: instruction limit exceeded` and stays broken until you replace it, exactly as any other unhandled error would. Nothing else on AWTRIX notices, and a `try`/`except` around a runaway loop does not defeat it.

In practice 200 000 instructions is a great deal of drawing. You will meet this limit by writing an accidental infinite loop, not by drawing a busy frame.

### `ERR:` on the panel

Any error your script does not handle leaves it **stuck broken**:

- the panel shows `ERR:<name>` in red whenever the app comes round,
- the web UI shows the error text against the script - and, when the failure has
  a source position, marks that line in the editor and scrolls to it,
- and every other script keeps running untouched.

A compile error reliably carries a line number. A *runtime* error usually does
not: what you get there is the message and the lifecycle hook it raised in
(`setup`, `draw`, …), which is generally enough to find it.

It stays that way - a broken script is not retried each frame - until you save it again. Saving resets the state and starts a fresh interpreter.

A script that does not even compile still **installs**. The source is stored, the app appears in the rotation, and it shows `ERR:` with the compiler's message.

### The caps

Every cap a script runs under - source size, installed count, memory, HTTP, MQTT, store and
shared state, with what happens when you reach each one - is tabulated under **Scripting** in
[Limits](../reference/limits.md#scripting). Two of them shape how you write scripts: the
instruction limit above, and how much memory the scripts on your AWTRIX share.

**How many scripts fit.** `scriptLimit` sets the number of installed scripts - [modules](#sharing-code-between-scripts) included, since they take the same memory - 16 out of the box
and adjustable from 0 to 32 under **System → Advanced** in the web UI (or over the API - see
[System configuration](../reference/system.md)). It takes effect at once, no reboot. Lowering it
below the number of scripts you already have **removes nothing**: those keep running and stay
editable, and only a *new* name is refused until deleting scripts brings the count back under.

**How much memory they share.** On a board without PSRAM - any 4 MB ESP32 - every
script shares about 96 KB with the icon decoder, the pushed apps holding their content, and the
room an HTTPS handshake needs. A handful of scripts is comfortable; a handful of scripts *and* a
long list of pushed apps is where installs start being refused. An **ESP32-S3 with PSRAM** raises
that ceiling to megabytes on the same build, with nothing to configure - AWTRIX decides at
boot. If you want to push scripting hard, that is the board to be on.

### "Not enough free memory to compile"

A `507` with this message means the install was refused because compiling it
right then would have been unsafe - not that the script is too big and not that
you are out of slots. Compiling is the expensive moment, not running: an install
briefly needs roughly the source size again in free memory, and AWTRIX still
has to have enough left afterwards to run what it installed.

A close relative is `507` with **`heap too fragmented to compile`**: the memory
is there, but not in one piece.

Three things clear either one:

- **Reboot.** Free memory breaks up as apps come and go; a reboot returns the
  largest usable block to full size. Worth trying before you shorten anything.
- **Delete a script you are not using.** Freeing an installed app frees its memory.
- **Shorten the script, or write it as fewer, longer functions.** A file full of
  one-line helpers costs more to install than the same code in a few long
  methods.

Re-saving an existing script is judged more leniently than a new install, so when
new scripts keep being refused, editing what is already there still works.

---

## The sandbox

Scripts cannot reach the filesystem, the process, or arbitrary memory. Each script compiles in its own private scope, so its class and top-level names belong to it alone.

Available, because they are pure computation:

| Module | For |
|---|---|
| `string` | formatting, splitting, searching |
| `json` | `json.load()` / `json.dump()` |
| `math` | the usual, plus `math.rand()` |
| `gc` | `gc.collect()`, `gc.allocated()` |
| `strict` | opt-in stricter name resolution |
| `global` | the script's own globals |

Unavailable, and `import` raises if you try: **`os`** (files, `chdir`, `system()`, `exit()`), **`sys`** (module loading), **`time`** (use the time functions above instead), **`debug`**, **`solidify`** and **`introspect`**. The builtins `open` and `input` are disabled too - `open` writes real files, and `input` would block the render loop on a read that never returns.

### One interpreter, many scripts

- **Failures are isolated.** Every hook runs with its own [instruction allowance](#the-instruction-limit), so one app raising, looping forever or running out of instructions breaks only itself. Every other script keeps running.
- **Names are isolated.** App A cannot see app B's class, members or callbacks, and two apps may share a class name freely. What one app deliberately [publishes](#talking-to-other-apps) is readable by all of them, but only ever writable by its owner, and what a [module](#sharing-code-between-scripts) returns is readable by every script that imports it.
- **Memory is shared.** Every script draws from the same pool, so an app that allocates without bound puts pressure on its neighbours.

The sandbox protects AWTRIX and stops *accidental* interference. It is not a defence against a script you chose to install: that script can publish to any MQTT topic and fetch any URL. Treat one you did not write the way you would treat any other program you run.

---

## Related

- [Build an app with AI](ai-prompt.md) - this API as a system prompt, for when you would rather describe an app than write one
- [Icons & assets](icons.md) - what `icon()` can draw, and how to upload more
- [App & notification payload](../reference/payload.md) - the schema `notify()`, `effect()` and the charts mirror
- [Pushed apps](pushed-apps.md) - the simpler way to put your own content on the panel, when something
  outside AWTRIX can keep it fed
- [MQTT automation](mqtt.md) - AWTRIX's own topics, alongside the arbitrary ones a script may use
- [Limits](../reference/limits.md#scripting) - every cap a script runs under, in one table
- [Simulator](../advanced/simulator.md) - iterate without hardware
