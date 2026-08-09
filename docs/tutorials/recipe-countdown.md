# Recipe: Countdown

Days until a date you pick, in a colour that changes when the date gets close.

Nothing here touches the network, so it works on any AWTRIX the moment you paste it in.
It is also the shortest useful app in these tutorials, which makes it a good one to
take apart.

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Countdown`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**, the `⋯`
menu on that app's row, then **Settings**. New to all this?
[Tutorial 1](first-draw.md) takes it slowly.

```berry
# @name    Countdown
# @desc    Days until a date
# @author  awtrix-ng
# @version 1.0
# @config  target text   "Target date" default="2026-12-24" help="YYYY-MM-DD"
# @config  label  text   "Label"       default="XMAS" maxlen=12
# @config  soon   number "Warn under"  default=7 min=0 max=365 unit=days
# @config  tint   color  "Colour"      default=#FFAA00
# @config  warn   color  "Close"       default=#FF3000

class Countdown
  var target        # the target date as a day number
  var label, soon   # from the settings
  var tint, warn
  var line, colour  # what draw() paints
  var ticks

  def init()
    self.label = store.get("label")
    self.soon = store.get("soon")
    self.tint = store.get("tint")
    self.warn = store.get("warn")
    self.ticks = 0
    self.line = nil
    self.target = nil

    var p = re.matchall("\\d+", store.get("target"))
    if p != nil && size(p) >= 3
      self.target = self.day_number(num(p[0], 0), num(p[1], 1), num(p[2], 1))
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
        self.line = str(left) + " " + self.label
        self.colour = left <= self.soon ? self.warn : self.tint
      end
    end
    self.ticks -= 1
  end

  def draw()
    clear()
    if self.line == nil
      text(1, 6, "...", 0x444444)
      return
    end
    scroll_text(self.line, self.colour)
  end
end

return Countdown()
```

Set your own date and label in the settings. Saving there restarts the app, so `init()`
picks the new date up immediately.

---

## How it works

**The date arrives as text, so it has to be parsed.** `re.matchall("\\d+", …)` pulls
every run of digits out of `2026-12-24` and hands back three strings, which `num()`
turns into numbers. This is deliberately forgiving: `2026/12/24` and `24.12.2026` would
both produce three numbers too, though only the first of those is in the right order.
Being clear in the `help=` text is cheaper than being clever in the parser.

**Date arithmetic without a date library.** Berry on AWTRIX has no `time` module, so
`day_number()` does the work. It converts a calendar date into a plain count of days,
using the standard civil calendar formula. Leap years, century rules and the four
hundred year cycle are all in those five lines. Subtract two of its answers and you
have the days between two dates, correctly, with no special cases.

It looks cryptic and it is. It is also short, exact and something you write once and
never think about again. Copy it into any app that needs to compare dates.

**The recalculation runs once a minute, in `loop()`.** A countdown in days changes at
most once a day, so recomputing forty times a second in `draw()` would be forty
thousand times more work than the problem deserves. `loop()` builds the finished string
and picks the colour, and `draw()` does nothing but paint them.

**`year() > 0` is a real guard, not a formality.** Every wall-clock call returns `-1`
until the device has fetched the time, which happens a little after boot. Without that
check the app would compute a countdown to a date thousands of years away and store it
in `self.line` before correcting itself a minute later.

**`scroll_text()` decides whether the text fits.** Give it the line and a colour, and
short text stands still and centred while long text travels across the panel. You never
have to guess how many characters fit at 32 pixels, and the app keeps the panel until
the line has finished its run, so there is no duration to calculate either.

---

## Making it yours

**Count hours instead of days.** Add `hour()` to the comparison and the same subtraction
gives you hours: `(self.target - today) * 24 - hour()`.

**Count up instead of down.** Swap the subtraction. "Days since" is the same app with
the operands the other way round, and dropping the `if left < 0` clamp is what makes it
work.

**Hide the app once the date has passed.** Add a `should_show()` returning
`self.line != nil && left > 0`, keeping `left` in a member. The rotation then skips
straight past it instead of showing a permanent zero.

**Add an icon.** `icon(name, 0, 0)` draws an 8×8 icon from the device's icon folder, and
the text starts at `x = 9` when there is one. Icon IDs differ from device to device, so
make it a `# @config … text` field rather than picking one for the user.

---

## Related

- [2. Give it a memory](state-and-time.md) for the lifecycle and `@config` in full
- [App scripting](../guides/scripting.md) for the time calls and `scroll_text()` options
- [Icons & assets](../guides/icons.md) for what `icon()` can draw
