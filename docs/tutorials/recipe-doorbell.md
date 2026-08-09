# Recipe: Headless doorbell

An app with no `draw()`. It never takes a turn in the rotation and never occupies the
panel. It sits there listening, and when its topic fires it interrupts whatever is on
screen with a message and a sound.

This is the operating mode people forget exists. An app does not have to be a picture.
It can be a rule.

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Doorbell`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**, the `⋯`
menu on that app's row, then **Settings**. New to all this?
[Tutorial 1](first-draw.md) takes it slowly.

```berry
# @name     Doorbell
# @desc     Rings the panel when an MQTT topic fires
# @author   awtrix-ng
# @version  1.0
# @headless true
# @config   topic text   "Topic"     default="home/doorbell/ring"
# @config   msg   text   "Message"   default="Doorbell" maxlen=32
# @config   icon  text   "Icon ID"   default="" help="Leave empty to show no icon"
# @config   tune  text   "Melody"    default="bell:d=4,o=5,b=120:c6,e6,g6"
# @config   quiet number "Ignore for" default=10 min=0 max=300 unit=s help="Repeats within this are dropped"

class Doorbell
  var spec       # the notification, built once
  var gap_ms     # how long to stay quiet after ringing
  var last       # now_ms() of the last ring, nil until the first

  def init()
    self.gap_ms = store.get("quiet") * 1000
    self.last = nil

    self.spec = {"text": store.get("msg"), "wakeup": true}
    var tune = store.get("tune")
    if tune != ""
      self.spec["soundRtttl"] = tune
    end
    var ic = store.get("icon")
    if ic != ""
      self.spec["icon"] = ic
    end
  end

  def setup()
    mqtt.subscribe(store.get("topic"), / t, p -> self.ring())
  end

  def ring()
    var t = now_ms()
    if self.last != nil && self.gap_ms > 0 && t - self.last < self.gap_ms
      return
    end
    self.last = t
    notify(self.spec)
  end
end

return Doorbell()
```

Publish anything at all to the topic and the panel wakes up, shows the message and plays
the melody.

---

## How it works

**`# @headless true` is the whole trick.** An app with that line is never given a turn on
the panel, so `draw()`, `should_show()` and `duration()` are never called. Leave them out
entirely. Everything else still runs: `init()`, `setup()`, `loop()`, MQTT callbacks and
HTTP callbacks all behave exactly as they do in any other app.

The file still ends with `return Doorbell()`. A headless app is an app.

Do not put the flag on an app that draws something. A headless app is never drawn,
whatever its `draw()` contains, and the result is a script that looks correct and does
nothing.

**`notify()` reaches past your own app.** It interrupts the rotation, it can play a
sound, and with `wakeup: true` it renders even while the display is powered off. That
makes it the right call for an event and the wrong call for your regular frame. It
returns `true` when the device accepted the notification and `false` for a malformed
payload or a full queue.

**The spec map is built once, in `init()`.** It never changes, so rebuilding it on every
ring would allocate for no reason. This app rings rarely enough that it would not matter,
and the habit is worth having anyway: a map literal written inside a method is a fresh
allocation every time that method runs.

The optional keys are added only when the user filled them in. An empty `icon` string is
not the same as no icon, and passing one gives you a missing-icon box on the panel.

**`self.last` starts as `nil`, not `0`.** This is a small trap worth naming. `now_ms()`
counts from boot, so it is a small number shortly after startup. With `self.last = 0` and
a ten second quiet period, a doorbell pressed in the first ten seconds after a reboot
would be silently swallowed. Starting at `nil` and checking for it makes the first ring
always work.

**The debounce is doing real work.** MQTT retained messages, a bouncing switch and a
double press all produce two events where a human made one. Ten seconds of quiet after a
ring costs nothing and prevents the panel shouting twice.

!!! note "Deactivating an app is different from headless"
    A headless app runs and never draws. An app the user **deactivates** in the web UI
    stops completely: no `loop()`, no HTTP callbacks, no MQTT messages. It stays
    installed and keeps its stored values, and nothing runs until it is switched back on.

---

## Sound, and where it comes from

`soundRtttl` in the notification plays an inline melody on the buzzer. It is the most
portable option, because every panel has one.

If you want more than a buzzer, the `sound` module picks the output for you:

```berry
    if sound.sinks()['mp3']
      sound.mp3("doorbell")
    else
      sound.rtttl("bell:d=4,o=5,b=120:c6,e6,g6")
    end
```

`sound.sinks()` reports which outputs this panel actually has, as a map with `buzzer`,
`track`, `mp3` and `radio` keys. The explicit calls never fall back, which is the point:
`sound.mp3()` on a device with no MP3 support does nothing rather than substituting a
beep you did not ask for.

All of it is gated on the device's global sound setting. A user who muted their panel
stays muted, and that is not a bug to work around.

---

## Making it yours

**Ring on a schedule instead of a topic.** Drop the subscription and give the app a
`loop()` that watches the clock. The trick is to fire on the *change*, not on the
condition, or you get one notification per second for a whole minute:

```berry
class Doorbell
  var spec, gap_ms, last
  var slot                      # the minute we last acted on

  def init()
    self.slot = -1
    # the rest of init() exactly as above
  end

  def loop()
    if hour() < 0
      return
    end
    var minute_of_day = hour() * 60 + minute()
    if minute_of_day != self.slot
      self.slot = minute_of_day
      if hour() == 7 && minute() == 0
        notify(self.spec)
      end
    end
  end
end
```

Both new lines are load-bearing. `var slot` declares the member, and `self.slot = -1`
gives it a value before anything reads it. Berry lets you *assign* to a member that was
never declared, but *reading* one raises `attribute_error: the 'Doorbell' object has no
attribute 'slot'`, and this loop reads before it writes. That is the kind of mistake
that installs cleanly and shows `ERR:` a second later.

**Ring on something from the network.** A `loop()` with a timer and an `http.get()`, the
shape from [tutorial 3](real-data.md), plus a comparison against the last value. A
headless app that fetches and only speaks up when something changed is one of the most
useful things you can put on a panel.

**Fetch for other apps instead of notifying.** Publish with `shared.set()` and let
several drawing apps read one fetch. One HTTP buffer and one parse on the device instead
of three is a real saving, and [Going easy on memory](going-easy-on-memory.md) explains
why it matters more than it sounds.

**Bring the rotation to an app instead of interrupting.** `rotation.show()` summons the
calling app to the panel immediately. It does nothing in a headless app, which is never
in the rotation, so this is the one thing here that needs a `draw()` after all.

---

## Related

- [Your first notification](../guides/notifications.md) for the full notification payload
- [Sound](../guides/sounds.md) for melodies, MP3 files and RTTTL syntax
- [MQTT automation](../guides/mqtt.md) for the broker side
