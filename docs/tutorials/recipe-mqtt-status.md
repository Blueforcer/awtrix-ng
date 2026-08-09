# Recipe: MQTT status

One MQTT topic on the panel, coloured green when it says what you expect and red with a
blinking marker when it does not. It goes dim when the value gets old, so a broker that
stopped publishing looks different from a door that is genuinely closed.

You need a broker configured on the device. Without one this app installs and runs
happily but never shows anything, which is exactly what it should do.

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Doorstate`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**, the `⋯`
menu on that app's row, then **Settings**. New to all this?
[Tutorial 1](first-draw.md) takes it slowly.

```berry
# @name    Doorstate
# @desc    An MQTT topic, coloured by its value
# @author  awtrix-ng
# @version 1.0
# @config  topic text   "Topic"        default="home/frontdoor/state"
# @config  alert text   "Alert value"  default="open"
# @config  ok    color  "Normal"       default=#00CC44
# @config  bad   color  "Alert"        default=#FF3000
# @config  stale number "Dim after"    default=10 min=0 max=240 unit=min help="0 never dims"

class Doorstate
  var value        # the last payload, nil until one arrives
  var alerting     # does it match the alert value
  var seen_at      # now_ms() when it arrived
  var word         # the alert value, read once
  var ok, bad      # colours
  var stale_ms     # 0 means never go stale

  def init()
    self.value = nil
    self.alerting = false
    self.seen_at = 0
    self.word = store.get("alert")
    self.ok = store.get("ok")
    self.bad = store.get("bad")
    self.stale_ms = store.get("stale") * 60000
  end

  def setup()
    mqtt.subscribe(store.get("topic"), / t, p -> self.on_msg(p))
  end

  def on_msg(payload)
    self.value = payload
    self.alerting = payload == self.word
    self.seen_at = now_ms()
  end

  def should_show()
    return self.value != nil
  end

  def draw()
    clear()
    if self.value == nil
      return
    end
    var fresh = self.stale_ms == 0 || now_ms() - self.seen_at < self.stale_ms
    var c = 0x444444
    if fresh
      c = self.alerting ? self.bad : self.ok
    end

    var bar = c
    if fresh && self.alerting && (now_ms() % 1200) < 400
      bar = 0x000000
    end
    rect_fill(0, 0, 3, height(), bar)

    scroll_text(5, 6, width() - 5, self.value, c)
  end
end

return Doorstate()
```

Point it at a topic you already publish to, set the value that should count as an
alert, and it works. Nothing in it is specific to doors.

---

## How it works

**Subscribe in `setup()`, never in `draw()`.** `setup()` runs once, just after the app
loads. Subscribing in `draw()` would register a new callback forty times a second.
Re-subscribing to a topic you already hold replaces the callback rather than adding a
second one, so it would not leak, but it is still forty pointless calls per second.

There is no unsubscribe. Deleting the script, or saving it again, drops its
subscriptions.

**Both MQTT calls are silent no-ops without a broker.** `mqtt.subscribe()` and
`mqtt.publish()` simply do nothing when the device has none configured. That is why an
app with an MQTT branch still installs and runs on every device, and why this one leans
on `should_show()` to stay out of the rotation until a message actually arrives.

**`should_show()` is not a guarantee, so `draw()` checks too.** The rotation honours a
`false`, but an app can still be brought to the panel directly, by
`PUT /api/v1/apps/active` from a script or a home automation, or by its own
`rotation.show()`. The `if self.value == nil` at the top of `draw()` costs one
comparison and means that path can never paint from a member that is not there yet.
Treat `should_show()` as the polite answer and the guard as the correct one.

**Wildcards work.** `+` matches one topic level and `#` matches the rest, so
`sensor/+/temp` is a valid subscription. The first argument your callback receives is
the **concrete** topic the broker delivered on, which is how you tell the branches apart.
This app ignores it, hence the `t` that goes unused in the closure.

**Payloads are strings in both directions.** A value you only display can stay a string,
which is what happens here. The moment you want to compare, calculate or store it, parse
it with `num(payload)`, which returns an `int` or a `real`, or `nil` if the payload was
not a number. It copes with both `876.6` and `"876.6"`, quotes included, which saves an
argument with whatever is publishing.

**Freshness is a real feature, not a nicety.** `now_ms() - self.seen_at` is how long ago
the last message landed. A broker that stops publishing otherwise leaves the panel
showing a confident green `closed` forever. Dimming to grey says "this is the last thing
I heard, and it was a while ago", which is the truth.

**The blink comes from the clock, not a counter.** `(now_ms() % 1200) < 400` is on for
400 ms out of every 1200. Frames are not evenly spaced, so a counter you increment in
`draw()` would drift and stutter. `now_ms()` does not.

!!! tip "MQTT is the cheapest data source on the device"
    A payload arrives small and already parsed out of whatever produced it. No 8 KB
    response body, no TLS handshake, no polling interval to tune. If the user has a
    broker, prefer it over HTTP for anything it can supply.

---

## Making it yours

**Show a number instead of a word.** Parse in `on_msg()` with `num(payload)` and keep the
number, then build the display string there too. Never build it in `draw()`.

**Watch several topics.** Call `mqtt.subscribe()` once per topic in `setup()`, up to
eight per app, and use the `topic` argument to decide which member to update. Past three
or four, consider separate apps instead, because a 32 by 8 panel says one thing well and
four things badly.

**Publish as well as listen.** `mqtt.publish("home/panel/status", "up")` in `setup()`
announces the panel to your automation. Publishing from `on_button()` turns the device
into a control surface, which is one of the nicer things it can do.

**Raise an alert rather than waiting for a turn.** If the change matters enough to
interrupt whatever is on screen, `notify()` is the call, and an app that only listens
does not need to draw at all. That is the [headless doorbell](recipe-doorbell.md).

---

## Related

- [MQTT automation](../guides/mqtt.md) for the topics AWTRIX publishes on its own
- [App scripting](../guides/scripting.md) for the full `mqtt` reference
- [Limits](../reference/limits.md#scripting) for subscription and queue limits
