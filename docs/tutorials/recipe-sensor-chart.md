# Recipe: Sensor chart

The panel's own temperature over the last few hours, drawn as a dim trend line with the
current reading sitting on top of it.

No network, no broker, no configuration beyond how often to sample. If your board has a
temperature sensor, this works the moment you paste it in. If it does not, the app stays
out of the rotation instead of showing you a confident zero.

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Trend`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**, the `⋯`
menu on that app's row, then **Settings**. New to all this?
[Tutorial 1](first-draw.md) takes it slowly.

```berry
# @name    Trend
# @desc    Panel temperature, recent history
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

At the default of fifteen minutes the chart covers four hours once it has filled up.
Set it to 1 while you are testing so you can watch it work.

---

## How it works

**Sensors answer `nil` when the board does not have them.** Not zero, `nil`. That
distinction is the whole reason this app is trustworthy: a missing sensor makes
`should_show()` return `false` and the rotation skips past, instead of the panel
reporting a crisp `0.0°` that somebody might believe. Every one of `sensor.temperature()`,
`humidity()`, `pressure()`, `light()`, `battery()` and `battery_volts()` behaves the
same way, and every one of them deserves the same check.

The `if self.label == nil` at the top of `draw()` covers the same ground a second time.
`should_show()` keeps the app out of the rotation, but an app can still be summoned to
the panel directly over the API, and painting from members that are still `nil` is how
you turn a missing sensor into an `ERR:`.

Readings are always in Celsius. If you want to follow the device's own preference,
`settings.get("useCelsius")` tells you what the user picked and the conversion is yours
to do.

**Two different rhythms live in one `loop()`.** The label follows the sensor as closely
as it can, so the number on the panel is current. The history only takes a sample every
fifteen minutes, because sixteen values at one per second would cover sixteen seconds
and tell you nothing. Splitting them costs one `if`.

**The label is rebuilt only when the value changes.** `if r != self.shown` looks like a
micro-optimisation and is not. `str(r) + "°"` allocates a new string every time it runs,
and on a still afternoon the temperature does not move for minutes at a time. The same
guard also caches the x position, so `draw()` never measures anything.

**`round(r, 1)` before `str()`, always.** A raw sensor real prints every decimal it
carries, and `21.399999618530273` does not fit on a 32 pixel panel.

**The chart is drawn first and dim, the number second and bright.** Both occupy the same
eight rows, and painting order decides what wins. `line_chart()` spans the full panel
width and takes at most sixteen values, dropping any extras, which is why the history is
trimmed to sixteen. It needs at least two points to draw anything, hence the `size` check.

Autoscaling is on by default, so the line uses the full height of the panel for whatever
range the data actually covers. That is right for temperature, where the interesting part
is a two degree drift. It is wrong for a percentage, which should be measured against 0
to 100, and `line_chart(list, colour, false)` is how you turn it off.

---

## Making it yours

**Chart something else.** Swap in `sensor.humidity()` and change the unit in the label.
The rest of the app does not care what the number means.

**Chart something from the network.** Replace the `sensor` call with the fetch from
[tutorial 3](real-data.md) and you have a graph of anything with an API.

**Show the direction rather than the history.** Keep just the oldest and newest values
and draw an arrow. On a panel this small a single glyph often reads faster than sixteen
data points.

**Use bars instead of a line.** `bar_chart()` takes the same three arguments. Bars suit
values that stand alone, like hourly rainfall; a line suits a quantity that drifts, like
temperature.

**Publish the reading for other apps.** `shared.set("temp", r)` makes it readable by
every app on the device as `Trend.temp`, so the next app that wants the temperature does
not need its own copy of any of this.

---

## Related

- [Brightness & sensors](../guides/brightness.md) for what each board actually measures
- [Charts & drawing](../guides/graphics.md) for the chart calls in their own right
- [App scripting](../guides/scripting.md) for `shared` and the full sensor reference
