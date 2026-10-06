# Storage and settings {#storing-and-sharing-data}

Keep values across a reboot, and let the user change your script's settings in the web UI
without touching the code.

## What you get

```berry
class Shown
  var count, label

  def init()
    self.count = store.get("count", 0)    # 0 the first time
    self.label = str(self.count)
  end

  def on_show()
    self.count += 1
    self.label = str(self.count)
    store.set("count", self.count)
  end

  def draw()
    clear()
    text(1, 6, self.label, 0xFFFFFF)
  end
end

return Shown()
```

The app counts how often it was shown. After a reboot it goes on from the last count, not from 0.

## How it behaves

- **Each script has its own store**, named values that survive a reboot. Other apps cannot read
  it.
- **The stored values are back before `init()` runs**, so your app can show the last value from
  its first frame.
- **`store.set()` takes effect at once.** AWTRIX writes the store to flash within five seconds, so
  a `store.set()` every second is fine. A power cut can lose the last five seconds of writes.
- **A setting is a stored value too.** A `# @config` line in the header puts it in the web UI,
  and `store.get()` reads it.
- **A value that does not fit is not saved, and you get no error.** The app keeps running with
  the value in memory and looks correct, until the next reboot brings back the last value that
  fitted. Store the finished value, not the whole answer from a web service.

## Remember a value {#storage}

`store.set(key, value)` keeps a value, and `store.get(key, default)` reads it back:

```berry
store.set("count", 0)
var count = store.get("count", 0)   # 0 if never written
var maybe = store.get("count")      # nil if never written
```

A value may be a whole number, a real, a string, `true` or `false`, a list or a map: anything
that fits in JSON.

Editing and saving a script keeps its store, and so does renaming it on the **Scripts** tab.

To see, change or reset what an app stored, open it on the **Scripts** tab and switch to **Data**.
The values are shown as JSON. Saving them restarts the app. Settings are not listed there: change
them on the **Apps** tab.

To hand a value to another app, use [`shared`](several-apps.md#talking-to-other-apps).

## Settings the user can change

When you give a script to someone else, they want their own city or color in it, without editing
code. A **setting** puts that value in the web UI.

A setting is one line in the header, next to `@name`:

<!-- panel -->
```berry
# @name    Greeter
# @config  who   text   "Your name"   default="World"
# @config  tint  color  "Color"       default=#FF8800

class Greeter
  var who, tint

  def init()
    self.who = store.get("who")
    self.tint = store.get("tint")
  end

  def draw()
    clear()
    text(1, 6, self.who, self.tint)
  end
end

return Greeter()
```

The app shows the defaults from the header, `World` in orange, until the user changes them:

1. Save the script.
2. On the **Apps** tab, press **⚙** on the app's row. The settings open under the row: a text box
   for the name and a color picker for the color.
3. Change the values and press **Save**. The app restarts with the new values.

**A setting is a stored value.** Read it with `store.get("who")`. Write the default only in the
header: `store.get` returns it from the first frame. It also works the other way: after
`store.set("who", "Ada")`, the settings show *Ada*.

The line format is **`# @config <key> <type> "<label>" <options…>`**. Only the key and the type are
required. Without a label, the key is shown.

| Type | Shows as | Extras |
|---|---|---|
| `bool` | a switch | |
| `text` | a text box | `maxlen=` (256 if you leave it out) |
| `number` | a number box | `min=` `max=` `step=` `unit=` |
| `slider` | a slider | `min=` `max=` `step=` `unit=` (0–100 if you leave them out) |
| `select` | a dropdown | `options=a,b,c`, required |
| `color` | a color picker | |

Every type also takes `default=`, `help=` (a line of explanation under the label) and `group=`.
Put a value with a space in quotes: `default="New York"`, `help="Where you live"`,
`group="Weather data"`.

```berry
# @config  city   text   "City"       default="Berlin" help="Where the weather comes from"
# @config  metric bool   "Celsius"    default=true
# @config  every  number "Refresh"    default=15 min=1 max=60 unit=min
# @config  mode   select "Show"       default=now options=now,today,week
# @config  bright slider "Brightness" default=80 min=0 max=100 unit=%
# @config  tint   color  "Color"      default=#FF8800
```

**A color is a number.** Write the default like an HTML color, `default=#FF8800`, and
`store.get("tint")` returns `0xFF8800`, ready to draw with.

### Put settings in groups

Use the same `group=` on several settings to put them in a category the user can fold:

```berry
# @config  city   text   "City"    default="Berlin" group="Weather data"
# @config  metric bool   "Celsius" default=true     group="Weather data"
# @config  tint   color  "Color"   default=#FF8800  group="Display"
# @config  every  number "Refresh" default=15 min=1 max=60 unit=min
```

Categories start folded and open by clicking their heading. Settings without a group stay outside
the categories.

## Good to know

- **Removing a `@config` line deletes the user's value for it** on the next save. Leave the line
  in while you test, instead of commenting it out.
- **Saving settings restarts the app.** `init()` and `setup()` run again with the new values. An
  animation starts over, and stored values stay.
- **`store.get(key)` without a default answers `nil`** for a key that was never written. Give a
  default, or check for `nil` before you draw with it.
- **A store that is too large is not saved.** The log says `store not saved` with the size. Keep
  finished values, not whole answers from a web service.

## Details

### Store calls

| Call | Does |
|---|---|
| `store.set(key, value)` | keeps `value` under `key` |
| `store.get(key, default?)` | the value under `key`, or `default`, or `nil` when there is neither |

- Values may be integers, reals, strings, booleans, lists and maps, anything that fits in JSON.
- Each app has its own store. Apps cannot read each other's.
- Values are saved to flash within five seconds. A power cut can lose the last five seconds of
  writes.
- Editing and saving a script keeps its store, and so does renaming it on the **Scripts** tab.
  `init()` and `setup()` see the stored values at once.
- A value that does not fit is not saved, and you get no error. The app keeps running with the
  value in memory. After the next reboot, the last value that fitted comes back. The log says
  `store not saved` with the size.
<!-- only tc002 -->
  The size limit is in [Limits](../../reference/limits.md#storage).
<!-- /only -->
<!-- only esp32 esp32-s3 -->
  A store larger than the free memory is not saved.
<!-- /only -->

### The `@config` line

- **A typo does not break the app.** A `@config` line AWTRIX cannot read is skipped, and a message
  at the top of the settings names it.
- **Removing a setting removes its value.** Values your code wrote with `store.set()` are never
  touched.
- The group name is shown exactly as written, up to 48 bytes, without translation.
- Categories appear in the order they first occur. Settings within each category keep their
  declaration order.
- Settings without a group, or with `group=""`, stay outside the categories.
- Saving, discarding changes and using defaults work on folded settings too. Saving keeps the
  categories' open or folded state.
- Changing a group name does not change the stored values or the keys used with `store.get()`.
- Key names and the length of a text value have limits, listed in
  [Limits](../../reference/limits.md#scripting).
- **[Modules](several-apps.md#sharing-code-between-scripts) can have settings too.** That is how
  several apps share one value. See
  [settings several apps share](several-apps.md#settings-several-apps-share).

## Related

- [Network](network.md): the weather example keeps its last reading in the store
- [Several apps together](several-apps.md#talking-to-other-apps): hand values to other apps
- [Sharing a script](sharing.md#the-header): the other lines of the header
- [Tutorial 2: Give it a memory](../../tutorials/state-and-time.md): a store and a setting, step by step
