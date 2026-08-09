# 3. Feed it real data

The app from [tutorial 2](state-and-time.md) has everything a real app has except real
numbers. It keeps state, it refreshes on a timer, it remembers across a reboot and the
user can configure it. All of that was built around `math.rand()`.

Now we replace that one line with a weather service, and almost nothing else changes.
That is the payoff for having put the work in `loop()` in the first place.

We use [Open-Meteo](https://open-meteo.com/), which needs no account and no API key.

---

## Fetching without freezing the panel

There is no blocking HTTP call on AWTRIX, and there is no `sleep()`. A script that
waited for a server would stall the render loop for every app on the device, so the
API does not offer the option.

Instead, `http.get()` returns immediately and calls you back later:

```berry
    http.get(url, def (body, status)
      # body is a string, or nil if nothing arrived at all
      # status is the HTTP code, or 0 when nothing arrived
    end)
```

The callback fires once, between frames, some time later. Usually that is a fraction of
a second. Occasionally it is thirty, and once in a while it never happens at all,
because a network is not a function call.

Two failures look different and you should treat them differently:

- **`body` is `nil` and `status` is `0`.** Nothing came back. No Wi-Fi, a DNS miss, a
  refused connection, or no answer within thirty seconds.
- **`body` is a string and `status` is 404, or 500, or anything else.** The server
  answered, and what it said was not what you wanted.

A real response always reaches the callback, error codes included. Branch on `status`
only where your app genuinely needs to tell the cases apart. For most apps the single
`if body == nil return end` at the top of the handler covers every failure worth
handling.

!!! note "The first request after a boot is slow on purpose"
    HTTPS requests are held for about fifteen seconds after a boot or a Wi-Fi
    reconnect, while the network stack settles. Show a placeholder and let it happen.
    It is not an error and there is nothing to fix.

---

## Ask for less

This is the most consequential line in any networked app, so it gets its own section.

Before writing any code against an API, look at what it actually answers. Open this in a
browser, with your own latitude and longitude in place of Berlin's:

```
https://api.open-meteo.com/v1/forecast?latitude=52.52&longitude=13.40&hourly=precipitation_probability&forecast_hours=16&timezone=auto
```

`forecast_hours=16` asks for exactly sixteen values, which is what the panel has room
for, and `timezone=auto` makes the first of them the hour you are in right now.

The full answer looks like this:

```json
{"latitude":52.52,"longitude":13.4,"generationtime_ms":0.04,"utc_offset_seconds":7200,
"timezone":"Europe/Berlin","timezone_abbreviation":"GMT+2","elevation":30.0,
"hourly_units":{"time":"iso8601","precipitation_probability":"%"},
"hourly":{"time":["2026-08-09T19:00","2026-08-09T20:00", … fourteen more … ],
"precipitation_probability":[0,0,0,0,0,0,0,5,28,18,5,0,3,0,0,0]}}
```

We want the last forty characters of that. By default the callback would receive the
whole thing, up to 8 KB, as one Berry string on the heap that every script on the device
shares.

The `find` and `keep` options turn that cap into a search. AWTRIX scans the response as
it streams in and keeps only a small window starting at the first occurrence of your
needle:

```berry
    http.get(self.url, / b, st -> self.on_body(b, st),
             {'find': "\"precipitation_probability\":[", 'keep': 128})
```

`body` is then the 128 bytes starting **at** the match, needle included. The size of the
document stops mattering. A field a megabyte into a response costs exactly the same as
one at the start.

Look closely at the needle. It ends with `[`, and that bracket is doing real work: the
string `"precipitation_probability"` also appears earlier, inside `hourly_units`, where
its value is `"%"`. Without the bracket we would match the units block and get a window
full of the wrong thing. Needles are worth reading twice.

If the needle never appears, the callback gets `(nil, status)` with the real status
code, which is how you tell "the API changed its field names" apart from "the Wi-Fi is
down".

---

## Getting sixteen numbers out of the window

The window is now this, and it is not valid JSON on its own:

```
"precipitation_probability":[0,0,0,0,0,0,0,5,28,18,5,0,3,0,0,0]}}
```

That rules out `json.load()`, which is fine, because we did not want it. `json.load()`
builds the entire document as Berry maps, lists and strings, several times the size of
the text it parsed. Here we want sixteen integers, and a regular expression gets them
without building anything else:

```berry
    var m = re.matchall("\\d+", body)
```

`re.matchall()` returns every non-overlapping match as a list. `\d+` is one or more
digits, written `"\\d+"` because a backslash is an escape in Berry strings too. The
needle contributes no digits, so what comes back is exactly our sixteen numbers, as
strings.

`re` needs no `import` and its matching is linear in the length of the text, so no
pattern can hang the panel.

!!! tip "When `json.load()` is the right call after all"
    When you need many fields, or the structure is nested, or you have to walk an array
    of objects, `json.load()` earns its cost and a regular expression becomes a liability.
    The rule of thumb is one or two values means `re`, a structure you have to navigate
    means `json`. [Going easy on memory](going-easy-on-memory.md) has both, measured.

---

## Not stacking up requests

If the network is slow and your timer keeps firing, you can have several requests
outstanding at once, each holding a buffer. The fix is one boolean:

```berry
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

  def on_body(body, status)
    self.in_flight = false
    # the rest of the handler follows
  end
```

Clear the flag on the **first line** of the handler, before any check that might return
early. A handler that returns before clearing it leaves the app convinced a request is
still running, and it never fetches again.

The `/ b, st -> self.on_body(b, st)` form is Berry's short closure syntax. It captures
`self`, which is what lets the handler reach your members. Writing
`def (body, status) … end` inline does the same thing; this form just keeps `loop()`
readable.

While we are here: be generous with the interval. A bare `http.get()` in `loop()` fires
once a second, which serves nobody and annoys whoever runs the API. Weather every five
to fifteen minutes is plenty, and because the interval is a `@config` field the user can
slow it down further without touching your code.

---

## Before the first answer

Between installing the app and the first successful response there are a few seconds,
or on a cold boot a few tens of seconds, where you have nothing to show. Never leave the
panel blank.

```berry
  def draw()
    clear()
    if !self.ready
      text(1, 6, "...", 0x444444)
      return
    end
    # the bars, exactly as in tutorial 1
  end
```

The other option is to skip the turn entirely. `should_show()` is asked as the rotation
arrives at your app, and returning `false` sends it straight on to the next one:

```berry
  def should_show()
    return self.ready
  end
```

Choose by what the silence means. A dimmed placeholder says "this app is here and
waiting", which is right when the data is expected any moment. `should_show()` says
"nothing to report", which is right for an app that is often genuinely empty, like a
reminder that only fires on bin day. A skipped app is marked with a dimmed chip on its
row in the web UI, so it is never a mystery where it went.

This app uses the placeholder, because after the first successful run it has stored
values and the gap only ever appears once.

---

## The finished app

```berry
# @name    Rain
# @desc    Chance of rain for the next 16 hours, from Open-Meteo
# @author  awtrix-ng
# @version 1.0
# @config  lat   text   "Latitude"   default="52.52" help="Decimal degrees, north positive"
# @config  lon   text   "Longitude"  default="13.40" help="Decimal degrees, east positive"
# @config  every number "Refresh"    default=15 min=1 max=60 unit=min
# @config  tint  color  "Bar colour" default=#0088FF

class Rain
  var hours          # 16 percentages, this hour first
  var url, tint      # built from the settings in init()
  var period, ticks  # loop() calls between fetches, and the countdown
  var in_flight      # a request is outstanding
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
    if m == nil || size(m) < 16 return end
    for i : 0 .. 15
      self.hours[i] = num(m[i], 0)
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
    line(0, h - 1, width() - 1, h - 1, 0x101820)
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * 2, h - bar, 2, bar, self.tint)
      end
    end
  end

  def on_button(btn)
    if btn == "select"
      self.ticks = 0
    end
  end
end

return Rain()
```

Open **Apps**, the `⋯` menu on the app's row, then **Settings**, and put your own
latitude and longitude in. Decimal degrees, so Berlin is `52.52` and `13.40`. Set
`Refresh` to 1 minute while you are testing and put it back afterwards. The `select`
button forces a fetch immediately, which saves a lot of waiting.

!!! note "Saving over tutorial 2 keeps its stored values"
    If you saved this over the same script name, the store still holds the sixteen
    random numbers from last time, so `ready` is already true and the panel shows those
    until the first real answer replaces them, a few seconds later. To see the `...`
    placeholder the way a new user would, save this under a fresh script name instead.

### The dim line along the bottom

One line in `draw()` has nothing to do with fetching and everything to do with being
believed:

```berry
    line(0, h - 1, width() - 1, h - 1, 0x101820)
```

Invented data is always interesting. Real weather usually is not. On a dry day every
one of those sixteen percentages is a zero or close to it, `bar` comes out as `0` for
almost every hour, and the panel goes almost completely dark. The app is working
perfectly and it looks broken.

The dim baseline fixes that for the price of one call. Now "no rain" reads as a flat
line, which is a statement, while a genuinely blank panel means something is wrong. Any
app that draws data going to zero wants some version of this.

Compare this against tutorial 2 and notice how little moved. `draw()` gained a
placeholder branch, a baseline, and lost nothing else. `loop()` calls `http.get()` where it used to
call `math.rand()`. Everything else is new methods sitting alongside the old ones, not
rewrites of them. An app built in this shape stays cheap to change.

---

## Letting the firmware draw the chart

Our hand-drawn bars taught us the coordinate system and they are worth keeping for that
reason. Now that the data is a plain list, there is a shorter way:

```berry
  def draw()
    clear()
    if !self.ready
      text(1, 6, "...", 0x444444)
      return
    end
    line(0, height() - 1, width() - 1, height() - 1, 0x101820)
    bar_chart(self.hours, self.tint, false)
  end
```

`bar_chart(list, paint, autoscale)` spans the full panel width and takes at most sixteen
values, which is why the request asks for exactly sixteen. `paint` can also be a palette
name, so `bar_chart(self.hours, "Ocean")` colours each bar by its own value. The third
argument switches autoscaling off, which matters here: a percentage should be measured
against 0 to 100, not against its own highest value, or a dry week would look alarming.

There is also `line_chart()` with the same signature, and `progress(pct)` for a single
value along the bottom row.

Which to use is a real choice, not an obvious one. The built-in chart is one line and
handles the scaling. The hand-drawn version is six lines and lets you decide what a
zero looks like, how wide a bar is, and where the colour comes from. Start with
`bar_chart()`, and reach for the loop when you want something it will not do.

---

## What you learned

- `http.get()` never blocks. It returns at once and calls you back between frames.
- `body == nil` with `status == 0` means nothing arrived. Any other status means the
  server answered, whatever it said.
- `find` and `keep` are the single largest memory saving available to a networked app,
  and they cost one line.
- `re.matchall()` pulls values out of a window without building a parse tree.
- An `in_flight` flag, cleared on the first line of the handler, stops requests stacking
  up.
- Draw a placeholder or return `false` from `should_show()`, but never show an empty
  panel.

## Next

You have written a complete, well-behaved app. The four recipes are independent and
each builds something different:

- [Countdown](recipe-countdown.md), which needs no network at all
- [MQTT status](recipe-mqtt-status.md), for a device that already has a broker
- [Sensor chart](recipe-sensor-chart.md), using the panel's own thermometer
- [Headless doorbell](recipe-doorbell.md), an app that never draws anything

And when your device starts refusing to install things, or somebody else's app starts
failing after you installed yours, [Going easy on memory](going-easy-on-memory.md) is
the chapter that explains why and what to do about it.

## Related

- [App scripting](../guides/scripting.md) for the full HTTP, regular expression and charting reference
- [Limits](../reference/limits.md#scripting) for response sizes, timeouts and how many requests may be in flight
