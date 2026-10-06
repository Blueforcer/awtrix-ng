# Keeping scripts small

Keep your script's memory use low, and see how much memory it uses.

## How it behaves

All scripts on your AWTRIX share one block of memory. A script that uses too much usually does
not break itself. It breaks other apps: icons are drawn as empty boxes, the next install is
refused with `507`, or another app stops updating. So every script should use as little as it
can.

How much memory scripts share depends on the device.
<!-- only esp32 esp32-s3 -->
On a board without PSRAM it is 96 KiB. Boards with PSRAM have much more.
<!-- /only -->
<!-- only tc002 -->
On your clock it is a quarter of the memory that is free at startup, at most 4 MiB. Smaller
clocks have as little as 96 KiB.
<!-- /only -->
[Limits](../reference/limits.md#scripting) lists the limits. Write the same way on every
device: a script cannot tell which board it will be installed on next.

---

## The checklist

Work through it from the top. The first items save the most.

1. **Filter HTTP answers with `find` and `keep`.** Never keep a whole response.
2. **Use `re` for one or two values.** Use `json.load()` only for a structure you have to walk.
3. **Keep the value, not the response.** Store the finished number or short text in a member.
   Keep the response body in a local variable only.
4. **Build nothing in `draw()`.** No joined strings, no `{…}` or `[…]` literals, no `log()`
   with built text. Build them in `init()`, `loop()` or a callback.
5. **Give every list a maximum size.** Sixteen values is enough for any chart on a <!-- only esp32 esp32-s3 -->32 × 8<!-- /only --><!-- only tc002 -->52 × 16<!-- /only --> display.
6. **Write few, longer methods.** Three or four methods make a good app. Each extra `def` costs
   about 200 bytes, even if it does nothing.
7. **Fetch once, share the result.** If several apps need the same reading, one app fetches it
   and the others read it with `shared.get()`.
8. **Poll slowly.** Fetch only as often as the data changes. Keep at most one request in flight.
9. **Keep the number of HTTPS apps low.**<!-- only esp32 esp32-s3 --> Without PSRAM, plan for about four apps
   that fetch over `https://`.<!-- /only --> Prefer MQTT or plain `http://` on your own network.
10. **Do not call `gc.collect()` in a running app.** Use it only to measure.

Each point is explained below with an example.

---

## 1. Filter HTTP answers

By default your callback receives the whole response body, up to 8 KB, as one string. The
`find` and `keep` options make AWTRIX search the response while it arrives. Your callback gets
only a small window that starts at the text you searched for:

```berry
    http.get(url, / b, st -> self.on_body(b, st),
             {'find': "\"precipitation_probability\":[", 'keep': 128})
```

Measured with a real Open-Meteo forecast of 8 028 bytes:

| Approach | Memory held afterwards |
|---|---|
| Whole body kept in a member and parsed with `json.load()` | **24 268 bytes** |
| `find` window, `re.matchall()`, sixteen numbers kept | **360 bytes** |

<!-- only esp32 esp32-s3 -->The first version holds a quarter of all script memory on a board without PSRAM, all day.<!-- /only --><!-- only tc002 -->The first version holds those 24 268 bytes all day, for sixteen numbers.<!-- /only -->
[Tutorial 3: Feed it real data](real-data.md#ask-for-less) shows how to choose the search text.

## 2. Use `re` for one or two values

`json.load()` turns the whole answer into Berry maps, lists and strings. `re.search()` creates
only the match. With the same 434-byte weather answer:

| Approach | Memory held |
|---|---|
| `json.load()`, whole answer | **2 623 bytes** |
| `re.search("\"temp\":([0-9.]+)", body)` | **217 bytes** |

The rule:

- **One or two values from an answer:** use `re`. This is most apps.
- **Several fields, nested objects or a list of records:** use `json.load()`. Narrow the answer
  with `find` first, so you parse a small window and not the whole document.

!!! tip "Write `\\` in patterns"
    A backslash starts an escape in Berry strings. Write `\d+` as `"\\d+"`. Writing `"\d+"`
    stops the script from compiling. `"[0-9]+"` works without any backslash.

## 3. Keep the value, not the response

In the callback, pick out what you need, store only that in a member, and let the rest go:

```berry
  def on_body(body, status)
    self.in_flight = false
    if body == nil return end
    var m = re.search("\"temp\":([-0-9.]+)", body)   # local, freed when the method returns
    if m == nil return end
    self.temp = num(m[1])                            # only the number is kept
  end
```

A body, a parsed map or a long list stored in `self` stays in memory as long as the app is
installed. A local variable is freed when the method returns.

## 4. Build nothing in `draw()`

`draw()` runs about forty times a second. This line builds a new string every frame:

```berry
    text(0, 6, str(self.temp) + "°", 0xFFFFFF)     # 23 bytes, forty times a second
```

That is about 920 bytes of garbage every second. It is cleaned up, but the cleanup takes time
from every frame. The same applies to `{…}` maps, `[…]` lists and `log()` lines with built text.
Build them once and keep them:

```berry
  def init()
    self.fx = {"speed": 0.3, "palette": "Ocean"}   # built once
  end

  def draw()
    effect("Plasma", self.fx)                      # reused every frame
  end
```

When the text depends on a value, rebuild it only when the value changes:

```berry
    var r = round(t, 1)
    if r != self.shown
      self.shown = r
      self.label = str(r) + "°"
      self.x = width() - text_ink_width(self.label)
    end
```

## 5. Give every list a maximum size

A list you add to in `loop()` grows forever unless you trim it.

| List | Memory held |
|---|---|
| 16 numbers | **360 bytes** |
| 256 numbers | **4 200 bytes** |

Trim it in place after each `push()`:

```berry
    self.hist.push(v)
    if size(self.hist) > 16
      self.hist.remove(0)
    end
```

Sixteen is a good limit: `bar_chart()` and `line_chart()` draw at most sixteen values.

## 6. Write few, longer methods

Every `def` stays in memory as long as the app is installed.

| Script | Memory after install |
|---|---|
| Two methods | **678 bytes** |
| The same two, plus eight methods that only `return 1` | **2 306 bytes** |
| A chart drawn with two methods | **1 705 bytes** |
| The same chart drawn with eleven methods | **3 778 bytes** |

Many small helper methods are the most common reason why an install is refused on a device that
otherwise has room.

## 7. Fetch once, share the result

If three apps need the temperature, only one of them fetches it. In that app, installed as
`Weather`, the callback publishes the finished value:

```berry
    shared.set("temp", self.temp)
```

Every other app reads it:

```berry
    var t = shared.get("Weather.temp", 0)
```

This works for HTTP, MQTT and Modbus readings. Share the finished number or short text, never
the raw response. Before you show a shared value, check `shared.age()` and show a placeholder
when the value is missing or too old. Publish only after a successful read.

A [background script](../guides/scripting/several-apps.md#running-without-ever-being-shown) can do the
fetching without taking a turn on the display. The
[meter example](../guides/modbus.md#share-one-device-across-several-apps) shows one reader
that feeds two display apps.

[Modules](../guides/scripting/several-apps.md#sharing-code-between-scripts) are for shared code, not shared
data. A fetch inside a module still runs once for every app that calls it. If only one app
needs the data, keep it in that app.

## 8. Poll slowly

- Keep at most one request in flight per app. [Tutorial 3: Feed it real data](real-data.md#not-stacking-up-requests)
  shows the `in_flight` flag.
- Fetch only as often as the data changes. Weather every 5 to 15 minutes is enough.
- Make the interval a `# @config … number` setting, so the user can slow it down.
- Give apps different intervals, so their requests do not all start at the same moment.

## 9. Keep the number of HTTPS apps low

An `https://` connection needs a large amount of memory for a short time.<!-- only esp32 esp32-s3 --> Without
PSRAM, about four apps that fetch over HTTPS is the practical limit.<!-- /only --> When apps that work alone
start to fail together, this is usually the reason.

Cheaper sources:

- **MQTT** – the value arrives small and ready to use. No connection setup, no response body,
  no polling.
- **Plain `http://`** on your own network – no encryption, so much less memory.

<!-- only esp32 esp32-s3 -->
`https://` requests wait about 15 seconds after a boot or a Wi-Fi reconnect. Show a
placeholder until the first answer arrives.
<!-- /only -->

## 10. Do not call `gc.collect()` in a running app

AWTRIX cleans up memory by itself. Calling `gc.collect()` in `draw()` or `loop()` only costs
time. Use it only in a measuring script (see below).

---

## See how much memory a script uses {#measure}

### After an install

Every install writes one line to the device log. Open **Log** in the web UI, or read
`GET /api/v1/logs`:

```
[script:Rain] vm heap +5040 bytes (shared 21940)
```

- The first number is what this app added: 5 040 bytes.
- `shared` is the total of all scripts on the device: 21 940 bytes.

Compare the total with `scriptHeapBudgetBytes` from
[Device state](../reference/device.md). When the total reaches that limit, new installs are
refused.<!-- only esp32 esp32-s3 --> The line also shows the device's free memory before and after the install.<!-- /only -->

Install, use the app for a while, then look at the total again. A total that keeps growing
points to a list or member that is never trimmed.

The **Dashboard** shows **Free RAM** for the whole device.

### For one piece of code

`gc.allocated()` returns the memory all scripts use right now, in bytes. To measure one step,
clean up, read, run the step, clean up again, read again:

```berry
# @headless true

import gc
import json

class Probe
  def setup()
    var body = "{\"temp\":21.5,\"hum\":52}"

    gc.collect()
    var before = gc.allocated()
    var data = json.load(body)
    gc.collect()
    var after = gc.allocated()

    log("tree costs " + str(after - before) + " bytes")
  end
end

return Probe()
```

`# @headless true` makes it a [background script](../guides/scripting/several-apps.md#running-without-ever-being-shown):
it runs, but does not take a turn on the display, so it needs no `draw()`. `import gc` goes below
the `# @…` header lines and above the class. Save the script and read the result in **Log**.
Delete it when you are done.

Always call `gc.collect()` before both readings. Without it, the numbers change from run to
run.

---

## When an install is refused {#install-refused}

A `507` with `not enough memory to compile` or `heap too fragmented to compile` means the
script was not installed. Try these steps in this order:

1. **Restart AWTRIX** and save the script again. This often fixes it without any change.
2. **Delete a script you do not use.** Its memory is free at once.
3. **Make the script smaller**, with fewer and longer methods (checklist item 6).

Saving changes to a script that is already installed needs less free memory than a new
install. So you can often still edit an existing script when new installs are refused.

[Not enough memory to compile](../guides/scripting/troubleshooting.md#not-enough-memory-to-compile)
has the details.

---

## Example: before and after

The same app, fetching the same forecast, written twice.

**Wasteful:**

```berry
  def on_body(body, status)
    self.body = body                    # keep the response, might need it later
    self.data = json.load(body)         # parse the whole thing
  end

  def draw()
    clear()
    var hourly = self.data.find("hourly")
    var p = hourly.find("precipitation_probability")
    var w = width() / size(p)
    for i : 0 .. size(p) - 1
      var bar = p[i] * height() / 100
      rect_fill(i * w, height() - bar, w, bar, 0x0088FF)
    end
    text(0, 6, "Rain " + str(p[0]) + "%", 0xFFFFFF)
  end
```

**Small**, the app from [tutorial 3](real-data.md):

```berry
  def on_body(body, status)
    self.in_flight = false
    if body == nil return end
    var m = re.matchall("[0-9]+", body)     # body is a 128-byte window
    if m == nil || size(m) < 16 return end
    for i : 0 .. 15
      self.hours[i] = num(m[i], 0)          # written in place, no new list
    end
    self.ready = true
  end

  def draw()
    clear()
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
```

| | Wasteful | Small |
|---|---|---|
| Memory held between fetches | 24 268 bytes | **360 bytes** |
| New memory per frame | a string and two lookups | **none** |
<!-- only esp32 esp32-s3 -->
| Share of 96 KiB script memory | about a quarter | less than half a percent |
<!-- /only -->

Four changes made the difference:

1. `find` and `keep` on the request: the body is 128 bytes instead of 8 028.
2. `re.matchall()` instead of `json.load()`.
3. The values go into a list that already exists.
4. `draw()` only reads members. It builds nothing.

---

## Related

- [Tutorial 3: Feed it real data](real-data.md) – `find`, `keep` and the callback in a full app
- [Scripting guide](../guides/scripting/index.md) – the full API
- [Limits](../reference/limits.md#scripting) – every limit in one table
