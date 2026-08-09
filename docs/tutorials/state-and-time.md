# 2. Give it a memory

At the end of [tutorial 1](first-draw.md) the app draws sixteen bars from sixteen
numbers you typed into the source. It looks right and it is completely inert.

This tutorial gives it the three things that separate a picture from an app. The
numbers start changing on their own. They survive a reboot. And the colour stops being
your decision and becomes the user's.

No network yet. That is [tutorial 3](real-data.md).

---

## The lifecycle

An app is not a program that runs from top to bottom. It is a set of methods AWTRIX
calls at different moments, and knowing which is which is most of the craft.

| Method | When it runs | May it draw? |
|---|---|---|
| `init()` | once, as the instance is created | no |
| `setup()` | once, right after the app loads | no |
| `loop()` | about once a second, on screen or not | no |
| `draw()` | every frame, roughly 40 times a second, while on screen | **yes** |
| `on_show()` | the app has just been rotated in | no |
| `on_hide()` | the app has just been rotated out | no |
| `on_button(btn)` | a button was pressed while the app is on screen | no |
| `should_show()` | the rotation arrived; `false` skips the app this turn | no |
| `duration()` | the rotation arrived; return milliseconds to override the dwell time | no |

Define only the ones you need. Every method costs memory for as long as the app is
installed, so an app with three good methods beats an app with ten small ones.

`init()` and `setup()` both run once and it is not obvious which to use.
**`init()` gives members their starting values**, and it is the only one of the two that
Berry itself calls, as the class is constructed. Stored values and `@config` settings are
already restored by then, so it can read them. **`setup()` runs a moment later, once the
app is wired into the device**, and it is where anything that reaches outside belongs:
the first HTTP request, an MQTT subscription, a log line. When in doubt, use `init()`.

One rule matters more than the rest of the table put together:

!!! note "`loop()` does the work. `draw()` paints what `loop()` left behind."
    `draw()` runs about forty times a second. Anything it computes, it computes forty
    times a second. `loop()` runs about once a second, and it keeps running while your
    app is off screen, which is the whole point: the data is ready and waiting when the
    rotation comes back to you.

That rule is why this app is about to grow a `loop()` and why `draw()` is going to stay
exactly as short as it is now.

---

## Make the numbers move

Real numbers arrive in the next tutorial. For now, let the app invent them, so the
plumbing is in place and testable.

The forecast shifts along by one hour at a time: drop the oldest value off the front,
add a new one at the end.

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
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * 2, h - bar, 2, bar, hsv(208, 100, clamp(v, 25, 90)))
      end
    end
  end
end

return Rain()
```

Save it and watch. The list starts as sixteen zeros and zeros draw nothing, so what you
see is the chart filling from the right, one new bar every five seconds, each one
shuffling left as the next arrives. Give it a minute and a half to fill up completely.

`draw()` did not change at all. It still reads `self.hours` and paints it. That is what
you are aiming for in every app you write: a `draw()` that knows nothing about where its
data came from.

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

Roughly is the honest word. `loop()` is not a precise clock, and it does not try to be.
For "refresh every five minutes" that is exactly the right amount of precision, and it
costs one integer.

When you need real time, there are two different clocks and they answer different
questions:

| Call | Answers |
|---|---|
| `now_ms()` | milliseconds since the device booted |
| `epoch_ms()` | milliseconds since 1970, so the actual date and time |
| `hour()` `minute()` `second()` `day()` `month()` `year()` `weekday()` | the local wall clock, split up |

`now_ms()` is the one for animation, because it advances evenly whether or not frames
do. A sweep from 0 to 1 every two seconds is `(now_ms() % 2000) / 2000.0`. Never animate
by incrementing a counter in `draw()`, because frames are not evenly spaced and the
animation will drift.

!!! warning "The clock is not ready at boot"
    All the wall-clock calls return `-1` in a `setup()` that runs while the device is
    starting up, because AWTRIX reinstalls scripts before it has fetched the time.
    Guard with `if hour() >= 0`, or simply do the work in `loop()`, which always runs
    with the clock available.

---

## Keep the list from growing

`self.hours.remove(0)` before every `push()` is doing quiet but important work. Without
it the list grows by one value every five seconds and never stops, and every script on
the device shares one pool of memory, so a list that grows forever eventually takes the
whole panel down with it. [Going easy on memory](going-easy-on-memory.md) has the
measurements.

Our app removes first and then pushes, because its list is always full. When yours
starts empty and fills up, push first and trim afterwards:

```berry
    self.hours.push(v)
    if size(self.hours) > 16
      self.hours.remove(0)
    end
```

Either way, trim **in place** with `remove()` rather than building a new list.

Sixteen is not an arbitrary number here. The built-in `bar_chart()` and `line_chart()`
calls take at most sixteen values and drop the rest, and sixteen bars is what fits on a
32 pixel panel at a sensible width. Keeping only what you draw is a good habit
generally, and here the shape of the hardware happens to agree with it.

---

## Survive a reboot

Right now a power cut leaves the panel blank until the app has ticked sixteen times.
`store` fixes that. It is a small key-value store, private to your app, that survives a
reboot.

```berry
    store.set("hours", self.hours)
    var saved = store.get("hours")     # nil if it was never written
```

Anything that survives a JSON round trip works: numbers, strings, booleans, lists and
maps. The budget is **2 KB serialised per app**, which is plenty for a finished value
and nowhere near enough for a raw API response. Store what you worked out, never what
you worked it out from.

The useful detail is the timing. The store is restored *before* `init()` runs, so an app
can show its last known value the instant the device boots instead of a placeholder:

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

Writing on every update sounds expensive and is not. Writes are collected in RAM and
reach flash at most once every five seconds, so a `store.set()` per second is fine.
What is not fine is writing data you have not checked yet, because a bad value that
reaches the store outlives the reboot that would otherwise have cleared it.

---

## Let the user choose the colour

Here is the rule that separates an app you wrote for yourself from an app you can hand
to somebody: **every value the user might reasonably want to change belongs in a
`# @config` line, not in the source.**

A `@config` line turns a stored value into a real field in the web UI, found under the
**Apps** tab, the `⋯` menu on your app's row, then **Settings**.

```berry
# @name    Rain
# @desc    Chance of rain, hour by hour
# @version 1.0
# @config  tint  color  "Bar colour" default=#0088FF
# @config  every number "Refresh"    default=15 min=1 max=60 unit=min
```

Four things to know, and they cover almost every mistake people make with these:

**Read a setting with `store.get(key)`.** There is no separate call. Settings and stored
values live in the same place, which is why the same function reads both.

**Do not repeat the default in code.** Write `store.get("tint")`, not
`store.get("tint", 0x0088FF)`. The declared default is already the answer on the very
first frame, and a second copy in the source is one more thing to keep in sync.

**Put the lines in the header, above any code.** The parser stops reading tags at the
first line that is neither blank nor a comment, so a `@config` below your `import math`
is never seen at all.

**Saving a setting restarts the app**, so `init()` runs again. That is exactly where
anything derived from a setting belongs.

A `color` setting hands you an integer, which is precisely what the drawing calls want,
so it goes straight through with nothing in between.

!!! danger "Never comment out a `@config` line to test something"
    Removing the line deletes the stored value. The user's choice is gone at the next
    save, with no warning and no way back.

---

## The finished script

```berry
# @name    Rain
# @desc    Chance of rain, hour by hour
# @version 1.0
# @config  tint  color  "Bar colour" default=#0088FF
# @config  every number "Refresh"    default=15 min=1 max=60 unit=min

import math

class Rain
  var hours       # 16 percentages, oldest first
  var tint        # accent colour, from the user's settings
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
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * 2, h - bar, 2, bar, self.tint)
      end
    end
  end
end

return Rain()
```

Two details in there are worth pausing on.

The first run seeds the list with sixteen random values rather than sixteen zeros.
Without that, the panel would show a single bar for the first fifteen minutes, because
`loop()` adds one value per refresh and zeros draw nothing. Seeding is the right call
while the data is invented. In [tutorial 3](real-data.md), where the numbers mean
something, seeding with fiction would be dishonest, so the app starts with zeros and
draws a placeholder until the first real answer arrives.

Note what happened to the colour. `hsv()` gave every bar its own brightness, which was
a nice effect and is no longer ours to choose. Once the user picks a colour, the app
should paint that colour. If you want both, a second `@config` line asking whether to
shade by value is the honest way to offer it, rather than quietly overriding what they
picked.

With `every` set to 15 minutes the chart now moves too slowly to watch, which is
correct for a weather app and inconvenient for testing. Set it to 1 while you work.

---

## What you learned

- `loop()` runs once a second whether or not the app is visible, and that is where work
  belongs. `draw()` only paints.
- A counter in `loop()` is a perfectly good timer. `now_ms()` is for animation, because
  it advances evenly when frames do not.
- Lists that grow need trimming in place, and sixteen values is the number the charts
  and the panel both agree on.
- `store` survives reboots, holds 2 KB per app, and is restored before `init()` runs.
- Anything the user might want to change is a `# @config` line read with
  `store.get(key)`, never a constant in the source.

## Next

[**3. Feed it real data**](real-data.md) throws away `math.rand()` and fetches a real
forecast. That brings in HTTP without blocking the panel, pulling values out of a
response without drowning in it, and what an app should show in the seconds before its
first answer arrives.

## Related

- [App scripting](../guides/scripting.md) for the full lifecycle and the `@config` reference
- [Settings](../reference/settings.md) for the device settings a script can read alongside its own
- [Limits](../reference/limits.md#scripting) for every cap in one table
