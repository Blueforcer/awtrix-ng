---
only: [esp32, esp32-s3]
---

# Recipe: Sensor chart

This app shows the device's own temperature over the last few hours.

It needs no network and no broker. The only setting is how often to take a sample. If your
device has a temperature sensor, the app works as soon as you paste it in. If it has none,
the app stays out of the rotation instead of showing a wrong zero.

---

## What you get

A dim line of the temperature over the last four hours, with the current reading in bright
orange at the right edge, for example `21.4°`.

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Trend`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**: press **⚙**
on that app's row. New to all this?
[Tutorial 1: Draw something](first-draw.md) takes it slowly.

```berry
# @name    Trend
# @desc    Device temperature, recent history
# @author  awtrix-ng
# @version 1.0
# @config  every number "Sample every" default=15 min=1 max=60 unit=min
# @config  trend color  "Trend line"   default=#224466
# @config  now   color  "Reading"      default=#FF8800

class Trend
  var hist          # up to 16 samples, oldest first
  var label, x      # the current reading as text, and where it starts
  var shown         # the value the label was built from
  var period, ticks
  var trend, now_c

  def init()
    self.hist = store.get("hist")
    if self.hist == nil
      self.hist = []
    end
    self.label = nil
    self.x = 0
    self.shown = nil
    self.period = store.get("every") * 60
    self.ticks = 0
    self.trend = store.get("trend")
    self.now_c = store.get("now")
  end

  def loop()
    var t = sensor.temperature()
    if t == nil
      return
    end

    var r = round(t, 1)
    if r != self.shown
      self.shown = r
      self.label = str(r) + "°"
      self.x = width() - text_ink_width(self.label)
    end

    if self.ticks <= 0
      self.ticks = self.period
      self.hist.push(r)
      if size(self.hist) > 16
        self.hist.remove(0)
      end
      store.set("hist", self.hist)
    end
    self.ticks -= 1
  end

  def should_show()
    return self.label != nil
  end

  def draw()
    clear()
    if self.label == nil
      return
    end
    if size(self.hist) >= 2
      line_chart(self.hist, self.trend)
    end
    text(self.x, 6, self.label, self.now_c)
  end
end

return Trend()
```

With the default of fifteen minutes, the full chart covers four hours. Set it to 1 while
you test, so you can watch it fill.

---

## How it works

**A missing sensor answers `nil`, not zero.** Then `should_show()` returns `false` and the
rotation skips the app. The display never shows a wrong `0.0°`. `sensor.temperature()`,
`humidity()`, `pressure()`, `light()`, `battery()` and `battery_volts()` all work this way,
so check every one of them for `nil`.

The `if self.label == nil` at the top of `draw()` is a second check. The app can still be
brought to the display directly over the API. Without the check, a missing sensor would
show `ERR:`.

Values are always in Celsius. `settings.get("useCelsius")` tells you what the user chose
for the device. If you want to follow it, convert the value yourself.

**One `loop()`, two speeds.** The label follows the sensor every second, so the number on
the display is current. The history takes a sample only every fifteen minutes. Sixteen
values at one per second would cover only sixteen seconds.

**The label is rebuilt only when the value changes.** `str(r) + "°"` creates a new string
every time it runs, and the temperature often stays the same for minutes. The same check
also stores the x position, so `draw()` never measures anything.

**Always `round(r, 1)` before `str()`.** A raw sensor value prints all its decimals, and
`21.399999618530273` does not fit on a <!-- only esp32 esp32-s3 -->32<!-- /only --><!-- only tc002 -->52<!-- /only --> pixel display.

**The chart is drawn first and dim, the number second and bright.** Both use the same
eight rows, and what is drawn last is on top. `line_chart()` fills the full display width and
takes at most sixteen values, so the history is trimmed to sixteen. It needs at least two
values to draw anything, so the app checks `size` first.

Automatic scaling is on by default: the line uses the full display height for the range the
data covers. That is right for temperature, where a change of two degrees is interesting.
Turn it off with `line_chart(list, color, false)`. Then the scale is fixed at 0 to 8, one step
per pixel row, so the values must already be pixel heights. Whole numbers are drawn: `21.4`
counts as `21`.

---

## Making it yours

**Chart something else.** Use `sensor.humidity()` instead and change the unit in the
label. The rest of the app stays the same.

**Chart something from the network.** Replace the `sensor` call with the fetch from
[tutorial 3](real-data.md).

**Show the direction instead of the history.** Keep only the oldest and newest values and
draw an arrow. On a small display, one symbol is often easier to read than sixteen values.

**Use bars instead of a line.** `bar_chart()` takes the same three arguments. Bars suit
separate values, like hourly rain. A line suits a value that changes slowly, like
temperature.

**Share the value with other apps.** `shared.set("temp", r)` makes it readable for every
app on the device as `Trend.temp`. Other apps then do not need their own copy of this code.

---

## Related

- [Brightness & sensors](../guides/brightness.md) – what each board measures
- [Charts & drawing](../guides/graphics.md) – all chart calls
- [Scripting guide](../guides/scripting/index.md) – `shared` and the full sensor reference
