# Tutorial 3: Feed it real data

This tutorial replaces the random numbers from [tutorial 2](state-and-time.md) with a real
weather forecast.

The app from tutorial 2 already keeps state, refreshes on a timer, survives a reboot and has
settings. The main change is where the numbers come from. Because the work already happens
in `loop()`, `draw()` stays almost the same.

We use [Open-Meteo](https://open-meteo.com/), which needs no account and no API key.

The tutorial goes in five steps. The first one gives you a working app. The others make it
more robust and lighter on memory.

---

## Look at the answer first

Before writing any code against a web service, look at what it answers. Open this address
in a browser, with your own latitude and longitude in place of Berlin's:

```
https://api.open-meteo.com/v1/forecast?latitude=52.52&longitude=13.40&hourly=precipitation_probability&forecast_hours=16&timezone=auto
```

`forecast_hours=16` asks for exactly sixteen values, which is what the display has room
for. `timezone=auto` makes the first of them the hour you are in right now.

The answer looks like this:

```json
{"latitude":52.52,"longitude":13.4,"generationtime_ms":0.04,"utc_offset_seconds":7200,
"timezone":"Europe/Berlin","timezone_abbreviation":"GMT+2","elevation":30.0,
"hourly_units":{"time":"iso8601","precipitation_probability":"%"},
"hourly":{"time":["2026-08-09T19:00","2026-08-09T20:00", … fourteen more … ],
"precipitation_probability":[0,0,0,0,0,0,0,5,28,18,5,0,3,0,0,0]}}
```

The sixteen numbers we want are in `"hourly"`, under `"precipitation_probability"`.

---

## Step 1: Fetch the forecast {#fetching-without-freezing-the-panel}

A web request takes time: usually a fraction of a second, sometimes much longer. AWTRIX
never lets an app wait for it, because a waiting app would freeze the display for every app.

So `http.get()` works with a *callback*: a function you hand over, which AWTRIX calls later.

```berry
      http.get(self.url, / b, st -> self.on_body(b, st))
```

- `http.get()` sends the request and returns at once. Your `loop()` carries on.
- When the answer is there, AWTRIX calls your function, once, between two frames.
- `/ b, st -> self.on_body(b, st)` is a short way to write a small function. It takes the
  two values AWTRIX passes in, the answer `b` and the status `st`, and hands them on to your
  own method `on_body()`.

`on_body(body, status)` then gets two values:

- **`body`** is the answer as text. It is `nil` (no value) when nothing came back at all:
  no Wi-Fi, a wrong address, or no answer within thirty seconds. `status` is then `0`.
- **`status`** is the HTTP status code. `200` means everything is fine. A server that
  answers with an error, for example `404` or `500`, still sends a `body`, but not the one
  you want.

So the handler starts with one check: `if body == nil || status != 200 return end`.

`json.load(body)` turns the answer text into Berry maps and lists. After that,
`data["hourly"]["precipitation_probability"]` is the list of sixteen numbers.

Here is the complete app. Paste it over your `Rain` script and save:

```berry
# @name    Rain
# @desc    Chance of rain for the next 16 hours, from Open-Meteo
# @version 1.0
# @config  lat   text   "Latitude"  default="52.52" help="Decimal degrees, north positive"
# @config  lon   text   "Longitude" default="13.40" help="Decimal degrees, east positive"
# @config  every number "Refresh"   default=15 min=1 max=60 unit=min
# @config  tint  color  "Bar color" default=#0088FF

import json

class Rain
  var hours       # 16 percentages, this hour first
  var url         # the address to fetch, built from the settings
  var tint        # bar color
  var period      # loop() calls between fetches
  var ticks       # counting down to the next fetch

  def init()
    self.url = "https://api.open-meteo.com/v1/forecast" +
               "?hourly=precipitation_probability&forecast_hours=16&timezone=auto" +
               "&latitude=" + store.get("lat") +
               "&longitude=" + store.get("lon")
    self.tint = store.get("tint")
    self.period = store.get("every") * 60
    self.ticks = 0
    self.hours = store.get("hours")
    if self.hours == nil
      self.hours = []
      for i : 0 .. 15
        self.hours.push(0)
      end
    end
  end

  def loop()
    if self.ticks <= 0
      self.ticks = self.period
      http.get(self.url, / b, st -> self.on_body(b, st))
    end
    self.ticks -= 1
  end

  def on_body(body, status)
    if body == nil || status != 200 return end
    var data = json.load(body)
    if data == nil return end
    var list = data["hourly"]["precipitation_probability"]
    if size(list) < 16 return end
    for i : 0 .. 15
      self.hours[i] = num(list[i], 0)
    end
    store.set("hours", self.hours)
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

Open **Apps**, press **⚙** on the app's row, and put your own latitude and longitude in.
Decimal degrees, so Berlin is `52.52` and `13.40`. Saving the settings restarts the app, and
the first fetch goes out straight away.

A few details:

- `json` is not built in like `http`, so the script starts with `import json`.
- `json.load()` returns `nil` when the text is not valid JSON. The app then keeps its old
  values.
- `num(list[i], 0)` returns the value as a number, or `0` if the service sent no value for
  that hour.
- `self.hours[i] = …` changes the existing list in place. `draw()` paints the new values on
  the next frame.

<!-- only esp32 esp32-s3 -->
!!! note "The first request after a restart can be slow"
    An `https://` request made shortly after a restart or a Wi-Fi reconnect takes about fifteen
    seconds to answer. That is normal. Step 3 shows what to draw while you wait.
<!-- /only -->

---

## Step 2: Don't fetch twice at once {#not-stacking-up-requests}

If the network is slow and your timer fires again, several requests can be waiting at
once, and each one uses memory. The fix is one member that says "a request is on its way":

```berry
  def loop()
    if self.ticks <= 0
      self.ticks = self.period
      if !self.in_flight
        self.in_flight = true
        http.get(self.url, / b, st -> self.on_body(b, st))
      end
    end
    self.ticks -= 1
  end

  def on_body(body, status)
    self.in_flight = false
    if body == nil || status != 200 return end
    # the rest of the handler as before
  end
```

Add `var in_flight` to the class and `self.in_flight = false` to `init()`.

Clear the flag on the **first line** of the handler, before any check that might return
early. If the handler returns before clearing it, the app thinks a request is still running
and never fetches again.

Choose a long interval. A bare `http.get()` in `loop()` fires once a second, which helps
nobody and puts load on the service. Weather every five to fifteen minutes is enough. The
interval is a `@config` setting, so the user can make it slower without editing code.

---

## Step 3: Before the first answer {#before-the-first-answer}

Between installing the app and the first successful answer there are a few seconds, or
after a restart a few tens of seconds, where you have nothing to show. Never leave the
display blank.

Add a member `ready` that says "we have real numbers". It is true when the store already
holds values from an earlier run. `on_body()` sets `self.ready = true` once it has stored the
new values, and `draw()` shows a dim placeholder until then. The changed parts, in their place
in the class:

<!-- panel -->
```berry
class Rain
  var hours, ready   # and the other members, as before

  def init()
    self.hours = store.get("hours")
    self.ready = self.hours != nil
    if !self.ready
      self.hours = []
      for i : 0 .. 15
        self.hours.push(0)
      end
    end
    # the rest of init(), as before
  end

  def draw()
    clear()
    if !self.ready
      text(1, 6, "...", 0x444444)
      return
    end
    # the bars, as before
  end
end

return Rain()
```

Until the first answer arrives, a new install shows three dim dots.

The other option is to skip the turn entirely. AWTRIX asks `should_show()` when the rotation
arrives at your app. Returning `false` sends it straight on to the next one:

```berry
  def should_show()
    return self.ready
  end
```

Which one to use:

- A dim placeholder means "this app is waiting for data". Use it when data is expected
  soon.
- `should_show()` returning `false` means "nothing to report". Use it for an app that is
  often empty, like a reminder that only shows on bin day.

A skipped app gets a dim marker on its row in the web UI, so you can see why it did not
appear.

This app uses the placeholder. After the first successful run it has stored values, so the
gap only ever appears once.

---

## Step 4: Make it lighter {#ask-for-less}

The app works. Now look at what it costs.

All scripts on the device share one block of memory. By default the whole answer lands
there as one piece of text, up to 8 KB. `json.load()` then builds maps, lists and strings
from it, several times the size of the text. Our answer is small, because we asked for only
sixteen hours. Many services answer with several kilobytes, and on a device with little
memory that is enough to make other apps fail.

We only want the sixteen numbers at the very end of the answer. So we ask AWTRIX to keep
only that part.

### Keep only a window of the answer

The `find` and `keep` options make AWTRIX search the answer while it arrives. It keeps only
a small window. The window starts where your search text (the *needle*) first appears:

```berry
      http.get(self.url, / b, st -> self.on_body(b, st),
               {'find': "\"precipitation_probability\":[", 'keep': 128})
```

`body` is then the 128 characters starting **at** the match, needle included. The size of
the whole answer does not matter. A value far into a long answer costs the same as one at
the start.

Look closely at the needle. It ends with `[`, and that bracket is important: the text
`"precipitation_probability"` also appears earlier, inside `hourly_units`, where its value
is `"%"`. Without the bracket we would match the units and get the wrong window.

Inside a Berry string, `\"` stands for a quote character. So the needle is the text
`"precipitation_probability":[` with its quotes.

If the needle never appears, the callback gets `nil` as `body`, with the real status code.
An error answer from the server does not contain the needle either, so the one check
`if body == nil return end` covers every failure.

### Getting sixteen numbers out of the window

The window is now this, and it is not valid JSON on its own:

```
"precipitation_probability":[0,0,0,0,0,0,0,5,28,18,5,0,3,0,0,0]}}
```

So `json.load()` cannot read it. We do not need it: a *regular expression*, a small search
pattern, gets the sixteen numbers without building anything else:

```berry
    var m = re.matchall("\\d+", body)
```

`re.matchall()` returns every match as a list. `\d+` means "one or more digits". It is
written `"\\d+"` because a backslash starts an escape in Berry strings too. The needle has
no digits, so what comes back is exactly our sixteen numbers, as text. `int(m[i])` turns
the text `"28"` into the number `28`.

`re` is built in and needs no `import`.

**This is the recommended form for every app that fetches regularly:** `find` and `keep`
to get a small window, and `re` to take the values out of it.

!!! tip "When `json.load()` is the right call after all"
    When you need many fields, or you have to walk through a list of records, use
    `json.load()`. A regular expression gets hard to get right there. Rule of thumb: one
    or two values means `re`, a structure you have to walk means `json`.
    [Keeping scripts small](going-easy-on-memory.md) compares both.

---

## Step 5: The finished app {#the-finished-app}

Everything from steps 1 to 4 together, plus two small extras explained below: a dim line
along the bottom, and the middle button to fetch at once.

```berry
# @name    Rain
# @desc    Chance of rain for the next 16 hours, from Open-Meteo
# @author  awtrix-ng
# @version 1.0
# @config  lat   text   "Latitude"  default="52.52" help="Decimal degrees, north positive"
# @config  lon   text   "Longitude" default="13.40" help="Decimal degrees, east positive"
# @config  every number "Refresh"   default=15 min=1 max=60 unit=min
# @config  tint  color  "Bar color" default=#0088FF

class Rain
  var hours          # 16 percentages, this hour first
  var url, tint      # built from the settings in init()
  var period, ticks  # loop() calls between fetches, and the countdown
  var in_flight      # a request is on its way
  var ready          # we have real numbers to show

  def init()
    self.url = "https://api.open-meteo.com/v1/forecast" +
               "?hourly=precipitation_probability&forecast_hours=16&timezone=auto" +
               "&latitude=" + store.get("lat") +
               "&longitude=" + store.get("lon")
    self.tint = store.get("tint")
    self.period = store.get("every") * 60
    self.ticks = 0
    self.in_flight = false

    self.hours = store.get("hours")
    self.ready = self.hours != nil
    if !self.ready
      self.hours = []
      for i : 0 .. 15
        self.hours.push(0)
      end
    end
  end

  def on_body(body, status)
    self.in_flight = false
    if body == nil return end
    var m = re.matchall("\\d+", body)
    if size(m) < 16 return end
    for i : 0 .. 15
      self.hours[i] = int(m[i])
    end
    self.ready = true
    store.set("hours", self.hours)
  end

  def loop()
    if self.ticks <= 0
      self.ticks = self.period
      if !self.in_flight
        self.in_flight = true
        http.get(self.url, / b, st -> self.on_body(b, st),
                 {'find': "\"precipitation_probability\":[", 'keep': 128})
      end
    end
    self.ticks -= 1
  end

  def draw()
    clear()
    if !self.ready
      text(1, 6, "...", 0x444444)
      return
    end
    var h = height()
    var w = width() / size(self.hours)
    line(0, h - 1, width() - 1, h - 1, 0x101820)
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * w, h - bar, w, bar, self.tint)
      end
    end
  end

  def on_button(btn)
    if btn == "select"
      self.ticks = 0
      return true
    end
  end
end

return Rain()
```

The app does not use `json`, so it has no `import json` line.

Set `Refresh` to 1 minute while you are testing, and put it back afterwards.

!!! note "Saving over tutorial 2 keeps its stored values"
    If you saved this over the same script name, the store still holds sixteen numbers
    from before. So `ready` is already true, and the display shows those numbers until the
    first real answer replaces them. To see the `...` placeholder the way a new user
    would, save this under a new script name.

### The middle button

`on_button(btn)` runs when a button is pressed while your app is shown. `btn` is
`"left"`, `"select"` or `"right"`. Here the middle button (`"select"`) sets the countdown to
zero, so the next `loop()` fetches at once. That saves a lot of waiting while you test.
Returning `true` tells AWTRIX that the app handled the press, so the button does nothing
else.

### The dim line along the bottom

One line in `draw()` has nothing to do with fetching. It makes a quiet display look
intentional:

```berry
    line(0, h - 1, width() - 1, h - 1, 0x101820)
```

On a dry day all sixteen percentages are zero or close to it. Almost no bar is drawn, and
the display is nearly dark. The app works, but it looks broken.

The dim line fixes that with one call. "No rain" shows as a flat line, and a completely
blank display means something is wrong. Any app that draws values that can drop to zero
benefits from this.

Compare this with tutorial 2. `draw()` got a placeholder and a line. `loop()` calls
`http.get()` instead of `math.rand()`. Everything else is new methods next to the old ones.

---

## Let the firmware draw the chart {#letting-the-firmware-draw-the-chart}

The hand-drawn bars show how coordinates work. AWTRIX can also draw the chart for you with
`bar_chart()`. This is the same app with the built-in chart:

```berry
# @name    Rain
# @desc    Chance of rain for the next 16 hours, from Open-Meteo
# @author  awtrix-ng
# @version 1.0
# @config  lat   text   "Latitude"  default="52.52" help="Decimal degrees, north positive"
# @config  lon   text   "Longitude" default="13.40" help="Decimal degrees, east positive"
# @config  every number "Refresh"   default=15 min=1 max=60 unit=min
# @config  tint  color  "Bar color" default=#0088FF

class Rain
  var hours          # 16 percentages, this hour first
  var bars           # the same values as bar heights, 0 to 8
  var url, tint      # built from the settings in init()
  var period, ticks  # loop() calls between fetches, and the countdown
  var in_flight      # a request is on its way
  var ready          # we have real numbers to show

  def init()
    self.url = "https://api.open-meteo.com/v1/forecast" +
               "?hourly=precipitation_probability&forecast_hours=16&timezone=auto" +
               "&latitude=" + store.get("lat") +
               "&longitude=" + store.get("lon")
    self.tint = store.get("tint")
    self.period = store.get("every") * 60
    self.ticks = 0
    self.in_flight = false

    self.hours = store.get("hours")
    self.ready = self.hours != nil
    if !self.ready
      self.hours = []
      for i : 0 .. 15
        self.hours.push(0)
      end
    end
    self.bars = []
    self.scale()
  end

  def scale()
    self.bars.clear()
    for v : self.hours
      self.bars.push(v * 8 / 100)
    end
  end

  def on_body(body, status)
    self.in_flight = false
    if body == nil return end
    var m = re.matchall("\\d+", body)
    if size(m) < 16 return end
    for i : 0 .. 15
      self.hours[i] = int(m[i])
    end
    self.scale()
    self.ready = true
    store.set("hours", self.hours)
  end

  def loop()
    if self.ticks <= 0
      self.ticks = self.period
      if !self.in_flight
        self.in_flight = true
        http.get(self.url, / b, st -> self.on_body(b, st),
                 {'find': "\"precipitation_probability\":[", 'keep': 128})
      end
    end
    self.ticks -= 1
  end

  def draw()
    clear()
    if !self.ready
      text(1, 6, "...", 0x444444)
      return
    end
    line(0, height() - 1, width() - 1, height() - 1, 0x101820)
    bar_chart(self.bars, self.tint, false)
  end

  def on_button(btn)
    if btn == "select"
      self.ticks = 0
      return true
    end
  end
end

return Rain()
```

`bar_chart(list, color, autoscale)` spans the full display width and takes at most sixteen
values. It leaves a one-pixel gap between the bars, so on a <!-- only esp32 esp32-s3 -->32 pixel display each bar is one
pixel wide<!-- /only --><!-- only tc002 -->52 pixel display each bar is two
pixels wide<!-- /only -->.

The third argument, `false`, switches autoscaling off:

- With autoscaling on, the highest value always fills the display. A dry day with 5 % would
  look alarming.
- With autoscaling off, the scale is fixed from 0 to 8, and 8 fills the full height. So the
  chart needs bar heights from 0 to 8, not percentages. `scale()` converts the percentages
  into the list `bars`, once after each answer, so `draw()` only paints.

The color can also be a palette name. `bar_chart(self.bars, "Ocean", false)` colors each
bar by its own height.

There is also `line_chart()` with the same arguments, and `progress(pct)` for a single
value along the bottom row.

Which one to use: the built-in chart is one line and spaces the bars for you. The
hand-drawn version lets you decide what a zero looks like, how wide a bar is, and where the
color comes from. Start with `bar_chart()`, and write the loop when you need something it
cannot do.

---

## What you learned

- `http.get()` never waits. It returns at once, and AWTRIX calls your callback later,
  between two frames.
- `body == nil` means nothing useful arrived. Check it first in every handler.
- An `in_flight` member, cleared on the first line of the handler, stops requests piling
  up.
- Draw a placeholder or return `false` from `should_show()`, but never show an empty
  display.
- `find` and `keep` keep only a small window of the answer, and `re.matchall()` takes the
  values out of it. This saves more memory than anything else in an app that fetches.
- `bar_chart()` draws the chart for you.

## Next

You have written a complete app. The four recipes are independent, and each one builds
something different:

- [Countdown](recipe-countdown.md), which needs no network at all
- [MQTT status](recipe-mqtt-status.md), for a device that already has a broker
<!-- only esp32 esp32-s3 -->
- [Sensor chart](recipe-sensor-chart.md), using the clock's own thermometer
<!-- /only -->
- [Background doorbell](recipe-doorbell.md), an app that never draws anything

If your device refuses to install scripts, or other apps start failing after you
installed yours, read [Keeping scripts small](going-easy-on-memory.md).

## Related

- [Scripting guide](../guides/scripting/index.md) for the full HTTP, regular expression and charting reference
- [Limits](../reference/limits.md#scripting) for response sizes, timeouts and how many requests may be in flight
