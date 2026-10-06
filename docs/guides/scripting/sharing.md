# Sharing a script {#sharing-a-script}

Get a script ready for someone else: describe it, name what it needs, and send the file.

## What you get

<!-- panel -->
```berry
# @name    Weather
# @desc    A sun and the temperature
# @author  blueray
# @version 1.0
# @icons   sun

class Weather
  def draw()
    clear()
    icon("sun", 0, 0)
    text(10, 6, "21°C", 0xFFFFFF)
  end
end

return Weather()
```

A script ready to share: the header names it in the web UI and on the AWTRIX Hub, and `@icons`
lets the sun icon travel with it.

## How it behaves

- **A script is one file**, with no build step. **Export** on the **Scripts** tab saves it as an
  `.ax` file. The other person imports it there, or pastes the text into the editor, and saves.
- **The header is the comment block at the start of the file.** Its lines start with `# @` and
  tell the web UI and the AWTRIX Hub what the script is and what it needs. The script runs the same
  without them.
- **A few things live outside the file, and the header names them.** Icons come from the AWTRIX
  Hub, named in an [`@icons`](drawing.md#the-icons-your-script-needs) line. A
  [module](several-apps.md#sharing-code-between-scripts) the script imports, or a background script
  whose `shared` values it reads, is a file of its own, named in a
  [`@requires`](#scripts-your-script-needs) line.<!-- only esp32-s3 tc002 --> Sounds sit in the script's own folder on the
  clock. They come along from the AWTRIX Hub. With a file you send by hand, they are
  [uploaded separately](sound.md#sounds-for-your-script).<!-- /only -->
- **The install name is not `@name`.** The [install name](#install-name) is the name the script
  is saved under, and its ID in the rotation. `@name` is only the title shown.
- **The clock never refuses a script because of `@needs` or `@display`.** It installs and runs it,
  and warns when the clock lacks something the script asks for.

## Describe your script {#the-header}

The comment lines at the top of the file that start with `# @` are the **header**. They tell the
web UI and the AWTRIX Hub what the script is and what it needs:

```berry
# @name    Weather
# @desc    Current temperature via Open-Meteo
# @author  blueray
# @version 1.0
# @icons   sun, cloud
# @config  lat text "Latitude"  default="52.52"
# @config  lon text "Longitude" default="13.40"
```

Every line is optional. Only the comment block at the **start** of the file counts. It ends at the
first line of code, so an `# @name` inside a function is just a comment. Unknown keys are ignored.
[Every header key](#every-header-key) lists them all.

## Checklist before you share {#checklist-before-you-share}

- [ ] **Describe it.** `@name`, `@desc`, `@author` and `@version` show up in the web UI and on the
      Hub.
- [ ] **List the icons.** Every Hub icon you draw with `icon()` goes into an
      [`@icons`](drawing.md#the-icons-your-script-needs) line. They then arrive with the script.
      Nobody uploads them by hand.
- [ ] **Publish your own icons first.** An icon that is only on your clock does not travel with
      the script. [Publish it to the Hub](../icons.md#share-an-icon-in-the-hub), then list it in
      `@icons`.
- [ ] **Turn fixed values into settings.** A city, a color, a refresh interval: make each an
      [`@config`](storage.md#settings-the-user-can-change) line, so the next person changes it in
      the web UI instead of in your code.
<!-- only esp32-s3 tc002 -->
- [ ] **Put sounds in the script folder.** See [Sounds for your script](sound.md#sounds-for-your-script).
<!-- /only -->
- [ ] **Name the scripts it needs.** It imports a module, or reads what a background script
      publishes? Add a [`@requires`](#scripts-your-script-needs) line for each.
- [ ] **Say which clocks it runs on.** It needs <!-- only tc002 -->a gamepad, <!-- /only -->a sound feature or a bigger display? Add
      [`@needs` and `@display`](#what-your-script-asks-of-the-clock). A script that adapts with
      `width()` and `height()` leaves `@display` out.

## Scripts your script needs

A script that imports a module, or reads what a background script publishes through
[`shared`](several-apps.md#talking-to-other-apps), only works when that other file is installed
too. Name it in the header, one line each:

```berry
# @name     Battery
# @requires fmt
# @requires Weather AbC123xyz456
```

- The first value is the name the other file is installed under. For a module, that is the name it
  is imported as.
- The second value is optional: its ID on the [AWTRIX Hub](https://awtrix.de/scripts), the twelve
  characters at the end of its page address.
- Up to 8 lines. Anything after a `#` on a line is a comment.

When you install a script from its Hub page, or save it in the web UI, AWTRIX checks these lines.
If something is missing, it names it and asks:

- **Install all** downloads every missing file that has a Hub ID, plus the files those need and
  their icons. It installs each under the name its line gives, and then the script itself. Nothing
  is installed until all files have been downloaded and checked.
- **Only this script** leaves the rest to you.
- **Cancel** installs nothing.

A missing file without a Hub ID is named, and you install it by hand. Updating a script from the
Hub asks the same way when the new version needs something new. An installed script shows what it
still lacks above the editor, with an **Install** button for files that have a Hub ID.

Downloads from the Hub need you signed in on the Hub page, or your Hub connection key under
**System → AWTRIX Hub** in the web UI, as for icons.

## What your script asks of the clock

<!-- only esp32 esp32-s3 -->
Some scripts only work on some clocks: a night light needs a light sensor, a wide scene needs a
wider display. Say so in the header, and the web UI and the
[AWTRIX Hub](https://awtrix.de/scripts) warn people before they install it:

```berry
# @name    Skyline
# @needs   sensors.light
# @display 64x8
```
<!-- /only -->
<!-- only tc002 -->
Some scripts only work on some clocks: a game needs a gamepad, a full-screen scene needs a taller
display. Say so in the header, and the web UI and the
[AWTRIX Hub](https://awtrix.de/scripts) warn people before they install it:

```berry
# @name    Racer
# @needs   gamepad, audio.effect
# @display 52x16
```
<!-- /only -->

- `@needs` names what the clock must have, up to 8 names separated by commas or spaces. The
  [names](#names-for-needs) are listed under Details.
- `@display` names the smallest display the script is made for, as width x height. Leave it out
  when the script adapts with `width()` and `height()`. Then it runs on every display.
<!-- only esp32 esp32-s3 -->
  A single panel is `32x8`. Chained panels are wider, for example `64x8`.
<!-- /only -->
<!-- only tc002 -->
  Your clock's display is `52x16`.
<!-- /only -->

When you share a script in the Hub and its header has no `@needs` line, the Hub reads which of the
names your code uses and writes the line for you. Untick what your script can do without before
you save.

## Good to know

- **Only the comment block at the start of the file counts.** An `# @name` after the first line of
  code is a plain comment.
- **An icon that is only on your clock does not travel.**
  [Publish it to the Hub](../icons.md#share-an-icon-in-the-hub) first, then list it in `@icons`.
- **A `@requires` line without a Hub ID is not installed for anyone.** The other person sees its
  name and installs that file by hand.
- **Leave `@display` out when your script adapts** with `width()` and `height()`. Then every display
  takes it without a warning.
- **Other apps use the install name.** They read your `shared` values as `<install name>.<key>`,
  never under `@name`.

## Details

### Every header key

| Key | Add it when |
|---|---|
| `@name`, `@desc`, `@author`, `@version` | always: a title, one line of description, you, and a version number |
| [`@icons`](drawing.md#the-icons-your-script-needs) | the script draws icons from the AWTRIX Hub |
| [`@config`](storage.md#settings-the-user-can-change) | the user should change a value in the web UI |
| [`@requires`](#scripts-your-script-needs) | the script needs another script or a module |
| [`@needs`, `@display`](#what-your-script-asks-of-the-clock) | it needs certain hardware or a certain display size |
| [`@headless true`](several-apps.md#running-without-ever-being-shown) | it is a background script: it runs but never draws |
| [`@ondemand`](several-apps.md#start-from-the-device-menu) | it stays out of the rotation and is started from the device menu |
| [`@module`](several-apps.md#sharing-code-between-scripts) | the file is a module other scripts import |
<!-- only tc002 -->
| [`@oauth`](../oauth.md#name-the-service) | it signs in to a service with the user's account |
<!-- /only -->

### Install name

The **install name** is separate from `@name`. It is the name you saved the script under and the
app's ID in the rotation. It must be 1–32 characters of `A–Z`, `a–z`, `0–9`, `_` and `-`, and it
cannot be the name of a built-in app such as `Time`. `@name` is only for display.

### Names for `@needs`

| Name | The clock |
|---|---|
<!-- only tc002 -->
| `gamepad` | lets games read a Bluetooth gamepad or a phone used as the gamepad |
| `ble` | lets scripts use Bluetooth (`import ble`) |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `audio.mp3` | plays MP3 files |
<!-- /only -->
| `audio.rtttl` | plays melodies (`{'rtttl': ...}`) |
<!-- only esp32-s3 tc002 -->
| `audio.song` | plays [song text](../../reference/songs.md) with its synthesizer (`{'song': ...}`) |
<!-- /only -->
<!-- only tc002 -->
| `audio.speech` | reads text aloud (`{'speech': ...}`), when it has a voice |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `audio.track` | has a DFPlayer module (`{'track': ...}`) |
<!-- /only -->
<!-- only esp32-s3 tc002 -->
| `audio.radio` | plays internet radio |
<!-- /only -->
<!-- only tc002 -->
| `audio.effect` | plays effects and music over each other (`sound.effect()`, `'loop': true`) |
| `microphone` | hears the pitch of a note at its microphone (`music.pitch()`) |
<!-- /only -->
<!-- only esp32 esp32-s3 -->
| `sensors.light` | has a light sensor |
<!-- /only -->
<!-- only tc002 -->
| `oauth` | lets scripts [sign in to services](../oauth.md). An `@oauth` line adds it by itself. |
| `crypto` | lets scripts compute [checksums and signatures and mine](../hashing.md) (`import crypto`) |
| `layout` | lets scripts prepare text, icon and chart regions |
| `tcp` | lets scripts [talk TCP](../tcp.md) (`import tcp`) |
<!-- /only -->

Each name matches what `GET /api/v1/capabilities` reports for the clock, at the same path:
<!-- only esp32 esp32-s3 -->`audio.rtttl` is `"audio":{"rtttl":true}`, `sensors.light` is `"sensors":{"light":true}`.<!-- /only --><!-- only tc002 -->`audio.effect` is `"audio":{"effect":true}`, `gamepad` is `"gamepad":true`.<!-- /only -->

Anything after a `#` on a `@needs` or `@display` line is a comment:

<!-- only esp32 esp32-s3 -->
```berry
# @needs   sensors.light   # dims at night
# @display 64x8            # two panels side by side
```
<!-- /only -->
<!-- only tc002 -->
```berry
# @needs   gamepad   # for steering
# @display 52x16     # the whole display
```
<!-- /only -->

### When a clock lacks something

The clock installs and runs such a script anyway. On the **Apps** tab its row has the label
*does not fit*: tap it to read what is missing. The web UI warns before you save or import such a
script and asks whether to continue. It also names what is missing before it installs one from
the Hub as an update or a dependency. The AWTRIX Hub hides scripts that do not fit the clock you
chose, and asks before it sends one anyway.

### Backing up and restoring over the API

To back up your scripts, or install them from a computer, use the HTTP API. See
[Back up and restore scripts](../../reference/http.md#back-up-and-restore-scripts).

## Related

- [Drawing → The icons your script needs](drawing.md#the-icons-your-script-needs): how `@icons`
  brings icons along
- [Storage and settings](storage.md#settings-the-user-can-change): `@config` lines
- [Several apps together](several-apps.md): modules and background scripts
<!-- only esp32-s3 tc002 -->
- [Sound → Sounds for your script](sound.md#sounds-for-your-script): MP3s in the script folder
<!-- /only -->
- [Updating Hub scripts](../hub-script-updates.md): how installed scripts get new versions
- [The web UI → Scripts](../../getting-started/web-ui.md#scripts): import, export and the editor
