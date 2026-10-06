# The web UI

AWTRIX has its own web UI. Open it in a browser to see the display live, change every
setting, manage apps, scripts, icons, <!-- only esp32 -->melodies<!-- /only --><!-- only esp32-s3 tc002 -->sounds<!-- /only --> and palettes, and update the firmware. It needs no app
and no cloud account.

## Open the web UI

1. Make sure your phone or computer is on the same network as AWTRIX.
2. Open the AWTRIX hostname in a browser, for example:

    ```
    http://awtrixng-a1b2c3.local/
    ```

3. If the `.local` name does not open, use the IP address instead. See
   [Find your clock](discovery.md).

The page is stored on AWTRIX itself, so it works without an internet connection. Only a few extras
load from the internet: the icon editor, the AWTRIX Hub links and the firmware update check.

## The header

The buttons at the top right:

| Button | What it does |
|---|---|
| **Ko-fi cup** | Opens the project's Ko-fi page |
| **Shop** | Opens the [AWTRIX Hub](../guides/hub.md) |
| **Book** | Opens this documentation |
| **Sun / moon** | Switches between light and dark theme |

## The tabs

On a phone the tabs sit in a bar at the bottom, on a computer in a row at the top. Each tab has its
own address, so you can bookmark it:

| Tab | Address | What you do there |
|---|---|---|
| **Dashboard** | `#/` | Watch the display live, switch it on and off, set the brightness |
| **Apps** | `#/apps` | Choose which apps show, in which order |
| **Scripts** | `#/scripts` | Write, install and update Berry apps |
| **Icons** | `#/icons` | Add, show, edit and delete icons |
| **Icon Editor** | `#/editor` | Draw icons and animations |
| **Audio** | `#/audio` | The volume mixer<!-- only esp32-s3 tc002 -->, MP3 files, internet radio<!-- /only --> and melodies |
| **Palettes** | `#/palettes` | Make color ramps for effects, text and charts |
| **Display** | `#/display` | Everything about what the display shows |
| **System** | `#/system` | Wi-Fi, MQTT, time, hardware, updates, backup |
| **Log** | `#/log` | Live device log |

The web UI shows only what your device can do.<!-- only esp32 esp32-s3 --> For example, the **Audio** tab is missing on a
device with neither a buzzer nor a speaker.<!-- /only -->

## Dashboard

### Live preview

The large picture at the top shows every LED of your display, updated four times a second. The
same picture on its own is at `http://<awtrix-ip>/fullscreen`. You can put it in a Home Assistant
dashboard or any other web page.

### Controls under the preview

| Control | What it does |
|---|---|
| **Power** | Switches the display on and off |
| **Brightness** | Sets the brightness, 0–255 |
<!-- only esp32 esp32-s3 -->
| **Auto brightness** | Lets the light sensor set the brightness. The manual slider is locked while it is on. Only on a device with a light sensor |
<!-- /only -->
| **◀ / ▶** | Previous / next app |
| **⏸ / ▶** | Stops the automatic app change, or starts it again. The same switch as **Auto rotation** under [Display](#display) |
| **Bell** | Closes the notification that is shown now |
| **↓** | Saves the current picture as a PNG |
| **●** | Records an animated GIF. Press again to stop and save |
| **1:1** | Saves PNG and GIF at one pixel per LED, for sharing in the AWTRIX Hub |

### Save a screenshot or GIF

1. Press **↓** for a still picture, or **●** to start a recording. The button turns red while it
   records.
2. Press **●** again to stop. A recording also stops by itself after 240 frames or 10 seconds.
3. The file lands in your browser's download folder, named `awtrix-<timestamp>`.

Without **1:1**, each LED is drawn as a large block with a thin dark gap, so the picture looks like
the real display. <!-- only esp32 esp32-s3 -->A 32×8 display gives a 3200×800 PNG and a 640×160 GIF. Larger displays are
enlarged less.<!-- /only --><!-- only tc002 -->The 52×16 display gives a 5200×1600 PNG and a GIF of up to 1040×320.<!-- /only --> The GIF plays back at the speed it was recorded. Every PNG and GIF is small enough to
use as a cover or gallery picture in the [AWTRIX Hub](https://awtrix.de).

While it records, the display may run a few frames per second slower.

### Status tiles

Tiles show how the device is doing. A tile appears only when the device has that sensor.

| Tile | Shows | Color |
|---|---|---|
| **Battery** | Charge in %, voltage below | green from 40 %, amber from 20 %, red below |
| **Wi-Fi** | Signal in dBm, and in words: *excellent / good / fair / weak* | green from −65 dBm, amber from −75 dBm, red below |
<!-- only esp32 esp32-s3 -->
| **Light level** | Ambient light, raw sensor value below | always blue |
| **Temperature** | Temperature | always blue |
| **Humidity** | Humidity | always blue |
<!-- /only -->
| **FPS** | Frames per second, out of 42 | green from 40, amber from 32, red below |

Below the tiles: firmware version (with a note when an update is available), hostname, IP address,
MAC address, uptime, free memory, <!-- only esp32-s3 -->PSRAM (`none` on a board without it), <!-- /only -->and the app shown now.

What each value means: [Device state](../reference/device.md#always-present-fields).

## Apps

The Apps tab decides what AWTRIX shows and in which order. Every change is saved at once. The
message that confirms it has an **Undo** button for a few seconds.

Each group shows its number of apps next to its title. Every group except **On the display** starts
folded away. Tap its title to open it.

### The groups

| Group | What is in it |
|---|---|
| **On the display** | The apps shown one after another, in this order |
| **In the device menu** | [Scripts started from the menu](../guides/scripting/several-apps.md#start-from-the-device-menu), such as games. They never join the rotation |
| **In the background** | [Scripts that run but never show](../guides/scripting/several-apps.md#running-without-ever-being-shown), for example one that only sends notifications |
| **Switched off** | Apps that do not run |
| **Shared settings** | [Settings several scripts use together](../guides/scripting/several-apps.md#settings-several-apps-share) |

A group without apps is not shown, except **On the display**.

### Change the order

Drag a row by its **⠿** grip. This works with a mouse and with a finger. **Move up** and **Move
down** in the row's **⋯** menu do the same one step at a time.

### What a row can do

| Control | What it does |
|---|---|
| **Show** | Shows this app on the display right away |
| **▶ Start** | Starts a script from the device menu. Hold the middle button on the clock to end it |
| **⚙** | Opens the app's or module's settings under the row. Press it again to close them |
| Switch | Switches the app off or on. An app switched on again goes back to the end of the display, or to the background |

The **⋯** menu holds the rest:

| Menu entry | What it does |
|---|---|
| **Move up** / **Move down** | Moves the app one place |
| **Duplicate** | Shows the app twice per round |
| **Edit** | Scripts only: opens the code in the [Scripts](#scripts) tab |
| **Install icons** | Scripts that name icons: installs them from the AWTRIX Hub |
| **Delete** | Pushed apps and apps marked *no data*: removes them. Press twice to confirm |

Built-in apps cannot be deleted, only switched off. Scripts are deleted in the [Scripts](#scripts)
tab.

Under the name, a row says what kind of app it is: *Built-in*, *Script* or *Pushed* (over HTTP or
MQTT). Labels next to the name point out something to know. Tap one for a short explanation:

| Label | Meaning |
|---|---|
| *error* | The script stopped with an error. Tap it to read the message |
| *does not fit* | The script needs hardware this device does not have |
| *no data* | The app is on and keeps its place, but nothing has sent it data yet |
| *skipped* | The script skipped this round. It stays in the rotation |
| *running* | A script from the device menu runs on the clock right now |

If scripts are switched off under **System → Scripting**, a banner says so at the top.

The same order can be set over the API: [HTTP API → Apps](../reference/http.md#apps).

## Scripts

An editor for Berry apps. On the left, a list of what is installed: **Scripts** first, then
**Modules**. On the right, the code with color highlighting.

### Write and install a script

1. Press **+** next to **Scripts** to start from a template. (**+** next to **Modules** starts a
   [module](../guides/scripting/several-apps.md#sharing-code-between-scripts).)
2. Type a name in the name field and write your code.
3. Press **Save** or **Ctrl-S**. This installs the script and starts it.
4. Press **Show on the clock** to switch the display to your script.

If the script has an error, the line is marked in the editor. The bar under the editor shows the
cursor position and how much of the allowed size you have used.

**Code** and **Data** above the editor switch between the script and the values it saved, as JSON.
Saving changed values restarts the script. See [Storage](../guides/scripting/storage.md).

### Buttons and keys

| Button / key | What it does |
|---|---|
| **+** | New script (or module) from a template |
| **Import** | Loads a `.ax` file from your computer into the editor |
| **Export** | Saves the editor content as a `.ax` file |
| **Show on the clock** | Shows the script you are editing on the display |
| **Save** / **Ctrl-S** | Installs the script under the name in the name field |
| **Ctrl-.** | Lists the functions you can use |
| **Ctrl-/** | Comments a line in or out |

Unsaved work stays in the editor when you visit another tab. If you open another file, the editor
asks first.

**Rename a script:** change the name and save. AWTRIX keeps the new name and removes the old
one.<!-- only esp32-s3 tc002 --> The script's sounds move along.<!-- /only -->

### Scripts that need more than your device has

A script can say what it needs, for example a gamepad or a certain display size (see
[`@needs` and `@display`](../guides/scripting/sharing.md#what-your-script-asks-of-the-clock)). If your device
does not have it, saving or importing tells you what is missing, for example *Not for this clock:
needs a gamepad.* You can choose **Save anyway**, **Import anyway** or
**Cancel**.

<!-- only esp32-s3 tc002 -->
### Sounds of a script

On a <!-- only esp32-s3 -->clock that plays MP3s<!-- /only --><!-- only tc002 -->clock<!-- /only -->, the editor bar has a note button. It shows how many sounds the script
has and opens them in the [Audio](#mp3s) tab, where you add, play and delete them. Modules have no
sounds of their own.

**Delete a script** with the bin next to it. Press twice to confirm. A script with sounds asks
instead: **Delete with sounds**, **Keep sounds** or **Cancel**. Kept sounds stay in the Audio tab,
marked *script removed*, until you delete them there. A script installed again under the same name uses
them.

<!-- /only -->
### Scripts from the AWTRIX Hub

- **Check for updates** checks every script you installed from the Hub and marks those with a newer
  version. See [Hub script updates](../guides/hub-script-updates.md).
- **Scripts for this device** opens the Hub with the scripts this AWTRIX can run.

### Shared values

The **Shared** list below the files shows the values scripts publish to each other with
[`shared.set()`](../guides/scripting/several-apps.md#talking-to-other-apps). Tap **Shared** to open it.
Each row shows `owner.key` and the value. A value written more than five minutes ago turns amber.
Point at a row to see how long ago it was written. The list updates every three seconds, is
read-only, and is empty again after a reboot.

How to write scripts: [Scripting guide](../guides/scripting/index.md).

## Icons

The header shows how much storage is used. The bar turns red above 90 %. The tab has two parts:

| Part | What it holds |
|---|---|
| **On the clock** | Every icon on the device, with the count. **Search installed icons** filters them |
| **Add** | Upload an icon, find one in the AWTRIX Hub, or draw one |

### Add an icon

- **Add icon:** drop `.gif`, `.png`, `.jpg` or `.jpeg` files on the upload area, or click it to
  choose. Each file gets its own progress line.
- **Icons for this device:** opens the [AWTRIX Hub](https://awtrix.de/icons) with icons that fit
  your display. Pick one and press **Send to AWTRIX**. Your browser copies the file to the clock
  over your local network.
- **Create an icon:** opens the [Icon Editor](#icon-editor).

PNG and JPG files are turned into GIF while they upload. That looks sharper on the display and
uses less space. So `smiley.png` becomes `smiley.gif` and replaces an older `smiley.jpg`. Animated
GIFs stay animated. Keep icons within your display's width and height. 8×8 is the usual size for a
still icon. A GIF as wide as the display can fill the background behind the text. See
[Payload → Icon](../reference/payload.md#icon).

### Use and manage an icon

**Show on the clock** shows the icon on the display for three seconds. The **⋯** menu on a tile:

| Menu entry | What it does |
|---|---|
| **Edit** | Opens the icon in the Icon Editor |
| **Publish to Hub** | Shares the icon in the AWTRIX Hub. For a Hub icon you changed: **Share as a new icon** |
| **View on Hub** | Hub icons only: opens its Hub page |
| **Rename** | Gives the icon a new name. Apps and scripts that use the old name need the new one |
| **Download icon** | Saves the file to your computer |
| **Delete** | Deletes the icon. Press twice to confirm |

Icons from the Hub carry a **Hub** badge. One you changed says *Changed on the clock*.

Publishing needs a Hub connection key. Create one in
[your Hub account](https://awtrix.de/account/settings) and save it under
**System → AWTRIX Hub**. The key stays in this browser. It is not stored on the clock.

More: [Icons](../guides/icons.md).

## Icon Editor

A pixel editor to draw icons and animations and save them straight to AWTRIX.

1. Open the **Icon Editor** tab, or choose **Edit** on an icon in the [Icons](#icons) tab.
2. Choose a size: 8×8, 32×8, <!-- only tc002 -->16×16, <!-- /only -->the whole display, or your own size up to the
   display size.
3. Draw, give the icon a name and save. It appears in the Icons tab.

The **Live** switch shows your drawing on the real display while you work.

The editor loads from the internet. Without internet the tab says the editor could not load. The
rest of the web UI keeps working.

More: [Icon editor](../guides/icon-editor.md).

## Audio

<!-- only esp32 -->
Everything that makes sound: the **Mixer** and the **Melodies**.

In the list, **▶** plays a melody on AWTRIX and turns into **■** while it plays. **■** stops it.
The bin deletes the melody. Press twice to confirm.
<!-- /only -->
<!-- only esp32-s3 -->
Everything that makes sound: the **Mixer**, then **MP3s**, **Radio** and **Melodies**. You only
see the sections your board can use. MP3s and radio need PSRAM and an I²S amplifier. Melodies
need a buzzer.
<!-- /only -->
<!-- only tc002 -->
Everything that makes sound: the **Mixer**, then **MP3s**, **Radio** and **Melodies**.
<!-- /only -->
<!-- only esp32-s3 tc002 -->

<!-- only esp32-s3 -->While the clock plays MP3s or radio, a<!-- /only --><!-- only tc002 -->A<!-- /only --> bar at the top shows what is playing now: the sound's name, or the
station and song title. **■** stops it.

In all three lists, **▶** plays the entry on AWTRIX and turns into **■** while it plays. **■** on
an MP3 or a melody stops that sound, and the radio keeps playing. **■** on a station stops the
radio, and other sounds keep playing. The bin deletes the entry. Press twice to confirm.
<!-- /only -->

### Mixer

How loud the clock plays. Each slider goes from 0 to 100 and applies at once:

| Slider | Setting | Volume of |
|---|---|---|
| **Master** | `volume` | the whole clock. The other <!-- only esp32 -->two<!-- /only --><!-- only esp32-s3 tc002 -->three<!-- /only --> are shares of it |
<!-- only esp32-s3 -->
| **Radio** | `radioVolume` | internet radio. Only on a board that plays radio |
<!-- /only -->
<!-- only tc002 -->
| **Radio** | `radioVolume` | internet radio |
<!-- /only -->
| **Apps** | `appVolume` | everything a script plays |
| **Alerts** | `alertVolume` | notification sounds and the sounds you play from outside |

With **Master** at 50 and **Alerts** at 60, a notification plays at 30.<!-- only tc002 --> The knob moves
**Master** too.<!-- /only -->

More: [Volume](../guides/sounds.md#volume).

<!-- only esp32-s3 tc002 -->
### MP3s

Your own MP3 files, for notifications and scripts.<!-- only esp32-s3 --> Only on a board that plays MP3s.<!-- /only -->

1. Drag MP3 files onto **⬆ Upload**, or click it to choose.
2. Use the file name without `.mp3` elsewhere: `ding.mp3` plays with `"sound":"ding"` in a
   notification.

File names may only use letters, digits, `_` and `-`. A file called `My Song (2024).mp3` is
refused. Rename it first. An MP3 cannot have the name of a melody either. The line below the
button shows how many sounds you have, their size and the free storage. **Search sounds** filters
the list.

| Button | What it does |
|---|---|
| **▶** | Plays the MP3 **on AWTRIX** |
| **🎧** | Plays it **in your browser** |
| **Bin** | Deletes it |

An MP3 pauses internet radio. The radio comes back by itself afterwards.

**Sounds of scripts.** Below your own files, every script with
[its own sounds](../guides/scripting/sound.md#sounds-for-your-script) has a closed group, for example
`AWTRIX GP · 21 · 1.4 MB` (name, number of sounds, size). Open it to play, delete or upload sounds
for that script. Before deleting, it says whose sound it is: *Sure? Used by AWTRIX GP*.

To open the tab with one script's group open, use `#/audio/<script>`, for example
`http://<awtrix-ip>/#/audio/awtrix-gp`.

More: [Sound](../guides/sounds.md).

### Radio

Your internet radio stations.<!-- only esp32-s3 --> Only on a board that can play a stream.<!-- /only -->

1. Press **+ Add station**.
2. Enter a name and the stream URL. **▶** in the form tries the URL before you keep it.
3. Press **Save** or `Enter`. **Cancel** or `Esc` closes the form without a change.

The URL must start with `http://` or `https://`, and each name can be used only once. Otherwise a
message appears under the fields and **Save** stays off. In the list, point at a row to see the full
URL. The pencil edits a station.

More: [Internet radio](../guides/radio.md).

<!-- /only -->
### Melodies

Short ringtones in RTTTL format, for notifications and scripts.<!-- only esp32 esp32-s3 --> Only on a board with a buzzer.<!-- /only --> Each
row shows the number of notes and the length, for example `10 notes · 2.3 s`. A melody with an
error is still listed, marked **Invalid melody**, so you can fix it.

| Button | What it does |
|---|---|
| **▶** | Plays it **on AWTRIX** |
| **🎧** | Plays it **in your browser** |
| **Pencil** | Opens it for editing |
| **Bin** | Deletes it |

**Add a melody:**

1. Press **+ New melody**.
2. **Name:** 1–24 characters of `A-Z`, `a-z`, `0-9`, `_`, `-`. This is the name you use in
   `"sound":"<name>"`.<!-- only esp32-s3 tc002 --> It cannot be the name of an MP3.<!-- /only -->
3. **RTTTL:** only the part after the name, for example `d=4,o=5,b=100:e,c`.
4. Press **Save** or `Enter`.

The melody is checked while you type. If it has an error, the line below names it, for example
`'h' is not a note`. **🎧** and **▶** in the form play what is in the fields now, so you can listen
before you save. 🎧 plays in your browser, so you hear it even with the clock's volume at 0.

If you paste a complete melody such as `jackpot:d=8,o=5,b=120:16c,16e,16g,c6` into the RTTTL field,
the name moves to the name field by itself. To rename a melody, change the name and save.

More: [Sound](../guides/sounds.md) · [HTTP API → Audio](../reference/http.md#audio).

## Palettes

A list of every palette AWTRIX knows, each shown as the color ramp it paints, and an editor next
to it. You can make your own palettes and change or restore the eight built-in ones.

How to use it: [Palette editor](../guides/palette-editor.md) · built-in palettes:
[Visuals → Palettes](../reference/visuals.md#palettes).

## Display

Settings for what the display shows, in five sections: **Brightness**, **Color**,
**App rotation**, **Text** and **Weather overlay**. The section list stays at the top while you
scroll. Clock, date and sensor settings are under **Apps → ⚙** on the app's row.

### Save your changes

1. Change as many settings as you like. The bar at the bottom counts your unsaved changes.
2. Press **Save** to send only what you changed, or **Discard** to undo.

If AWTRIX refuses a value, nothing from that save is applied, and a message names the setting.

### Black and white in color fields

For the clock, date, temperature, humidity and battery colors, open **Apps → ⚙**. Switch
**Global text color** on to inherit the text color, or off to choose a color of the app's own,
including black.

Under **Display → Color**, **white** means *off* for **Color correction** and **Color tint**.

Every setting with its range and default:

- [Settings → Brightness](../reference/settings.md#brightness)
- [Settings → App rotation](../reference/settings.md#app-rotation)
- [Settings → Clock app](../reference/settings.md#clock-app), [Clock text](../reference/settings.md#clock-text), [Date text](../reference/settings.md#date-text), [Weekday bar](../reference/settings.md#weekday-bar)
- [Settings → Global text](../reference/settings.md#global-text)
- [Settings → Sensor apps](../reference/settings.md#sensor-apps)
- [Settings → Color](../reference/settings.md#panel): saturation, gamma, correction, tint
- [Visuals → Weather overlays](../reference/visuals.md#weather-overlays) · [Transitions](../reference/visuals.md#transitions)

## System

<!-- only esp32 esp32-s3 -->
Device settings in these sections: **Wi-Fi**, **Web server**, **MQTT**, **Time**, **Panel**,
**Brightness & sensors**, **GPIO**, **Buttons**, **Audio**, **Scripting**, **Mirroring**, **Misc**, then
**AWTRIX Hub**, **Maintenance** and **Backup & restore**. Sections your board cannot use are hidden.
<!-- /only -->
<!-- only tc002 -->
Device settings in these sections: **Wi-Fi**, **Web server**, **MQTT**, **Time**, **Panel**,
**Brightness & sensors**, **Buttons**, **Audio**, **Scripting**, **Mirroring**, **Misc**, then
**AWTRIX Hub**, **Gamepad**, **Home Assistant Voice**, **iPhone**, **Maintenance** and
**Backup & restore**.
<!-- /only -->

All fields are in [System configuration](../reference/system.md):
[Wi-Fi](../reference/system.md#wi-fi),
[MQTT and Home Assistant](../reference/system.md#mqtt-and-home-assistant),
[Time](../reference/system.md#time),
[Identity, web server and authentication](../reference/system.md#identity-web-server-and-authentication),
<!-- only esp32 esp32-s3 -->
[Sensor calibration](../reference/system.md#sensor-calibration),
[Auto-brightness](../reference/system.md#auto-brightness),
<!-- /only -->
[Buttons](../reference/system.md#buttons),
<!-- only esp32 esp32-s3 -->
[Sound hardware](../reference/system.md#sound-hardware),
<!-- /only -->
[Settings → Sound](../reference/settings.md#sound),
[Miscellaneous](../reference/system.md#miscellaneous).

Save works as on the Display tab. When a change needs a restart, an amber banner says *Saved.
Reboot to apply.* Its **Reboot now** button restarts the clock. Sound settings apply at once.

### Wi-Fi {#wifi}

The **Scan** button next to the network name searches for networks and lists them by signal
strength. 🔒 marks networks with a password.

### Passwords

Password fields always look empty, with *(unchanged)* in them. Leave a field empty to keep the
saved password. Type a new one to change it.

### MQTT

A badge at the top shows the connection: whether the broker answered, the address it connected to,
or why it failed (for example *Wrong login* or *Host not found*).

### Time

**Use my zone** takes the time zone your browser is set to.

### Panel

<!-- only tc002 -->
The display is fixed at 52×16, so this section only shows the size.

<!-- /only -->
<!-- only esp32 esp32-s3 -->
Describe your LED panels:

- how wide one panel is and how many panels the cable runs through,
- which corner the first LED is in,
- whether the LEDs run along rows or columns,
- whether every second row runs back the other way (**Serpentine**).

The **Display size** line adds it up while you type, for example `32 × 8 = 256 LEDs`. It turns red
if the total width is outside 32–128. Such a value is not saved.

**Mirror** flips the picture left to right. **Rotate 180°** turns it upside down and swaps the
left and right buttons. Use it when the display hangs upside down.

Everything except the total width takes effect right away, so you can flip **Serpentine** and watch
the display.

The built-in apps are drawn for 32 pixels width. On a wider display they are centered.

All fields: [Panel and orientation](../reference/system.md#panel-and-orientation).
<!-- /only -->

### Brightness & sensors

<!-- only esp32 esp32-s3 -->
Minimum and maximum brightness, light sensor factor and gamma, **LDR on GND** and brightness
smoothing, shown only on a board with a light sensor. Below them: battery divider, low-battery
threshold, and temperature and humidity offset and decimals.

### GPIO

Which pin does what: **LED data**, three buttons, battery, light sensor, buzzer, I²C<!-- only esp32 --> and
DFPlayer.<!-- /only --><!-- only esp32-s3 -->, DFPlayer and the three I²S pins for an audio amplifier.<!-- /only --> `-1` means "not connected". **LED data** is a list of
the pins that can drive LEDs.

<!-- only esp32 -->
**Preset wiring** fills in the wiring of a **Ulanzi TC001** or an **AWTRIX 2 mainboard**. It does
not save. Check the fields, then press **Save**.
<!-- /only -->
Wrong or conflicting pins are refused when you save. GPIO changes apply after a reboot.

More: [GPIO & boards](../reference/gpio.md) ·
<!-- only esp32 -->
[Board presets](../reference/gpio.md#board-presets) ·
<!-- /only -->
[Validation rules](../reference/gpio.md#validation-rules) ·
[Recovery from a bad map](../reference/gpio.md#recovery-from-a-bad-map).
<!-- /only -->
<!-- only tc002 -->
The clock has no light sensor, so this section holds only the low-battery threshold.
<!-- /only -->

### Audio

The sound hardware. The volumes are on the [Audio tab](#mixer).

<!-- only esp32 esp32-s3 -->
- **DFPlayer** adds a DFPlayer module on the DFPlayer pins. It shows once both DFPlayer pins are
  set under [GPIO](#gpio).
<!-- /only -->
<!-- only tc002 -->
- **Boot sound** turns the sound at power-on on or off.
- **Music source** picks what music visualizers and `music.pitch()` react to: what the speaker
  plays, the microphone, or automatic.
<!-- /only -->

### AWTRIX Hub

Paste your Hub connection key here to download icons and script updates from the
[AWTRIX Hub](https://awtrix.de) and to share icons. **Open Hub account** takes you to the page where
you copy the key. The key is saved only in this browser. In another browser, enter it again.
**Remove key** deletes it.

<!-- only tc002 -->
### Gamepad

Shows the two gamepads, whether they are connected, and their player numbers.
**Pair** adds a gamepad. Each **Forget** removes only its own gamepad. The player number goes by
first input and can differ from the row number. While a phone is a gamepad, a **Phone** row shows
which player it plays. See [Gamepad](../guides/gamepad.md).

### Home Assistant Voice

Enter the Home Assistant address, an access token and the Assist pipeline, then press
**Save and connect**. The **Voice** switch turns it on and off at once. A badge shows the connection
or, in plain words, why it fails. See [Home Assistant Voice](../guides/voice.md).

### iPhone

**iPhone link** switches the connection on and off. **Connection** shows the iPhone
and whether it is connected. **Forget iPhone** removes the remembered iPhone. The apps, music and
covers are chosen in the [AWTRIX NG app](../guides/app.md). See [iPhone](../guides/iphone.md).

<!-- /only -->
### Advanced

This section appears only when the firmware has a setting the page has no field for. You can change
the raw value there.

### Maintenance

| Action | What happens |
|---|---|
| **Firmware update** | **Check for updates** asks GitHub for the newest release. If there is one, **Download & install** loads and installs it. The document icon opens the release notes |
<!-- only esp32 esp32-s3 -->
| **Upload firmware (.bin)** | Installs a firmware file from your computer, with a progress bar. AWTRIX restarts with the new firmware |
<!-- /only -->
<!-- only tc002 -->
| **Upload firmware (.awup)** | Installs an `.awup` update package from your computer, with a progress bar. AWTRIX restarts with the new firmware. See [Updating firmware](../guides/updating.md) |
<!-- /only -->
| **Reboot** | Restarts AWTRIX. Press twice to confirm |
| **Reset settings** | Sets all [settings](../reference/settings.md) back to their defaults: the Display tab, volumes, which apps rotate and the rest. Wi-Fi, MQTT, hardware settings and files stay. Press twice to confirm |
| **Factory reset** | Erases **everything**: Wi-Fi, files, settings. Type the hostname, then press **Erase everything** |

!!! danger "A factory reset cannot be undone"
    It erases the Wi-Fi login, every uploaded file and all settings. AWTRIX then starts in setup
    mode again.

More: [Updating firmware](../guides/updating.md) ·
[Persistence and resets](../reference/system.md#persistence-and-resets).

### Backup and restore

**Create a backup:**

<!-- only esp32 -->
1. Tick what to include: Wi-Fi, Settings, Icons, Melodies, Palettes, Scripts, App order.
   Melodies are only offered where the board has a buzzer.
<!-- /only -->
<!-- only esp32-s3 -->
1. Tick what to include: Wi-Fi, Settings, Icons, Melodies, Palettes, MP3s, Scripts, App order.
   Melodies and MP3s are only offered where the board can play them. Scripts include their sounds.
<!-- /only -->
<!-- only tc002 -->
1. Tick what to include: Wi-Fi, Settings, Icons, Melodies, Palettes, MP3s, Scripts, App order.
   Scripts include their sounds.
<!-- /only -->
2. Press **Download backup**. Your browser saves a `.zip` file.

The file is built in your browser. Nothing is stored on AWTRIX or sent anywhere else.

!!! warning "Keep the backup safe"
    With **Wi-Fi** or **Settings** ticked, the file contains your Wi-Fi, MQTT and web login
    passwords in plain text.

**Restore a backup:** press **Choose backup…** and pick the `.zip`. Anything that could not be
restored is reported as a warning. If the backup changed Wi-Fi, system settings, settings or the
app order, you are offered a reboot.

## Log

The device log, updated every second while the tab is open.

- **Auto-scroll** follows new lines. Scroll up and it pauses. Scroll back to the bottom and it
  continues.
- **Copy** puts the whole log on your clipboard, for example for a bug report.
- **Clear** empties the list in your browser only.

For more detail in the log, switch on **System → Misc → Debug mode**.

## Setup mode

A new AWTRIX, or one after a factory reset, opens its own Wi-Fi network, the setup hotspot.
Connect to it and the setup page opens by itself.

The web UI then shows only the **System** tab, with a blue banner: *Setup mode: connect
the clock to your Wi-Fi below.* You get:

1. the clock's MAC address, then your Wi-Fi name and password, plus an optional clock name,
2. **Reboot**: AWTRIX restarts and joins your network,
3. **Restore**: load a backup, Wi-Fi included, after a reset.

The other tabs come back once AWTRIX is on your network. If you set a web login, it is also needed
on the setup hotspot. See [Authentication](../reference/http.md#authentication).

If the Wi-Fi sign-in window on your phone cannot upload the backup file, open the setup page
address it shows in a normal browser. Logs, scripts, file lists and other settings are unavailable
on the setup hotspot. See the complete
[setup-mode policy](../reference/errors.md#provisioning-lockdown-403).

## When it goes wrong

- **A message pops up at the bottom.** It is AWTRIX's own error message and names the field that
  caused it. See [Errors](../reference/errors.md).
- **"Clock not reachable".** The page cannot reach AWTRIX. Check that it is on and on the same
  network, then reload.
- **A button says "Sure?"** Delete, reset and reboot buttons need a second press within three
  seconds. If you wait, the button goes back to normal and nothing happens.
- **The Icon Editor does not load.** It needs internet access.

## Related

- [HTTP API](../reference/http.md) - every route, with curl
- [MQTT topics](../reference/mqtt.md) - the same commands over a broker
- [App & notification payload](../reference/payload.md) - what you can put on the display
