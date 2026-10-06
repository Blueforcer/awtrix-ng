# Recipe: MQTT status

This app shows the value of one MQTT topic, colored by what it says.

You need an MQTT broker set up on the device. Without one, the app installs and runs, but
never appears on the display.

---

## What you get

A colored bar on the left and the topic's value next to it: green while the value is normal,
red with a blinking bar when it matches your alert value, and gray once no new value has come
for a while, ten minutes by default.

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Doorstate`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**: press **⚙**
on that app's row. New to all this?
[Tutorial 1: Draw something](first-draw.md) takes it slowly.

```berry
# @name    Doorstate
# @desc    An MQTT topic, colored by its value
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
  var ok, bad      # colors
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

Set a topic you already publish to and the value that counts as an alert. The app works
for any topic, not only doors.

---

## How it works

**Subscribe in `setup()`, never in `draw()`.** `setup()` runs once, just after the app
starts. Subscribing to a topic you already have replaces the callback, so a subscription in
`draw()` would not add up, but it would make forty useless calls a second.

There is no unsubscribe. Deleting the script, or saving it again, removes its
subscriptions.

**Without a broker, MQTT calls do nothing.** `mqtt.subscribe()` and `mqtt.publish()` do
nothing when no broker is set up. So an app with MQTT still installs and runs on every
device. This app uses `should_show()` to stay out of the rotation until a message arrives.

**`draw()` checks too.** The rotation skips an app whose `should_show()` returns `false`.
But the app can still be brought to the display directly: by `PUT /api/v1/apps/active` from a
script or a home automation, or by its own `rotation.show()`. The `if self.value == nil` at
the top of `draw()` makes sure the app never paints a value that is not there yet.

**Wildcards work.** `+` matches one topic level and `#` matches the rest, so
`sensor/+/temp` is a valid subscription. The first argument of your callback is the
**actual** topic the message came on, so you can tell the topics apart. This app does not
need it, so the `t` in the callback is unused.

**Payloads are always strings.** A value you only show can stay a string, as here. To
compare, calculate or store it as a number, use `num(payload)`. It returns an `int` or a
`real`, or `nil` if the payload is not a number. It accepts both `876.6` and `"876.6"`,
quotes included.

**The app shows when a value is old.** `now_ms() - self.seen_at` is the time since the last
message. Without this check, a broker that stops publishing would leave a green `closed`
on the display forever. Gray means "this is the last value, and it is old".

**The blink uses the clock, not a counter.** `(now_ms() % 1200) < 400` is on for 400 ms of
every 1200 ms. Frames are not evenly spaced, so a counter in `draw()` would blink
unevenly. `now_ms()` does not.

!!! tip "MQTT is the cheapest data source"
    An MQTT message is small and contains only the value. There is no large response, no
    HTTPS connection and no polling interval. If the user has a broker, prefer it over
    HTTP.

---

## Making it yours

**Show a number instead of a word.** Convert the payload with `num(payload)` in `on_msg()`
and keep the number. Build the display text there too, never in `draw()`.

**Watch several topics.** Call `mqtt.subscribe()` once per topic in `setup()`, up to eight
per app. Use the `topic` argument to decide which member to update. For more than three or
four topics, use separate apps: a <!-- only esp32 esp32-s3 -->32 × 8<!-- /only --><!-- only tc002 -->52 × 16<!-- /only --> display shows one thing well, not four.

**Publish as well.** `mqtt.publish("home/panel/status", "up")` in `setup()` tells your home
automation that the display is running. Publish from `on_button()` to use the device buttons
as switches.

**Interrupt the display instead of waiting for a turn.** If a change is important enough,
use `notify()`. An app that only listens does not need to draw at all. That is the
[background doorbell](recipe-doorbell.md).

---

## Related

- [MQTT](../guides/mqtt.md) – the topics AWTRIX publishes by itself
- [Scripting guide](../guides/scripting/index.md) – the full `mqtt` reference
- [Limits](../reference/limits.md#scripting) – subscription and queue limits
