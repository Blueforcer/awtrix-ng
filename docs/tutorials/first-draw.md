# 1. Draw something

This is the first of three tutorials that build one app together. By the end of the
third, your panel shows the chance of rain for the next sixteen hours, fetched from a
real weather service, with the location set from the web UI.

Right now we build none of that. We build a picture.

| Tutorial | What it adds |
|---|---|
| **1. Draw something** | pixels on the panel, from numbers you type in yourself |
| [2. Give it a memory](state-and-time.md) | state, a clock, and a setting the user can change |
| [3. Feed it real data](real-data.md) | a real forecast over the network |

You need an AWTRIX in front of you, or the [simulator](../advanced/simulator.md), which
runs scripts just as well and reloads faster.

---

## Make the script

Open the web UI, go to the **Scripts** tab, and create a script called `Rain`.

Paste this in and press **Save**, or `Ctrl-S`:

```berry
# @name Rain
# @desc Chance of rain, hour by hour

class Rain
  def draw()
    clear()
    text(1, 6, "rain", 0x0088FF)
  end
end

return Rain()
```

Your app has joined the **rotation**, which is the queue of apps taking turns on the
panel. Wait a few seconds and it comes round, or press the right button on the device
to skip ahead to it. The panel says **rain** in blue.

That is a complete app. Three things make it one:

**It is a class.** Everything the app owns lives inside it. Two people can both write
`class Rain` without ever colliding, because each script gets its own private scope.

**It has a `draw()`.** This is the only method an app must have. AWTRIX calls it about
forty times a second while your app is the one on screen, and whatever it paints is
the frame.

**It ends with `return Rain()`.** That hands AWTRIX the instance to run.

!!! tip "The editor knows the API"
    Built-in calls are highlighted in their own colour, and **Ctrl-Space** completes
    them. A misspelled `pixel` stays plain, so you see it before you save. The list
    always matches the firmware you are actually talking to.

### When it goes wrong

Nothing you do here can harm the device. A broken script breaks only itself: the panel
shows `ERR:` in red when its turn comes, every other app keeps running, and saving the
script again clears it. The message is waiting next to your script in the **Scripts**
tab.

These are the three you will actually meet, with the exact words AWTRIX uses:

| Message | What happened |
|---|---|
| `script must end with 'return YourApp()'` | the last line is missing |
| `no draw() method` | the class has no `draw()`, so there is nothing to paint |
| `syntax_error: unexpected token …` | usually a missing `end` |

The third one deserves a warning. Berry closes every `if`, `for`, `while`, `def` and
`class` with `end`, and a missing one is the most common mistake there is. The line
number in the message is where the parser finally gave up, which is *after* the line you
actually got wrong, sometimes several lines after. Look upwards from it, not at it.

Errors that only show up while the app is running arrive the same way. `"x" + 5` raises
`type_error: unsupported operand type(s) for +: 'string' and 'int'`, because Berry will
not join a number to a string for you. Write `"x" + str(5)`. That one catches everybody
at least once.

---

## How the panel is addressed

The panel is 32 pixels wide and 8 tall. `x` runs from 0 on the left, `y` runs from 0 at
the **top**, so a larger `y` is further down.

These two lines go inside `draw()`, next to the `text()` call you already have:

```berry
    pixel(0, 0, 0xFF0000)                        # top left
    pixel(width() - 1, height() - 1, 0x00FF00)   # bottom right
```

Ask `width()` and `height()` rather than writing 32 and 8. Some builds run a different
panel, and a script that measures adapts to them for free. It costs you nothing today
and saves you a rewrite later.

Anything you draw outside the panel is quietly clipped. Drawing at `x = 500` is not an
error, it just does not appear. That is a friendlier rule than it sounds, because it
means a chart that runs long cannot crash your app.

One thing surprises everyone once: **`y` in `text()` is the baseline, not the top.**
Almost every app wants `y = 6`.

---

## Colours are just numbers

A colour is a single integer. What a colour picker calls `#0088FF` is `0x0088FF` here.
There is no colour object and nothing to construct.

These three lines paint exactly the same blue, three different ways:

```berry
    text(1, 6, "rain", 0x0088FF)            # written directly
    text(1, 6, "rain", rgb(0, 136, 255))    # from channels, each 0 to 255
    text(1, 6, "rain", hsv(208, 100, 100))  # from hue 0 to 360, sat and value 0 to 100
```

`hsv()` earns its place when the colour has to follow a value, which is exactly what we
will want in a moment.

A word of warning about brightness. These LEDs are genuinely bright in a dark room.
`0xFFFFFF` is right for a few glyphs. For anything that fills area, something like
`0x202020` is plenty, and saturated colours at moderate value read better than the same
hue at full blast.

---

## Centre the text properly

Guessing where text starts works until the text changes length. Measure instead:

```berry
  def draw()
    clear()
    var s = "rain"
    text((width() - text_ink_width(s)) / 2, 6, s, 0x0088FF)
  end
```

`text_ink_width()` reports how wide the lit pixels are, which is what you want for
fitting and centring. There is also `text_width()`, which reports how far the pen
moves. Use that one when you are chaining runs of text side by side.

---

## Now the bars

Text was the warm-up. What this app actually wants to show is sixteen numbers, one per
hour, each a percentage.

We do not have real numbers yet, so type some in. The shape of the code will not change
when the real ones arrive in [tutorial 3](real-data.md).

```berry
# @name Rain
# @desc Chance of rain, hour by hour

class Rain
  var hours

  def init()
    self.hours = [0, 0, 5, 20, 45, 70, 90, 60, 30, 10, 0, 0, 15, 40, 25, 5]
  end

  def draw()
    clear()
    var h = height()
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * 2, h - bar, 2, bar, 0x0088FF)
      end
    end
  end
end

return Rain()
```

Sixteen bars, two pixels wide each, filling the panel exactly.

Three new things arrived with that.

**`var hours` and `init()`.** A value the app needs to remember lives in a member. You
declare it with `var` at the top of the class and give it a value in `init()`, which
Berry runs once as the instance is created. Members are how state survives from one
frame to the next.

!!! warning "Declare every member, and give it a value"
    Berry lets you assign to a member you never declared, so a stray `self.total = 0`
    somewhere quietly works. Reading one you never declared does not. It raises
    `attribute_error: the 'Rain' object has no attribute 'total'` and the panel shows
    `ERR:`. A `var` line at the top of the class and a value in `init()` cost two lines
    and remove the whole category of problem.

**The loop.** `for i : 0 .. size(self.hours) - 1` walks the indices. Berry writes ranges
with `..`, and every block closes with `end`. A missing `end` is the single most common
reason a script refuses to install.

**The arithmetic.** `v * h / 100` scales a percentage into pixels. Multiply first, then
divide. Two integers divided in Berry give an integer, so `v / 100 * h` would collapse
every bar under 100% to zero.

The `if bar > 0` guard is there so a zero percent hour draws nothing at all rather than
a stub. On a panel this small the difference between "no rain" and "a little rain"
should be visible, and a one pixel bar for both would hide it.

---

## Let the colour carry the meaning

Thirty-two by eight leaves no room for a legend, so colour has to do that work.

```berry
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * 2, h - bar, 2, bar, hsv(208, 100, clamp(v, 25, 90)))
      end
    end
```

`hsv(208, 100, v)` holds the hue at blue and lets the value follow the percentage, so a
likely hour glows and an unlikely one sits back. `clamp(v, 25, 90)` keeps the dimmest
bar visible and the brightest one comfortable to look at.

This is worth more than it looks. The bar height already encodes the number. Brightness
encoding it a second time makes the shape readable from across a room, which is the
distance most panels are read from.

---

## The finished script

```berry
# @name Rain
# @desc Chance of rain, hour by hour
# @version 1.0

class Rain
  var hours

  def init()
    self.hours = [0, 0, 5, 20, 45, 70, 90, 60, 30, 10, 0, 0, 15, 40, 25, 5]
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

Change a number in the list, save, and the panel follows immediately. That loop is
short enough to be worth doing a few times, because it is the loop you will be living
in for the next two tutorials.

---

## What you learned

- An app is a class with a `draw()` and a final `return YourClass()`.
- `draw()` runs about forty times a second and paints one frame from what the app
  already knows.
- `width()` and `height()` beat hardcoded numbers.
- Colours are integers, and `hsv()` is how you make one follow a value.
- Members declared with `var` and set in `init()` are how an app remembers anything.

## Next

[**2. Give it a memory**](state-and-time.md) puts those sixteen numbers under the
app's own control. They start changing on a timer, the app survives a reboot with its
last values intact, and the accent colour becomes something the user can pick in the
web UI without touching your code.

## Related

- [App scripting](../guides/scripting.md) is the full reference behind every call used here
- [Charts & drawing](../guides/graphics.md) covers the drawing calls in their own right
- [Simulator](../advanced/simulator.md) if you would rather iterate without hardware
