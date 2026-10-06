# Recipe: Countdown

This app shows the days until a date you pick. It does not use the network, so it works on any
AWTRIX as soon as you paste it in.

---

## What you get

One line with the days left and your label, in your color. In the last week before the date,
the color turns red. A line that fits stands still in the middle. A longer one moves through
the display.

This short script paints the same line the app shows with the default settings, 79 days
before 24 December:

<!-- panel -->
```berry
class Countdown
  def draw()
    clear()
    scroll_text("79 XMAS", 0xFFAA00)
  end
end

return Countdown()
```

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Countdown`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**: press **⚙**
on that app's row. New to all this?
[Tutorial 1: Draw something](first-draw.md) takes it slowly.

```berry
# @name    Countdown
# @desc    Days until a date
# @author  awtrix-ng
# @version 1.0
# @config  target text   "Target date" default="2026-12-24" help="YYYY-MM-DD"
# @config  label  text   "Label"       default="XMAS" maxlen=12
# @config  soon   number "Warn under"  default=7 min=0 max=365 unit=days
# @config  tint   color  "Color"       default=#FFAA00
# @config  warn   color  "Close"       default=#FF3000

class Countdown
  var target        # the target date as a day number
  var label, soon   # from the settings
  var tint, warn
  var msg, color    # what draw() paints
  var once          # scroll options, built once
  var ticks

  def init()
    self.label = store.get("label")
    self.soon = store.get("soon")
    self.tint = store.get("tint")
    self.warn = store.get("warn")
    self.ticks = 0
    self.msg = nil
    self.color = self.tint
    self.once = {"repeat": 1}
    self.target = nil

    var p = re.matchall("\\d+", store.get("target"))
    if size(p) >= 3
      self.target = self.day_number(int(p[0]), int(p[1]), int(p[2]))
    end
  end

  # Days since a fixed point, for any Gregorian date. Two of these subtracted
  # give the number of days between them, which is all we need.
  def day_number(y, m, d)
    if m <= 2
      y -= 1
    end
    var era = y / 400
    var yoe = y - era * 400
    var mp = m > 2 ? m - 3 : m + 9
    var doy = (153 * mp + 2) / 5 + d - 1
    return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy
  end

  def loop()
    if self.ticks <= 0
      self.ticks = 60
      if self.target != nil && year() > 0
        var left = self.target - self.day_number(year(), month(), day())
        if left < 0
          left = 0
        end
        self.msg = str(left) + " " + self.label
        self.color = left <= self.soon ? self.warn : self.tint
      end
    end
    self.ticks -= 1
  end

  def draw()
    clear()
    if self.msg == nil
      text(1, 6, "...", 0x444444)
      return
    end
    scroll_text(self.msg, self.color, self.once)
  end
end

return Countdown()
```

Set your own date and label in the settings. Saving there restarts the app, so `init()`
picks the new date up immediately.

---

## How it works

**The date is text, so the app reads the numbers out of it.** `re.matchall("\\d+", …)`
takes every group of digits from `2026-12-24` and returns three strings. `int()` turns them
into numbers. `2026/12/24` also works. `24.12.2026` gives three numbers in the wrong order,
so the `help=` text tells the user the format.

**Date calculation without a date library.** Berry on AWTRIX has no `time` module, so
`day_number()` does the work. It turns a calendar date into a plain count of days. Leap
years are included. Subtract two of its results and you get the days between two dates.
Copy it into any app that needs to compare dates.

**The calculation runs once a minute, in `loop()`.** A countdown in days changes at most
once a day, so there is no reason to calculate it forty times a second in `draw()`.
`loop()` builds the text and picks the color. `draw()` only paints them.

**`year() > 0` is needed.** Every wall-clock call returns `-1` until the device has fetched
the time, shortly after boot. Without the check, the app would count down to a wrong date
for up to a minute.

**`scroll_text()` decides whether the text fits.** Short text stands still and centered.
Long text moves across the display. `{"repeat": 1}` keeps the app on the display until the text
has finished its run, so you do not need to set a duration.

---

## Making it yours

**Count hours instead of days.** Multiply the days by 24 and subtract the hours already
gone today: `(self.target - self.day_number(year(), month(), day())) * 24 - hour()`.

**Count up instead of down.** For "days since", swap the two sides of the subtraction and
remove the `if left < 0` check.

**Hide the app once the date has passed.** Keep `left` in a member: add `var left`, set
`self.left = 0` in `init()` and `self.left = left` in `loop()`. Then add a `should_show()`
that returns `self.msg != nil && self.left > 0`. The rotation then skips the app instead of
showing zero.

**Add an icon.** Pick an icon on the [AWTRIX Hub](../guides/icons.md#install-from-the-awtrix-hub)
and note its name. Put that name in a `# @icons` line in the header, for example
`# @icons calendar`. When someone installs the script from the Hub, the icon comes with it. For
a script you pasted in yourself, the web UI offers a button that installs it. See
[The icons your script needs](../guides/scripting/drawing.md#the-icons-your-script-needs). Draw the
icon in `draw()` and start the text to the right of it:

```berry
    icon("calendar", 0, 0)
    scroll_text(9, 6, width() - 9, self.msg, self.color, self.once)
```

Use your icon's name in both places, the `# @icons` line and the `icon()` call. An 8 px wide
icon plus a gap of one pixel puts the text at `x = 9`.

---

## Related

- [Tutorial 2: Give it a memory](state-and-time.md) – app methods and `@config`
- [Scripting guide](../guides/scripting/index.md) – the time calls and `scroll_text()` options
- [Icons](../guides/icons.md) – what `icon()` can draw
