# Controlling the device {#controlling-the-device}

Show a notification, move the rotation, switch the display off, and read or change the device
settings from your script.

## What you get

```berry
class Doorbell
  def on_button(btn)
    if btn == "select"
      notify({"text": "Doorbell", "sound": {"rtttl": "d:d=4,o=5,b=120:c,e,g"}})
    end
  end

  def draw()
    clear()
    text(1, 6, "Ring", 0xFFFFFF)
  end
end

return Doorbell()
```

Press select while the app is shown, and a notification says Doorbell with a short melody.

## How it behaves

- **`notify()` shows a notification like one sent to the API.** It interrupts the rotation, and it
  can wake a switched-off display. Its sound is an alert and plays over your script's own sounds.
- **`rotation` moves the apps along now.** `rotation.pause()` keeps the app shown until your
  script resumes the rotation or the user moves it.
- **`display.power()` switches the display**, a moment later. AWTRIX and its scripts keep running
  while it is off.
- **`settings` reads and changes the device settings**, with the keys and checks of the API. A
  change is saved and stays after your app is gone, like a change in the web UI.
- **The uppercase setting never changes what a script draws.** Read the settings when your app
  should match the built-in apps.

## Show a notification {#notifications}

`notify()` takes a map in the [notification payload format](../../reference/payload.md), the same
object `POST /api/v1/notifications` takes. So everything a notification can do works here: `hold`,
`stack`, `wakeup`, a `sound`, an `effect`, an `overlay`, colors, charts. Colors may be numbers:
`rgb()`, `hsv()` and `0xRRGGBB` all work.

```berry
def on_button(btn)
  if btn == "select"
    notify({"text": "ARMED", "textColor": rgb(255, 0, 0), "hold": true,
            "sound": {"rtttl": "s:d=8,o=5,b=200:c,g,c,g", "loop": true}})
  end
end
```

`sound` takes the same sound as [`sound.play()`](sound.md#what-to-play)<!-- only esp32-s3 -->, except `station`<!-- /only --><!-- only tc002 -->, except `station` and
`nextBar`<!-- /only -->.
<!-- only esp32-s3 tc002 -->
A plain name finds your script's [own sounds](sound.md#sounds-for-your-script) first.
<!-- /only -->
The sound is an alert: it plays at the **Alerts** volume, over your script's own sounds. With
`"loop": true` it repeats while the notification is shown. For a sound without a notification,
use [`sound`](sound.md#sound).

`notify()` returns `true` when AWTRIX accepted the notification, and `false` for a malformed
payload or a full queue.

## Move the rotation {#driving-the-rotation}

`rotation` lets a script move the app rotation:

```berry
rotation.next()      # advance to the next app now
rotation.previous()  # step back to the previous app
rotation.show()      # bring the rotation to THIS app now
rotation.pause()     # freeze the automatic switching
rotation.resume()    # let it run again
rotation.close()     # end THIS app if it was started from the menu
```

**`rotation.show()`** shows your app now, when you have something to say. It takes no argument:
a script can only show itself. A pause stays in place, so `show()` then `pause()` shows your app
and keeps it there.

**`rotation.pause()`** stops the automatic switching, so the display stays where it is: during an
animation, or while you wait for something. `rotation.next()` and `rotation.previous()` still
work while paused, so your app can step through a sequence itself. **Any move by the user, with a
button, the web UI or the API, ends the pause.** Call `rotation.resume()` when you no longer need
it.

**`rotation.close()`** ends an [`@ondemand`](several-apps.md#start-from-the-device-menu) script that
was started, as holding select does.

## Switch the display off {#display-power}

`display` switches the display on and off. AWTRIX and its scripts keep running.

```berry
display.power(false)  # switch the display off, a moment later
display.power(true)   # switch it on again
display.is_on()       # is it on right now?
```

The change happens a moment later, so `display.is_on()` right after the call may still show the
old state. Nothing is saved: after a reboot the normal display state applies.

## Match the device's look {#device-settings}

Your script draws everything itself. Text you draw without a color follows the device's text
color, but a color you pass stays as it is, and the uppercase setting never changes your text.
White text next to a built-in app tinted amber looks wrong. `settings` reads the device settings
so you can match them:

<!-- panel -->
```berry
class Train
  var label, color

  def on_show()
    self.label = settings.apply_case("Zug 12")   # read each time the app appears
    self.color = settings.get("textColor")
  end

  def draw()
    clear()
    text(1, 6, self.label, self.color)
  end
end

return Train()
```

With the factory settings, the text is white and in capitals, like the text of a pushed app.
`settings.apply_case()` applies the device's uppercase setting to your text, exactly as for the
app next to yours.

`settings.get(key)` takes the keys [`PATCH /api/v1/settings`](../../reference/settings.md) takes:
`brightness`, `textColor`, `appDurationMs`, `useCelsius`, `time24h`, `volume` and all others,
spelled the same as in the REST API and MQTT. Values come in the same form as in the API. Numbers
are numbers, switches are `true` or `false`, colors are `0xRRGGBB` numbers (like `rgb()` and
`hsv()` return), and settings the API names by word (`timeSeparatorMode`, `dateOrder`,
`transitionEffect`, …) are strings:

```berry
settings.get("brightness")          # 120
settings.get("autoBrightness")      # false
settings.get("timeSeparatorMode")   # "pulse"
settings.get("gamma")               # 1.9
settings.get("textColor")           # 16777215
```

You read what the user **configured**: `settings.get("brightness")` is the setting, not the
brightness auto-brightness uses right now.

The five accent colors return `nil` when the user never picked one. `nil` means "use
`textColor`", as the built-in apps do:

```berry
var col = settings.get("temperatureColor")
if col == nil col = settings.get("textColor") end
```

### Change a setting {#writing}

`settings.set()` checks values like the REST API does. It returns `false`, and changes nothing,
for an unknown key, a value of the wrong type, a number out of range or an unknown word:

```berry
settings.set("brightness", 40)              # true
settings.set("brightness", 999)             # false, 0-255
settings.set("timeSeparatorMode", "blink")  # true
settings.set("timeSeparatorMode", "wobble") # false
settings.set("textColor", "#FF8800")        # true
settings.set("timeColor", nil)              # true, clears the accent color
```

`true` means accepted: the change takes effect on the next frame and is saved, like a `PATCH`
from the network.

Change as little as you can. The device belongs to its owner, and a script that quietly changes
brightness or turns off sound is hard to track down from the web UI. Change only what your app
needs, change it back when done, and prefer reading over writing.

## Write to the log {#logging}

<a id="logging-and-version"></a>

```berry
log("fetched " + str(n) + " rows")
```

`log()` writes to the AWTRIX log, marked `[script:<name>]`, and shows up in the web UI console. It
accepts any value, not only strings.

## Find out which firmware runs {#which-firmware-is-running}

```berry
log("running on " + version())        # for example "1.0.14"
```

`version()` returns the firmware version as a string, the same one the web UI shows. Log it, so a
problem report says which firmware your script ran on.

To find out whether the clock can do something, ask for the feature itself:
[`sound.can()`](sound.md#different-clocks) for sound, `width()` and `height()` for the display, or
an `import` inside [`try`](index.md#just-enough-berry) for a module.

## Good to know

- **Call `notify()` once per event.** Each call adds a notification to the queue, so a call in
  `draw()` or on every `loop()` fills it.
- **A setting you change stays changed**, also after your app is gone. Change it back when your
  app is done.
- **Setting keys are spelled as in the API:** `textColor`, not `textcolor`. An unknown key answers
  `nil` and raises no error.
- **`display.is_on()` right after `display.power()` may still show the old state.** The change
  happens a moment later.
- **Do not compare versions with `<` or `>`.** Berry compares text character by character, so
  `"1.0.9" > "1.0.14"` is `true`.

## Details

| Call | Does | Returns |
|---|---|---|
| `notify(payload)` | shows a notification, in the [payload format](../../reference/payload.md) | `true` when accepted, `false` for a malformed payload or a full queue |
| `rotation.next()` | shows the next app now | |
| `rotation.previous()` | shows the previous app now | |
| `rotation.show()` | brings the rotation to this app now | `false` if your app is not in the rotation |
| `rotation.pause()` | stops the automatic switching | |
| `rotation.resume()` | lets it run again | |
| `rotation.close()` | ends this app if it was started from the menu | `false`, doing nothing, in any other app |
| `display.power(on)` | switches the display on or off, a moment later | `true` when the request was accepted |
| `display.is_on()` | whether the display is on right now | `true` or `false` |
| `settings.get(key)` | the configured value | the value, or `nil` |
| `settings.set(key, value)` | changes a setting and saves it | `true` when the change was accepted |
| `settings.apply_case(str)` | your text with the device's uppercase setting applied | the text |
| `log(value)` | writes to the AWTRIX log, marked `[script:<name>]` | |
| `version()` | the firmware version, the same one the web UI shows | a string |

**Notifications:** the queue limit and waking from off apply as for a notification from the API.

**Rotation:**

- `rotation.show()` takes no argument. A pause stays in place.
- `rotation.next()` and `rotation.previous()` still work while paused.
- Any move by the user, with a button, the web UI or the API, ends the pause, and the rotation runs
  normally again. If you forget `rotation.resume()`, the next user action ends the pause anyway.

**Display power:**

- `display.power()` takes only `true` or `false`.
- Nothing is saved: after a reboot the normal display state applies.
- A notification with `wakeup: true` can show while the display is off. `display.is_on()` still
  returns `false` during that wake-up.

**Settings:**

- Case matters: `textColor`, not `textcolor`. An unknown key returns `nil`, so a typo reads as
  "not set" instead of raising an error.
- The nested `scroll` and `weekdayBar` groups have no flat key: `get` returns `nil` and `set`
  returns `false`. Use the REST API for those.
- `settings.set()` returns `false`, and changes nothing, for an unknown key, a value of the wrong
  type, a number out of range or an unknown word.
- Setting a value that is already in place returns `true` and does nothing.
- The uppercase setting affects pushed apps, never what a script draws.

## Related

- [Sound<!-- only esp32-s3 tc002 --> and music<!-- /only -->](sound.md): a sound without a notification
- [Your first notification](../notifications.md): what a notification can show
- [Settings](../../reference/settings.md): every key `settings.get()` and `settings.set()` take
- [Several apps together](several-apps.md): skip a turn, stay longer, start from the menu
