# Going easy on memory

Every script on your AWTRIX draws from one pool. On a board without PSRAM that pool is
about 96 KB, and it is shared not only between your scripts but with the icon decoder,
the pushed apps holding their content, and the room an HTTPS handshake needs while it
is happening.

This has a consequence that catches people out. A wasteful script does not usually
break itself. It breaks its neighbours. Icons start drawing as holes, the next script
you try to install is refused with a `507`, and somebody's weather app stops updating.
Nothing points at the app that actually caused it.

This chapter is how to write scripts that are good neighbours, in order of how much
each thing is worth.

!!! note "About the numbers on this page"
    Every figure below was measured, not estimated, by running the code in the
    [simulator](../advanced/simulator.md) and reading `gc.allocated()` around it. They
    are the Berry interpreter's own numbers and they are the same on your device,
    because it is the same interpreter running the same bytecode.

    Figures for the device's overall free memory are a different matter. Those depend
    on your board, your build and what else is installed, so this page describes them
    in words and leaves the exact values to [Limits](../reference/limits.md#scripting).

---

## Where the memory actually goes

Three things consume it, and they behave differently.

**What your app holds.** Members, lists, maps, strings, and the class itself. This is
memory you are using for as long as the app is installed. It is the easiest to control
and the one people get wrong most often.

**The moment of compiling.** Installing a script needs roughly the source size again in
free memory, on top of what the finished app will occupy. Compiling is the expensive
moment, not running. That is why an install can be refused on a device where five apps
are running quite happily.

**What is in flight.** An HTTP response arriving, an icon being decoded, a TLS handshake
being negotiated. These are large, short-lived, and they need their memory *now*. They
are also the ones that fail first when the heap is under pressure, which is why the
symptom of a greedy script is so often an unrelated app misbehaving.

---

## Measure, do not guess

Two instruments, both free.

**The install line in the log.** Every script install prints what it cost:

```
[script:Rain] vm heap +5040 bytes (shared 21940)
```

The first number is what this app now occupies. The second is the total across every
script on the device. Watch that second number as you install things and you have a
running account of where you stand. It is in the web UI console and in
`GET /api/v1/logs`.

**`gc.allocated()` from inside a script.** This reports the live total in bytes. To
measure one operation, collect first, take a reading, do the thing, collect again, take
another:

`import gc` goes at the top of the file, below the `# @config` header and above the
class. The measurement itself goes wherever the code you are suspicious of lives:

```berry
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

  def draw()
    clear()
  end
end

return Probe()
```

Save that, and the number appears in the web UI console. A `# @headless true` line in
the header keeps a throwaway measuring script out of the rotation while you work.

The two `gc.collect()` calls matter. Without them you are measuring garbage that has not
been swept yet, and the number moves around between runs.

!!! warning "Never call `gc.collect()` in `draw()`"
    It is fine in a measurement, and it is a mistake in a running app. Berry collects on
    its own schedule, and forcing a collection forty times a second costs time you do
    not have inside a 25 ms frame budget.

A single reading tells you less than a trend. Install, watch the shared total, use the
app for a while, look again. Memory that keeps climbing is the thing worth chasing.

---

## The levers, strongest first

### 1. Never hold the response

This is the largest single saving available to any networked app, and it costs one line.

By default your callback receives the response body, up to 8 KB, as a Berry string. The
`find` and `keep` options make AWTRIX scan the response as it streams past and hand you
only a small window:

```berry
    http.get(url, / b, st -> self.on_body(b, st),
             {'find': "\"precipitation_probability\":[", 'keep': 128})
```

Measured on a real Open-Meteo hourly forecast of 8 028 bytes:

| Approach | Held afterwards |
|---|---|
| Body parked in a member, parsed with `json.load()` | **24 268 bytes** |
| `find` window, `re.matchall()`, sixteen integers kept | **360 bytes** |

Sixty-seven times less, for data that was never more than sixteen numbers. The larger
figure is a quarter of everything the device has for scripts, held by one app, all day.

Notice that the parse tree is roughly twice the size of the text it came from, and you
are holding both. That ratio depends on the shape of the document. Berry shares
identical strings, so a body full of repeated values parses more cheaply than one full
of distinct ones, which is worth knowing and not worth relying on.

### 2. Prefer `re` to `json` for one or two values

`json.load()` builds the entire document as Berry maps, lists and strings.
`re.search()` allocates the match and its groups and nothing else. On the same
434-byte weather response:

| Approach | Held |
|---|---|
| `json.load()`, whole tree | **2 623 bytes**, six times the source text |
| `re.search("\"temp\":([0-9.]+)", body)` | **217 bytes** |

Both drop to zero the moment you release them, so this is about peak, not about leaks.
Peak is what decides whether the icon decoder finds room in the same second.

The rule is not "never use `json`". It is:

- **One or two values out of a response:** `re`. This is most apps, most of the time.
- **A structure you genuinely have to walk**, several fields, nested objects, an array
  of records: `json.load()` earns its cost, and a regular expression trying to do the
  same job would be fragile and slower to write.

If you do reach for `json.load()`, narrow the input with `find` first, so you are
parsing a window rather than a document.

!!! tip "Remember `\` is an escape in Berry strings too"
    The pattern `\d` is written `"\\d+"`. Writing `"\d+"` is not a runtime surprise, it
    is a compile error, because Berry reads the backslash as the start of an escape.
    `"[0-9]+"` sidesteps the question entirely.

### 3. Keep the value, drop the source

In the callback, extract what you need, assign *that* to a member, and let everything
else go.

```berry
  def on_body(body, status)
    self.in_flight = false
    if body == nil return end
    var m = re.search("\"temp\":([-0-9.]+)", body)   # local, collected on return
    if m == nil return end
    self.temp = num(m[1])                            # a number, 0 extra bytes
  end
```

A body, a parsed map or a long list assigned to `self` holds its memory until the device
reboots. A local holds it until the method returns. That is the whole difference, and it
is one keyword.

### 4. Allocate nothing in `draw()`

`draw()` runs about forty times a second. One innocent string concatenation there:

```berry
    text(0, 6, str(self.temp) + "°", 0xFFFFFF)     # 23 bytes, forty times a second
```

That is **23 bytes** per frame, measured, which is **920 bytes every second** of pure
churn. It never leaks, because the collector keeps up. It does mean the collector is
running constantly instead of rarely, inside a frame budget of 25 ms.

The same applies to every `{…}` map literal, every `[…]` list literal, and every
`log()` line with a built string in it. Build them once and keep them:

```berry
  def init()
    self.fx = {"speed": 0.3, "palette": "Ocean"}   # built once
  end

  def draw()
    effect("Plasma", self.fx)                      # reused every frame
  end
```

When the displayed text depends on a value that rarely changes, rebuild it only when the
value actually changes:

```berry
    var r = round(t, 1)
    if r != self.shown
      self.shown = r
      self.label = str(r) + "°"
      self.x = width() - text_ink_width(self.label)
    end
```

On a still afternoon that runs a handful of times an hour instead of 144 000 times.

### 5. Bound every collection

A list you push to in `loop()` grows forever unless you stop it.

| List | Held |
|---|---|
| 16 integers | **360 bytes** |
| 256 integers | **4 200 bytes** |

The second one is what you get after a little over four hours of sampling once a minute,
and it will keep going. Trim in place rather than rebuilding:

```berry
    self.hist.push(v)
    if size(self.hist) > 16
      self.hist.remove(0)
    end
```

Sixteen is a natural ceiling on this hardware, because `bar_chart()` and `line_chart()`
take sixteen values and a 32 pixel panel has nowhere to put more.

### 6. Fewer, larger methods

Every `def` is a function object that exists for as long as the app is installed, and
each one costs before its body does anything at all.

Eight methods whose entire body is `return 1`, added to an otherwise identical script:

| Script | Installed |
|---|---|
| Two methods | **678 bytes** |
| The same two, plus eight trivial helpers | **2 306 bytes** |

That is **1 628 bytes for eight empty methods**, a little over 200 bytes each.

It shows up in real code too. The same chart-drawing routine, written twice:

| Written as | Installed |
|---|---|
| Two methods | **1 705 bytes** |
| Eleven methods | **3 778 bytes** |

Twice the memory for the same picture. Three or four well-named methods make a good app.
Ten one-line helpers make an expensive one, and they are the most common reason an
install is refused for memory on a device that otherwise has room.

This is one of the few places where the advice runs against normal programming taste.
On a general-purpose computer, decomposing into small functions is free and usually
right. Here it is not free.

### 7. One fetch, many readers

If three apps want the temperature, three apps do not need to fetch it. One polls and
publishes, the others read:

```berry
    shared.set("temp", t)                       # in the fetching app
    var t = shared.get("Weather.temp", 0)       # in every other app
```

One HTTP buffer, one TLS handshake, one parse. `shared` holds scalars only - store the
finished value there, never the raw body you got it out of. Use `shared.age()` to check
how old a value is before trusting it.

A [headless app](recipe-doorbell.md) that does nothing but fetch and publish is a good
shape for this. It never takes a turn on the panel and every drawing app gets its
numbers for free.

---

## HTTPS is the expensive guest

A TLS handshake needs a large block of memory, all at once, for a short time. It is
routinely the single biggest allocation your device makes, larger than any script.

Three consequences worth designing around:

**There is a practical ceiling on how many apps can poll HTTPS.** It is lower than
people expect, and it depends on the board. When apps that worked individually start
failing together, this is usually why.

**Requests are held for about fifteen seconds after a boot or a Wi-Fi reconnect**, while
the network stack settles. That is by design. Draw a placeholder and let it happen.

**Staggering helps, and being slower helps more.** Two apps polling every five minutes
will eventually collide. Different intervals, and intervals no faster than the data
actually changes, keep them out of each other's way. Make the interval a
`# @config … number` field so the user can slow it down without editing anything.

Where you have the choice, prefer the cheaper source. MQTT is the cheapest data on the
device: the payload arrives small and already extracted, with no handshake, no 8 KB
buffer and no polling interval to tune. Plain `http://` on your own network avoids the
handshake too.

---

## Free is not the same as free in one piece

There are two different questions about memory, and they have different answers.

*Is there enough?* is about the total. *Is there enough in one piece?* is about
fragmentation, and it is the one that produces the confusing failures.

Memory breaks up over time. Apps come and go, responses arrive and are released, icons
are decoded and discarded. What is left is the same number of free bytes scattered in
smaller and smaller pieces. A compile that needs one contiguous block the size of your
source then fails, on a device reporting plenty of free memory, with:

```
507  heap too fragmented to compile
```

Its close relative is `507 not enough free memory to compile`, which is the simpler
problem: the total really is too low.

Three things clear either one, in the order worth trying:

**Reboot.** This returns the largest usable block to full size and costs you a few
seconds. Try it before shortening anything, because a fragmentation failure is not a
verdict on your script.

**Delete a script you are not using.** Freeing an installed app frees its memory
immediately, no reboot needed.

**Shorten the script, written as fewer and longer methods.** Lever 6 above, and the one
that actually changes the arithmetic.

Re-saving an existing script is judged more leniently than a new install, so when new
installs keep being refused, editing what is already there still works.

---

## Before and after

The same app, fetching the same forecast, written twice.

**The wasteful version.** Nothing here is unreasonable. It is how you would write it on
a machine with memory to spare.

```berry
  def on_body(body, status)
    self.body = body                    # keep the response, might need it later
    self.data = json.load(body)         # parse the whole thing
  end

  def draw()
    clear()
    var hourly = self.data.find("hourly")
    var p = hourly.find("precipitation_probability")
    for i : 0 .. size(p) - 1
      var bar = p[i] * height() / 100
      rect_fill(i * 2, height() - bar, 2, bar, 0x0088FF)
    end
    text(0, 6, "Rain " + str(p[0]) + "%", 0xFFFFFF)
  end
```

**The careful version**, which is the app from [tutorial 3](real-data.md):

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
    line(0, h - 1, width() - 1, h - 1, 0x101820)
    for i : 0 .. size(self.hours) - 1
      var v = self.hours[i]
      var bar = v * h / 100
      if bar > 0
        rect_fill(i * 2, h - bar, 2, bar, self.tint)
      end
    end
  end
```

| | Wasteful | Careful |
|---|---|---|
| Held between fetches | 24 268 bytes | **360 bytes** |
| Allocated per frame | a map lookup, a list lookup and a string | **nothing** |
| Share of a 96 KB script heap | about a quarter | under half a percent |

Four changes did all of that:

1. `find` and `keep` on the request, so the body was 128 bytes rather than 8 028.
2. `re.matchall()` instead of `json.load()`, so no tree was ever built.
3. Values written into a list that already exists, rather than a new list per fetch.
4. `draw()` reading a member instead of navigating a structure and building a string.

None of them made the app harder to read. The careful version is arguably the clearer
of the two, because `draw()` no longer needs to know anything about the shape of a
weather API.

---

## When somebody reports a problem

If a user tells you their device refuses to install anything, or an app stopped
updating, or icons have started coming out as grey boxes, this chapter is the diagnosis.
In order:

1. Ask them to **reboot** and try again. If that fixes it, it was fragmentation.
2. Ask them to **delete a script they are not using**, and check the shared total in the
   log afterwards.
3. Look for **a member holding a response body or a parsed map** in whatever they
   installed most recently.
4. Look for **an unbounded list** that grows in `loop()`.
5. Count the **HTTPS pollers** and their intervals.

An **ESP32-S3 with PSRAM** moves the Berry heap into PSRAM and raises the ceiling to
megabytes, with nothing to configure. If you are pushing scripting hard, that is the
board to be on. Write the same way regardless, because you cannot tell from inside a
script which board you are running on, and neither can whoever installs it next.

---

## Related

- [3. Feed it real data](real-data.md) for `find`, `keep` and the callback shape in context
- [App scripting](../guides/scripting.md) for the full API and what each call costs
- [Limits](../reference/limits.md#scripting) for every cap in one table
- [Simulator](../advanced/simulator.md) if you want to reproduce these measurements
