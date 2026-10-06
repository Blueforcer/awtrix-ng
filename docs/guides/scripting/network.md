# Network

Fetch data from the internet, pick out the values you need, and talk to your smart home over
MQTT.

## What you get

<a id="http"></a><a id="a-complete-fetch"></a>

```berry
import json

class Api
  var label, ticks

  def init()
    self.label = "..."                 # shown until the first answer
    self.ticks = 0
  end

  def loop()
    if self.ticks <= 0
      self.ticks = 60                  # once a minute is plenty
      http.get("https://example.com/api", / body, status -> self.on_body(body, status))
    end
    self.ticks -= 1
  end

  def on_body(body, status)
    if body == nil return end          # no answer this time
    var data = json.load(body)
    if !isinstance(data, map) return end
    var value = data.find("temperature")
    if value != nil self.label = str(value) end
  end

  def draw()
    clear()
    text(1, 6, self.label, 0xFFFFFF)
  end
end

return Api()
```

Once a minute, the app fetches an answer in JSON and shows its `temperature` field. Until the
first answer arrives, it shows `...`.

## How it behaves

- **A request returns at once.** `http.get()` sends it, and your script goes on. The display
  never waits for the network.
- **Your [callback](index.md#callbacks) runs once, a little later.** `body` is the answer as text
  and `status` its HTTP code. When no answer comes at all, for example without Wi-Fi, `body` is
  `nil` and `status` is `0`.
- **Use the answer in the callback.** `http.get()` itself returns nothing, and the answer is not
  there on the next line. Keep what you need in a member: the callback updates it, and `draw()`
  only paints it.
- **`loop()` runs about once a second, also while your app is hidden.** Start requests there,
  spaced out with a counter, so the data is ready when your app comes round.
- **MQTT works the same way.** `mqtt.subscribe()` returns at once, and your callback runs for
  every message.

## Send a token, a header or a body {#methods-headers-and-a-request-body}

Many APIs want a token in a header, or data in the body of the request. `opts`, the last argument
of every request, takes `headers` as a plain map. `http.post()`, `http.put()` and `http.patch()`
take the body before the callback:

```berry
http.get("https://api.example.com/v1/me", / b, st -> self.on_body(b, st),
         {'headers': {'Authorization': "Bearer " + self.token,
                      'Accept': "application/json"}})

http.post("https://hooks.example.com/panel", json.dump({'state': "up"}),
          / b, st -> self.on_sent(st),
          {'headers': {'Content-Type': "application/json"}})
```

An answer with an error code reaches your callback too, with its body. A rejected token comes
back as a real answer, for example with `status` `401`, so check `status` before you use the
body. [Details](#http-calls) lists every method.

<a id="keep-tokens-private"></a>

!!! warning "A token in a script is only as private as AWTRIX"
    Script source is stored as plain text, and `GET /api/v1/apps/script/<name>` returns it. That
    route needs a login only when you turn on [HTTP authentication](../../reference/http.md#authentication)
    with `authEnabled`. By default there is none. Without a login, anyone who can reach AWTRIX can
    read a token in your script.

    A login is not encryption. Basic auth travels over plain HTTP, so on an untrusted network the
    password and the script can be read in transit. The same is true for a config backup.

<!-- only esp32 esp32-s3 -->
    Outbound HTTPS is encrypted, but the clock does **not** check the server certificate.
<!-- /only -->
<!-- only tc002 -->
    Outbound HTTPS checks the certificate and host name. An untrusted or mismatched certificate
    fails the request.
<!-- /only -->

    So: set a login, and use a token that can only read what the script needs and that you can
    revoke on its own.

## Pick one field out of a big answer {#picking-one-field-out-of-a-big-answer}

An answer is kept up to 8 KB. To read a field from a bigger one, let AWTRIX search the answer as
it arrives and keep only the part you need:

```berry
http.get(url, / b, st -> self.on_body(b, st),
         {'find': "\"followerCount\":", 'keep': 64})
```

`b` then starts **at** the match, the search text included, so you can read the value right after
it. The size of the document does not matter: a field a megabyte in works as well as one at the
start.

- **`find`** is the text to search for. The part from its first match on is kept.
- **`keep`** is how many bytes are kept from the match on, 256 by default.

If the text never comes, `b` is `nil` and `st` is the **real** status code. `find` works with any
method, headers and request body.

## Find a value with a pattern {#regular-expressions}

`re` finds text in an answer when `string.find` gets awkward: a value with changing text around
it, several matches, or an HTML page without an API. No `import` is needed.

```berry
var m = re.search("\"followerCount\":(\\d+)", body)
if m != nil
  self.count = num(m[1])
end

re.matchall("\\d+", "a1 b22 c333")   # ["1", "22", "333"]
```

`re.search()` answers `nil`, or a list with the whole match at `[0]` and the groups from `[1]`
on. `\` is also an escape character in Berry strings, so the pattern `\d` is written `"\\d"`.
[Pattern syntax](#pattern-syntax) lists what a pattern may hold.

## Talk to your smart home over MQTT {#mqtt}

`mqtt.subscribe()` calls your function for every message on a topic:

```berry
class Sensor
  var last

  def init()
    self.last = "-"
  end

  def setup()
    mqtt.subscribe("sensor/+/temp", / topic, payload -> self.set(payload))
  end

  def set(payload)
    self.last = payload
  end

  def draw()
    clear()
    text(1, 6, self.last, 0xFFFFFF)
  end
end

return Sensor()
```

`+` matches one level of the topic and `#` the rest. The callback gets the topic the message
arrived on, so a subscriber to `sensor/+/temp` can tell which sensor sent it. `mqtt.publish()`
sends a message:

```berry
mqtt.publish("home/panel/status", "up")
```

Both calls do nothing while MQTT is not set up, so a script with MQTT parts still runs without a
broker. Topics do not have to be under AWTRIX's own [MQTT prefix](../mqtt.md#the-prefix).

## Read a Modbus device {#modbus-tcp}

<a id="display-a-measurement"></a><a id="choose-what-to-read"></a><a id="convert-the-values"></a><a id="share-one-device-across-several-apps"></a><a id="when-a-read-fails"></a>
Read measurements from an energy meter, inverter or other Modbus TCP device on your network with
`import modbus`. [Modbus in scripts](../modbus.md) shows a complete app, the four read calls, how to
convert the values and how several apps share one device.

<!-- only tc002 -->
## Talk to a line-based service {#tcp-connections}

`import tcp` opens a plain TCP connection and hands your script the text it
receives line by line: a status feed, a telnet-style device or a mining pool.
[TCP in scripts](../tcp.md) shows an example, the three calls and what ends a connection.
<!-- /only -->

## A complete example: weather {#a-real-one-weather}

**`weather.ax`** shows the current temperature from Open-Meteo (no API key needed) next to a sky
symbol drawn from rectangles and circles. Upload it, enter your coordinates in the web UI, and it
works without anyone opening the source. The blocks below are the whole file, in order.

```berry
# @name    Weather
# @desc    Current temperature via Open-Meteo (no API key)
# @author  awtrix-ng
# @version 1.0
# @config  lat text "Latitude"  default="52.52" help="Decimal degrees, north positive"
# @config  lon text "Longitude" default="13.40" help="Decimal degrees, east positive"

import json

class Weather
  var url            # built once from the configured coordinates
  var temp           # last reading in °C, nil until the first success
  var label          # the finished string draw() paints, built once per fetch
  var sky            # 0 clear, 1 cloud, 2 rain, 3 snow, 4 storm
  var ticks          # loop() calls left until the next fetch
  var in_flight      # true while a request is outstanding
```

The [header](sharing.md#the-header) describes the app and adds two
[settings](storage.md#settings-the-user-can-change): latitude and longitude get a **⚙** on the Apps tab, so
whoever installs it enters their own place without touching the code.

Every member is declared with `var` at the top and set in `init()`. `sky` is a simple category, not
the raw weather code, and `label` is a finished string. Both are worked out once per fetch, so
`draw()` has nothing left to calculate.

```berry
  def init()
    self.url = "https://api.open-meteo.com/v1/forecast?current_weather=true" +
               "&latitude=" + store.get("lat") + "&longitude=" + store.get("lon")
    self.temp = store.get("temp")
    self.sky = store.get("sky", 0)
    self.label = self.temp == nil ? nil : self.format(self.temp)
    self.ticks = 0
    self.in_flight = false
  end
```

`init()` runs when the stored values are already back. So right after a boot, the display shows the
last known reading instead of `...` until the first request returns. `store.get(key)` without a
default returns `nil`, which `draw()` checks for. `lat` and `lon` always have a value, because the
header gives them defaults. Do not repeat those defaults in the code.

Changing the location in the web UI restarts the app, so `init()` builds a new `url` by itself.

```berry
  # --- fetch ---

  def format(t)
    var half = t >= 0 ? 0.5 : -0.5
    return str(int(t + half)) + "°"
  end

  def classify(code)
    if code >= 95 return 4 end
    if code >= 85 return 3 end
    if code >= 80 return 2 end
    if code >= 71 && code <= 77 return 3 end
    if code >= 51 return 2 end
    if code >= 2  return 1 end
    return 0
  end
```

`int()` cuts off towards zero, so `format()` adds a half with the same sign as the number. That way
−3.4 °C rounds to −3 and −3.6 °C to −4. `classify()` sorts the WMO weather codes into the five
symbols the display can draw. Both run once per fetch, never per frame.

```berry
  def on_body(body, status)
    self.in_flight = false
    if body == nil return end

    var data = json.load(body)
    if !isinstance(data, map) return end
    var cw = data.find("current_weather")
    if !isinstance(cw, map) return end
    var t = cw.find("temperature")
    if t == nil return end

    self.temp = t
    self.sky = self.classify(int(cw.find("weathercode", 0)))
    self.label = self.format(t)
    store.set("temp", t)
    store.set("sky", self.sky)
  end
```

Four habits worth copying:

- **One `nil` check covers every network failure:** offline, refused, or no answer within 30
  seconds. The checks after it catch an answer that is not the JSON you expect. This API needs no
  login, so `status` is not checked. A script with a token would check it.
- **`find()` instead of `[]`**, because `[]` raises on a missing key. If the API changes, your app
  should not break.
- **The store is written only when the data is good**, so a bad response cannot replace the value
  that comes back after a reboot.
- **The display string is built here**, not in `draw()`.

`json.load()` is fine here because the answer is a few hundred bytes. For an API that returns
kilobytes, use [`find` and `keep`](#picking-one-field-out-of-a-big-answer), so only the part you
need is kept.

The [callback](index.md#callbacks) passed to `http.get()` below hands the answer to `on_body()`.

```berry
  def loop()
    if self.ticks <= 0
      self.ticks = 300
      if !self.in_flight
        self.in_flight = true
        http.get(self.url, / b, st -> self.on_body(b, st))
      end
    end
    self.ticks -= 1
  end
```

`loop()` runs whether or not the app is shown, so the data is fresh when the app comes round.
300 calls at about one a second is roughly five minutes. The `in_flight` check makes sure a slow
network cannot pile up requests: a stuck request delays the next one instead of adding another.

```berry
  # --- draw ---

  def glyph()
    if self.sky == 0
      circle_fill(3, 3, 2, 0xFFAA00)
      pixel(3, 0, 0xFF6600) pixel(0, 3, 0xFF6600)
      pixel(6, 3, 0xFF6600) pixel(3, 6, 0xFF6600)
      return
    end

    circle_fill(2, 4, 1, 0x8899AA)
    circle_fill(5, 3, 2, 0x8899AA)
    rect_fill(1, 4, 6, 2, 0x8899AA)

    if self.sky == 2
      pixel(2, 7, 0x3388FF) pixel(4, 6, 0x3388FF) pixel(6, 7, 0x3388FF)
    elif self.sky == 3
      pixel(2, 7, 0xCCEEFF) pixel(4, 6, 0xCCEEFF) pixel(6, 7, 0xCCEEFF)
    elif self.sky == 4
      pixel(4, 6, 0xFFDD00) pixel(3, 7, 0xFFDD00)
    end
  end
```

The symbol is drawn, not loaded as an icon, in the leftmost eight columns. Rectangles and circles
always work and need nothing installed. (With icons you would add an
[`# @icons`](drawing.md#the-icons-your-script-needs) line.) Three of the five symbols share the cloud and
differ only in a few pixels below it.

```berry
  def draw()
    clear()

    if self.label == nil
      text(1, 6, "...", 0x666666)
      return
    end

    self.glyph()

    var c = 0x00FF00
    if self.temp >= 28
      c = 0xFF4000
    elif self.temp <= 0
      c = 0x00AAFF
    end
    text(9 + (width() - 9 - text_ink_width(self.label)) / 2, 6, self.label, c)
  end
```

`draw()` paints **only from saved state**. It never fetches, parses or waits: `loop()` and the
callback have done that already. Most network apps should work this way.

Before the first reading, the app shows a dimmed `...`, never an empty display. The color tells the
range at a glance: blue at or below freezing, red from 28 °C, green in between. The number is
centered in the columns right of the symbol, measured with `text_ink_width()`, because `-13°` is
wider than `5°`.

```berry
  def on_button(btn)
    if btn == "select"
      self.ticks = 0
    end
  end
end

return Weather()
```

Select fetches again on the next `loop()`. Left and right switch apps as usual. The file ends with
`return Weather()`, the app AWTRIX runs.

## Good to know

- **A bare `http.get()` in `loop()` sends a request every second.** Count down in a member, as
  `ticks` does in the examples.
- **`data["key"]` stops your app when the key is missing.** Use `data.find("key")`, which answers
  `nil`.
- **An answer is cut at 8 KB.** For a bigger one, keep only the part you need with
  [`find` and `keep`](#picking-one-field-out-of-a-big-answer).
- **A `POST` or `PUT` does not follow redirects reliably.** Send requests with a body straight to
  the final address.
- **An MQTT payload is text.** Convert it with [`num()`](time-buttons-sensors.md#numbers) before
  you compare or calculate.

## Details

### HTTP calls

| Call | Does |
|---|---|
| `http.get(url, cb, opts?)` | fetches `url` |
| `http.post(url, body, cb, opts?)` | sends `body` with `POST` |
| `http.put(url, body, cb, opts?)` | sends `body` with `PUT` |
| `http.patch(url, body, cb, opts?)` | sends `body` with `PATCH` |
| `http.delete(url, cb, opts?)` | sends `DELETE` |
| `http.request(method, url, cb, opts?)` | sends any method. `opts` may carry `body` |

Each call returns at once. `cb(body, status)` runs once, later. `opts` is an optional map:

| Key | Value | Meaning |
|---|---|---|
| `headers` | a map of names to values | sent with the request |
| `find` | text | keep only the part of the answer from the first match on |
| `keep` | bytes, `256` by default | how much is kept from the match on. `cap` can make it smaller, not larger |
| `cap` | bytes, 8 KB by default | the most of the answer that is kept |
| `body` | text | the request body of `http.request()` |

- AWTRIX sets `Host`, `Content-Length`, `Transfer-Encoding` and `Connection` itself and ignores
  them in your headers. All other headers are yours.
- A large `cap` only works when the device has that much memory free. If memory runs out while the
  answer is still arriving, the whole answer is dropped: your callback gets `(nil, status)` with
  the real status code. So do not rely on a big `cap`. Use `find`.

### What your callback gets

- **Any real answer, including 4xx and 5xx,** reaches your callback with its `body`. That is where
  an API explains what went wrong.
- **No answer:** no Wi-Fi, an unknown host, a refused connection, too many requests at once, or no
  answer within 30 seconds. `status` is `0` and `body` is `nil`.
- **A request that cannot be sent**, with an unknown method or a malformed header, gets
  `cb(nil, 0)` at once, like a network error.
- **`find` text that never comes** gives `(nil, status)` with the real status code, so you can tell
  it from a network failure.
- Only `http://` and `https://` addresses work.
- An answer is kept up to 8 KB, or up to `cap` if you set one. It is less when the device is short
  of free memory.
- Each script can have **8 requests** open at once.
- `GET` follows redirects. A `POST` or `PUT` does not follow them reliably.
<!-- only esp32 esp32-s3 -->
- For about 15 seconds after the clock joins Wi-Fi, `https://` requests wait, then run. The first
  result simply arrives a few seconds later. When memory is too tight for HTTPS, the request is
  retried for about 20 seconds before your callback gets a failure.
<!-- /only -->

### Pattern syntax

| Call | Does |
|---|---|
| `re.search(pattern, text)` | first match anywhere: `nil`, or a list with the whole match at `[0]` and the groups from `[1]` on |
| `re.match(pattern, text)` | the same, but the match must start at the first character |
| `re.matchall(pattern, text)` | every match that does not overlap the one before, full matches only, as a list |

- Supported: literals, `.`, `[a-z0-9]` and `[^...]` classes, `\d \D \w \W \s \S`, `(...)` groups,
  `|`, `^`, `$`, and `* + ?` with the lazy forms `*? +? ??`.
- Not supported: `{n,m}`, backreferences, lookaround.
- Patterns are limited to 256 bytes and 7 groups.
- A group that did not take part in the match is `nil` in the list.
- An invalid pattern makes every call return `nil`. Nothing raises, so a typo in a pattern shows as
  your "no data" state, not as `ERR:`.
- Any pattern finishes quickly, even one like `(a*)*b`, so it cannot freeze the display.

### MQTT calls

| Call | Does |
|---|---|
| `mqtt.subscribe(topic, cb)` | calls `cb(topic, payload)` for every message on `topic`. `+` matches one level, `#` the rest |
| `mqtt.publish(topic, payload)` | sends `payload` to `topic` |

- Payloads are strings in both directions. `mqtt.publish()` converts what you give it.
- Subscribing to the same topic again replaces the callback.
- There is no unsubscribe. Deleting or saving the script ends its subscriptions.
- **8 subscriptions per script.** A ninth is ignored without an error, and its callback never
  runs. Use one wildcard such as `sensor/+/temp` instead of many single topics, and tell them
  apart by the topic.

Every limit a script runs under is in [Limits](../../reference/limits.md#scripting).

## Related

- [Storage and settings](storage.md): keep the last answer across a reboot, and let the user enter
  an address or a token
- [Modbus in scripts](../modbus.md): energy meters and inverters
<!-- only tc002 -->
- [TCP in scripts](../tcp.md): services that send text line by line
- [Signing in from scripts](../oauth.md): services that need a sign-in
<!-- /only -->
- [MQTT](../mqtt.md): AWTRIX's own topics
- [Tutorial 3: Feed it real data](../../tutorials/real-data.md): a weather forecast, step by step
