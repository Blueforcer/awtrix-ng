# Recipe: Background doorbell

This app is a *background script*: it has no `draw()` and never takes a turn in the
rotation. It listens to an MQTT topic and speaks up only when a message arrives.

An app does not have to show a picture. It can also work like a rule in the background.

---

## What you get

When a message arrives on the topic, the display wakes up, shows your message as a
notification and plays a sound.

---

## The script

In the web UI, open the **Scripts** tab, create a script called `Doorbell`, paste this in and
save. The settings declared at the top of the file then appear under **Apps**: press **⚙**
on that app's row. New to all this?
[Tutorial 1: Draw something](first-draw.md) takes it slowly.

```berry
# @name     Doorbell
# @desc     Rings when an MQTT topic fires
# @author   awtrix-ng
# @version  1.0
# @headless true
# @config   topic text   "Topic"     default="home/doorbell/ring"
# @config   msg   text   "Message"   default="Doorbell" maxlen=32
# @config   icon  text   "Icon"      default="" help="Name of an installed icon, or empty"
# @config   tune  text   "Melody"    default="bell:d=4,o=5,b=120:c6,e6,g6"
# @config   quiet number "Ignore for" default=10 min=0 max=300 unit=s help="Repeats within this are dropped"

class Doorbell
  var spec       # the notification, built once
  var gap_ms     # how long to stay quiet after ringing
  var last       # now_ms() of the last ring, nil until the first

  def init()
    self.gap_ms = store.get("quiet") * 1000
    self.last = nil

    self.spec = {
      "text": store.get("msg"),
      "icon": store.get("icon"),
      "wakeup": true
    }
    var tune = store.get("tune")
    if tune != "" self.spec["sound"] = ["doorbell", {"rtttl": tune}] end
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

Publish anything to the topic. The display wakes up, shows the message and plays the melody.

---

## How it works

**`# @headless true` makes it a background script.** A background script never gets a
turn on the display, so `draw()`, `should_show()` and `duration()` are never called. Leave
them out. Everything else runs as in any other app: `init()`, `setup()`, `loop()`, MQTT
callbacks and HTTP callbacks.

The file still ends with `return Doorbell()`.

Do not use the line on an app that draws something. A background script is never drawn,
so the script would look correct and show nothing.

**`notify()` works outside your own app.** It interrupts the rotation, it can play a
sound, and with `wakeup: true` it is shown even while the display is off. Use it for
events, not for regular frames. It returns `true` when the device accepted the
notification, and `false` for a wrong payload or a full queue.

**The notification map is built once, in `init()`.** It never changes, so there is no need
to build it again on every ring. A map written inside a method is created again every time
the method runs.

An empty setting means "none". With the **Icon** field empty, the notification shows no
icon. With the **Melody** field empty, it plays no sound.

The icon must already be on the device: install it from the
[AWTRIX Hub](../guides/icons.md#install-from-the-awtrix-hub) first and enter its name. If
the icon is missing, the notification shows without it. If your doorbell always uses the
same icon, you can instead write its name into the script and list it in a `# @icons`
line, so it arrives together with the script. See
[The icons your script needs](../guides/scripting/drawing.md#the-icons-your-script-needs).

**`self.last` starts as `nil`, not `0`.** `now_ms()` counts from boot, so it is a small
number shortly after startup. With `self.last = 0` and a ten second quiet time, a ring in
the first ten seconds after a reboot would be ignored. Starting with `nil` makes the first
ring always work.

**The quiet time is needed.** Retained MQTT messages, a bouncing switch and a double press
all send two messages for one ring. Ten seconds of quiet after a ring stops the display from
ringing twice.

!!! note "Deactivating an app is different from a background script"
    A background script runs and never draws. An app the user **deactivates** in the web UI
    stops completely: no `loop()`, no HTTP callbacks, no MQTT messages. It stays
    installed and keeps its stored values until it is switched back on.

---

## Sound, and where it comes from

The notification's `sound` is a list. The clock plays the first entry it can play:

<!-- only esp32 -->
1. `"doorbell"`: a melody saved under the name `doorbell`.
2. `{"rtttl": tune}`: the melody from the **Melody** field. It plays on the buzzer.

So the script plays your saved doorbell melody when there is one, and the melody from the field
otherwise.
<!-- /only -->
<!-- only esp32-s3 -->
1. `"doorbell"`: a stored sound called `doorbell`. That is an MP3 in the script's own folder, an
   MP3 uploaded on the **Audio** tab, or a melody saved under that name.
2. `{"rtttl": tune}`: the melody from the **Melody** field. It plays on the buzzer.

So the same script plays your doorbell MP3 where there is one, and the melody everywhere else.
<!-- /only -->
<!-- only tc002 -->
1. `"doorbell"`: a stored sound called `doorbell`. That is an MP3 in the script's own folder, an
   MP3 uploaded on the **Audio** tab, or a melody saved under that name.
2. `{"rtttl": tune}`: the melody from the **Melody** field. It plays on the speaker.

So the script plays your doorbell MP3 when there is one, and the melody otherwise.
<!-- /only -->

The sound plays at the alert volume of the device. If the user turned the master volume or the
alert volume down to 0, it stays silent. See [Volume](../guides/sounds.md#volume).

---

## Making it yours

**Ring on a schedule instead of a topic.** Remove the subscription and give the app a
`loop()` that watches the clock. React to the *change* of the minute, not to the minute
itself. Otherwise you get one notification per second for a whole minute:

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

Both new lines are needed. `var slot` declares the member, and `self.slot = -1` gives it a
value before it is read. Berry lets you *assign* to a member that was never declared, but
*reading* one raises `attribute_error: the 'Doorbell' object has no attribute 'slot'`. This
loop reads before it writes, so without these lines the app installs and then shows `ERR:`.

**Ring on something from the network.** Use a `loop()` with a timer and an `http.get()`,
as in [tutorial 3](real-data.md), and compare with the last value. A background script
that fetches data and only speaks up when something changes is very useful.

**Fetch for other apps instead of notifying.** Publish with `shared.set()` and let several
drawing apps read the result of one fetch. That uses much less memory than three apps that
each fetch the same data. See [Keeping scripts small](going-easy-on-memory.md).

**Bring an app to the display instead of interrupting.** `rotation.show()` brings the
calling app to the display at once. It does nothing in a background script, because a
background script is never in the rotation. For this you need an app with a `draw()`.

---

## Related

- [Your first notification](../guides/notifications.md) – all notification keys
- [Sound](../guides/sounds.md) – melodies, MP3 files and RTTTL syntax
- [MQTT](../guides/mqtt.md) – the broker side
