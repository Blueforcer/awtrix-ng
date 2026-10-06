# Tutorial 2: Give it a memory

This tutorial turns the picture from [tutorial 1](first-draw.md) into an app that changes by
itself.

At the end of tutorial 1 the app draws sixteen bars from numbers you typed into the source.
Here you add three things:

- The numbers change on their own.
- They survive a reboot.
- The user picks the color in the web UI.

The network comes in [tutorial 3](real-data.md).

---

## The lifecycle

An app is not a program that runs from top to bottom. It is a set of methods that AWTRIX
calls at different moments. This tutorial needs three of them:

| Method | When AWTRIX calls it |
|---|---|
| `init()` | once, when the app starts. Give every member its first value here. |
| `loop()` | about once a second, also while the app is not shown. Do the work here. |
| `draw()` | about forty times a second while the app is shown. Paint here, and only here. |

There are a few more, for example for the buttons or for skipping a turn. You add them only
when you need them. [The lifecycle](../guides/scripting/index.md#the-lifecycle) in the scripting
guide lists them all.

The most important rule:

!!! note "`loop()` does the work. `draw()` paints what `loop()` left behind."
    `draw()` runs about forty times a second. Anything it calculates, it calculates forty
    times a second. `loop()` runs about once a second, and it keeps running while your
    app is not shown. So the data is ready when the app comes back to the display.

That is why the app now gets a `loop()`, and why `draw()` stays as short as it is.

---

## Make the numbers move

Real numbers arrive in the next tutorial. For now, the app makes up random numbers, so you
can test everything else.

The forecast moves on by one hour at a time: remove the oldest value at the front, add a new
one at the end.

<!-- panel motion=10 alt="The first ten seconds: a bar appears at the right edge, and five seconds later the next one" -->
```berry
# @name Rain
# @desc Chance of rain, hour by hour

import math

class Rain
  var hours
  var ticks

  def init()
    self.hours = []
    for i : 0 .. 15
      self.hours.push(0)
    end
    self.ticks = 0
  end

  def loop()
    if self.ticks <= 0
      self.ticks = 5
      self.hours.remove(0)
      self.hours.push(math.rand() % 101)
    end
    self.ticks -= 1
  end

  def draw()
    clear()
    var h = height()
    var w = width() / size(self.hours)
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * w, h - bar, w, bar, hsv(208, 100, clamp(v, 25, 90)))
      end
    end
  end
end

return Rain()
```

Save it and watch. The list starts as sixteen zeros, and zeros draw nothing. So the chart
fills from the right, one new bar every five seconds, and each bar moves left as the next
one arrives. The picture shows the first ten seconds. After about a minute and a half the
chart is full.

`import math` gives the app `math.rand()`, which returns a random whole number.
`math.rand() % 101` turns it into a number from 0 to 100.

`draw()` did not change at all. It still reads `self.hours` and paints it. Aim for this in
every app: `draw()` does not need to know where its data comes from.

---

## Counting seconds

`loop()` runs about once a second, so a counter is the simplest possible timer:

```berry
  def loop()
    if self.ticks <= 0
      self.ticks = 5          # do it again in roughly five seconds
      # the work goes here
    end
    self.ticks -= 1
  end
```

`loop()` is not a precise clock. For "refresh every five minutes" it is precise enough, and
it needs only one number.

When you need the time of day, or smooth animation, AWTRIX has clock calls for that. They
are listed under [Time](../guides/scripting/time-buttons-sensors.md#time) in the scripting guide.

---

## Keep the list from growing

`self.hours.remove(0)` before every `push()` is important. Without it the list grows by one
value every five seconds and never stops. All scripts on the device share one block of
memory, so a list that grows forever finally makes other apps fail too.
[Keeping scripts small](going-easy-on-memory.md) has more rules like this.

Our app removes first and then pushes, because its list is always full. When yours
starts empty and fills up, push first and trim afterwards:

```berry
    self.hours.push(v)
    if size(self.hours) > 16
      self.hours.remove(0)
    end
```

Either way, trim **in place** with `remove()` rather than building a new list.

Why sixteen: the built-in `bar_chart()` and `line_chart()` calls take at most sixteen values
and drop the rest, and sixteen bars fit well <!-- only esp32 esp32-s3 -->even on a 32 pixel display<!-- /only --><!-- only tc002 -->on the 52 pixel display<!-- /only -->. Keep only what you
draw.

---

## Survive a reboot

Right now a power cut leaves the chart empty until the app has added sixteen new values.
`store` fixes that. It is a small storage for named values, private to your app, that
survives a reboot.

```berry
    store.set("hours", self.hours)
    var saved = store.get("hours")     # nil if it was never written
```

`nil` is Berry's word for "no value". You can store numbers, strings, booleans (`true` or
`false`), lists and maps. Store finished values, never a raw answer from a web service.

The store is already filled when `init()` runs. So an app can show its last values right
after a boot, instead of an empty display:

```berry
  def init()
    self.hours = store.get("hours")
    if self.hours == nil
      self.hours = []
      for i : 0 .. 15
        self.hours.push(0)
      end
    end
    self.ticks = 0
  end

  def loop()
    if self.ticks <= 0
      self.ticks = 5
      self.hours.remove(0)
      self.hours.push(math.rand() % 101)
      store.set("hours", self.hours)
    end
    self.ticks -= 1
  end
```

Writing on every update is fine. AWTRIX saves the store at most once every five seconds, so
even a `store.set()` every second does no harm. A power cut can lose the last few seconds of
writes.

Only store data you have checked. A bad value in the store is still there after a reboot.

---

## Let the user choose the color

**Every value the user might want to change belongs in a `# @config` line, not in the
code.** Then other people can use your app without editing it.

A `@config` line creates a field in the web UI. You find it under the **Apps** tab: press
**⚙** on your app's row.

```berry
# @name    Rain
# @desc    Chance of rain, hour by hour
# @version 1.0
# @config  tint  color  "Bar color" default=#0088FF
# @config  every number "Refresh"   default=15 min=1 max=60 unit=min
```

Each line names the setting (`tint`), its type (`color`), the label the user sees, and a
default value. Four rules:

**Read a setting with `store.get(key)`.** Settings and stored values live in the same
place, so the same call reads both.

**Do not repeat the default in code.** Write `store.get("tint")`, not
`store.get("tint", 0x0088FF)`. The default from the `@config` line is already there on the
first frame.

**Put the lines in the header, above any code.** AWTRIX stops reading `@` lines at the
first line that is neither empty nor a comment. A `@config` below `import math` is
ignored.

**Saving a setting restarts the app**, so `init()` runs again. Calculate everything that
depends on a setting in `init()`.

A `color` setting gives you a number, which the drawing calls can use directly.

!!! warning "Removing a `@config` line removes its value"
    When you save the script without one of its `@config` lines, AWTRIX deletes the value
    the user chose for that setting. Putting the line back brings back the default, not the
    user's choice. So do not comment out a `@config` line to try something.

---

## The finished script

```berry
# @name    Rain
# @desc    Chance of rain, hour by hour
# @version 1.0
# @config  tint  color  "Bar color" default=#0088FF
# @config  every number "Refresh"   default=15 min=1 max=60 unit=min

import math

class Rain
  var hours       # 16 percentages, oldest first
  var tint        # bar color, from the user's settings
  var period      # loop() calls between refreshes
  var ticks       # counting down to the next one

  def init()
    self.hours = store.get("hours")
    if self.hours == nil
      self.hours = []
      for i : 0 .. 15
        self.hours.push(math.rand() % 101)
      end
    end
    self.tint = store.get("tint")
    self.period = store.get("every") * 60
    self.ticks = 0
  end

  def loop()
    if self.ticks <= 0
      self.ticks = self.period
      self.hours.remove(0)
      self.hours.push(math.rand() % 101)
      store.set("hours", self.hours)
    end
    self.ticks -= 1
  end

  def draw()
    clear()
    var h = height()
    var w = width() / size(self.hours)
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * w, h - bar, w, bar, self.tint)
      end
    end
  end
end

return Rain()
```

Three details:

`every` is in minutes, and `loop()` runs about once a second, so `store.get("every") * 60`
is the number of `loop()` calls between two refreshes.

On the first run, the list starts with sixteen random values instead of zeros. Otherwise
the display would show only one bar for the first fifteen minutes, because `loop()` adds one
value per refresh and zeros draw nothing. That is fine while the data is made up. In
[tutorial 3](real-data.md) the numbers are real, so the app starts with zeros and draws a
placeholder until the first real answer arrives.

The bars have one color. `hsv()` gave every bar its own brightness. Here the user picks the
color, so the app uses exactly that color.

With `every` set to 15 minutes the chart moves too slowly to watch. Set it to 1 while you
test.

---

## What you learned

- `init()` sets up, `loop()` does the work once a second, `draw()` only paints.
- `loop()` keeps running while the app is not shown, so the data is ready when it
  comes back.
- A counter in `loop()` is a good timer.
- Trim growing lists in place. Sixteen values fit the charts and the display.
- `store` survives reboots, and is already filled when `init()` runs.
- Anything the user might want to change is a `# @config` line read with
  `store.get(key)`, never a constant in the source.

## Next

[**Tutorial 3: Feed it real data**](real-data.md) replaces `math.rand()` with a real forecast. You
learn how to fetch data without freezing the display, what to show before the first answer
arrives, and how to take only the values you need from an answer.

## Related

- [Scripting guide](../guides/scripting/index.md) – all app methods and the `@config` reference
- [Settings](../reference/settings.md) – the device settings a script can also read
- [Limits](../reference/limits.md#scripting) – every limit in one table
