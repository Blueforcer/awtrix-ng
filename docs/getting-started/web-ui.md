# Web UI tour

AWTRIX serves its own control panel. Point a browser at it and you get a live picture of the
matrix, every setting it has, an editor for melodies, and a file manager for icons
and palettes - no app, no cloud, no internet connection.

Open it at the AWTRIX hostname:

```
http://awtrixng-a1b2c3.local/
```

…or at its IP address if `.local` names do not resolve on your network - see
[Finding AWTRIX](discovery.md). The page is a single HTML file stored on AWTRIX itself, so
it works fully offline - including in provisioning mode, before it has ever seen your Wi-Fi.

## The tabs

The navigation bar carries ten tabs (a bottom bar on phones, a top row on desktop). Each is a URL
hash, so you can bookmark or link any of them directly:

| Tab | URL | What it is for |
|---|---|---|
| **Dashboard** | `#/` | Live matrix preview, power/brightness, vitals |
| **Apps** | `#/apps` | Reorder, switch off and delete apps |
| **Scripts** | `#/scripts` | Write, install and debug Berry apps |
| **Icons** | `#/icons` | Icon files, storage, LaMetric downloader |
| **Icon Editor** | `#/editor` | Draw 8×8/32×8 icons in an embedded editor and save them to AWTRIX |
| **Audio** | `#/audio` | MP3 sounds, RTTTL melodies and internet radio |
| **Palettes** | `#/palettes` | Build colour ramps for effects, text and charts |
| **Display** | `#/display` | Everything about what the matrix shows |
| **System** | `#/system` | Wi-Fi, MQTT, time, hardware, GPIO, maintenance |
| **Log** | `#/log` | Live device log |

The old `#/sounds` and `#/radio` bookmarks still work - they land on the Audio tab.

## Dashboard

The Dashboard is an instrument panel: what AWTRIX is showing, how it is doing, and the controls
that act on it - in that order.

### Live preview

The hero card is a canvas showing every LED, polled from `GET /api/v1/display/screen` four times a
second. Each LED is drawn as a square with a one-pixel gap, so you read it as a matrix rather than a
photo. Polling pauses while the tab is hidden.

You can fetch the same frame yourself:

```bash
curl http://<awtrix-ip>/api/v1/display/screen
```

See [HTTP API → Display](../reference/http.md#display) for the response shape.

### Screenshot and GIF

The two buttons on the right of the docked controls export what the preview shows. **↓** saves the
current frame as a PNG. **●** starts a recording - the button turns red, the preview polls up to 25
times a second, and pressing it again writes an animated GIF of everything since. Both files land in
your browser's download folder, named `awtrix-<timestamp>`. Both draw an LED as a block with the same
dark grid line the preview uses, blown up so the file is usable on a desktop: a 32×8 panel becomes a
3200×800 PNG, and 640×160 for the GIF, which pays for the size on every frame. Each GIF frame is held
for as long as it took to fetch the next one, so it plays back at the speed it was recorded. A
recording stops on its own after 300 frames - twelve seconds at the full rate, longer if AWTRIX
answers slower. Every frame is a request, so a recording costs the device a few frames per second of
its own render rate; nothing else about it involves the device.

### Docked controls

Directly under the canvas, acting on what you just saw:

| Control | What it does | API call |
|---|---|---|
| **Power** switch | Turns the LED matrix on and off | `PATCH /api/v1/display` `{"power":true}` |
| **Brightness** slider | 0–255, sent 300 ms after you stop dragging | `PATCH /api/v1/settings` `{"brightness":120}` |
| **◀ / ▶** | Previous / next app in the rotation | `POST /api/v1/apps/previous` · `/next` |
| **Bell** | Dismisses the notification currently on screen | `DELETE /api/v1/notifications/active` |
| **↓** | Saves the preview as a PNG | - |
| **●** | Records the preview as an animated GIF | - |

If the brightness slider snaps back, check **Display → Brightness → Auto brightness**. While that is
on, the ambient light sensor owns the brightness value.

### Vitals

A grid of tiles, each turning a raw number into a verdict. A tile is omitted, not blanked, when the
board does not report its value - a build with no battery simply has no battery tile.

| Tile | Shown when the device reports | Sub-line | Colour |
|---|---|---|---|
| **Battery** | `batteryPercent` | voltage, 2 decimals | green ≥ 40 %, amber ≥ 20 %, red below |
| **WiFi** | `wifiRssi` (always) | signal in words | green ≥ −65 dBm, amber ≥ −75 dBm, red below |
| **Light** | `lightLevel` | raw sensor reading | always blue - a measurement, not a verdict |
| **Temperature** | `temperature` | - | always blue |
| **Humidity** | `humidity` | - | always blue |
| **FPS** | `fps` | `/ 42` | green ≥ 40, amber ≥ 32, red below |

Colour is never the only signal: the WiFi tile also spells its verdict out - *excellent / good /
fair / weak*. Temperature has no bar, because it has no natural 0–100 scale.

What each number measures, and its caveats, is in
[Device state](../reference/device.md#always-present-fields).

Below the tiles, a metadata line: version, IP address, uptime, free RAM, PSRAM (on boards that
have it), current app.

## Apps

The top card, **App rotation**, *is* the loop - the list you see is exactly what AWTRIX will
rotate through after you save. Drag a row by its ⠿ grip to reorder it; it works with the mouse and
with a finger.

Everything else a row can do sits behind its **⋯** menu:

| Menu entry | Effect |
|---|---|
| **Show now** | Show this app right now (`PUT /api/v1/apps/active`) |
| **Duplicate** | The app appears twice per cycle |
| **Settings** | Scripts and modules that declare settings: opens them under the row |
| **Edit** | Scripts and modules: opens the source in the [Scripts](#scripts) tab |
| **Deactivate** / **Activate** | Switches the app off or back on |
| **Delete** | Removes it, two-step confirm |

There are two ways to be rid of an app, and they mean different things. **Deactivate** switches it
off and keeps it: it stays on the list, in its place, ready to come back. **Delete** removes it
outright - out of AWTRIX and off the list. Deleting takes effect at once; taking the name off the
list is written when you hit **Save**.

That is also how you get rid of a name you mistyped. A pushed app you stop sending stays on the list
marked *no data*, keeping its place for the next push, and there is nothing left to delete -
**Delete** takes the name off the list all the same.

A chip on the row says where an app came from: *pushed* for one sent in over the API, *script* for a
Berry app running on AWTRIX. Built-in apps carry no chip and offer no **Delete**, only
**Deactivate**. A script is deleted from the Scripts tab, next to the editor that wrote it.

While a settings panel is open, the row's **⋯** turns into **✕** to close it again.

The last card, **Modules**, lists the Berry files other scripts
[import](../guides/scripting.md#sharing-code-between-scripts) instead of running in their own right.
It shows the ones there is something to do about: a module with
[settings several apps share](../guides/scripting.md#settings-several-apps-share), which gets the
same **Settings** entry an app does, and any module stuck on an error. Plain library code stays out
of the way - the Scripts tab lists every module. A module never draws, so its **⋯** menu holds only
**Settings** and **Edit**. The card is hidden while there is nothing to show.

Reordering, duplicating and switching apps on or off reach AWTRIX only when you hit **Save** in the
sticky bar, which sends the whole list at once:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/order \
  -H 'Content-Type: application/json' \
  -d '{"order":["Time","Date","Temperature"],"disabled":["Humidity","Battery"]}'
```

The **Background** card appears when a
[headless script](../guides/scripting.md#running-without-ever-being-shown) is running. Those scripts
work without ever being drawn - an MQTT listener that only raises notifications, say - so they have
no place in the rotation and no order to sit in.

The **Disabled** card holds everything switched off. Nothing there runs, whatever it is made of.
Bring any of it back with **+ Activate**: an ordinary app returns to the rotation, a headless script
to the Background card. New apps arrive switched on.

Endpoint details: [HTTP API → Apps](../reference/http.md#apps).

## Scripts

An editor for Berry apps: a file tree of what is installed on the left - **Scripts** first, then
**Modules** - and the source on the right, with syntax highlighting, API completion (**Ctrl-.**) and the compiler's error
marked on the offending line. **Ctrl-S** saves and installs in one step. A status bar under the
editor shows the caret position and how many of the allowed bytes you have used.

The buttons above the editor:

| Button | Effect |
|---|---|
| **+** | New app from a template - the **+** on the Modules section starts a [module](../guides/scripting.md#sharing-code-between-scripts) instead |
| **Import** | Load a `.ax` file from your computer into the editor |
| **Export** | Save what is in the editor as a `.ax` file |
| **Show on AWTRIX** | Switch the panel to the script you are editing, so you can watch a change land |
| **Save** | Install the buffer under the name in the name field |

An unsaved buffer survives a detour through another tab, and switching files with unsaved work
prompts rather than discarding it. To rename, edit the name and save: AWTRIX stores the new name
and removes the old one.

Below the file tree sits **Shared**: every value the installed scripts have published to each other
through [`shared.set()`](../guides/scripting.md#talking-to-other-apps), refreshed every three
seconds. Each row is `owner.key`, its current value, and how long ago it was written - the age turns
amber past five minutes. It is read-only, and nothing in it survives a reboot.

The language, the callbacks and worked examples are in [AWTRIX scripting](../guides/scripting.md).

## Display

Everything about what the matrix shows, in seven sections with a sticky subnav: **Brightness**,
**Colour**, **App rotation**, **Clock & date**, **Text**, **Weather overlay**, **Sensors**.

Every field row shows a plain-language label *and* the API key it maps to, in small mono type - so
you can read the form and write the curl.

Only changed fields are sent. One sticky save bar at the bottom counts your unsaved changes;
**Save** collects only the dirty fields, **Discard** reverts them. The page never blindly re-writes
every setting.

**Display on**, **Overlay** and the overlay speed are runtime state and go to
`PATCH /api/v1/display`; everything else on this tab goes to `PATCH /api/v1/settings`.

**Black means "inherit".** For the nullable colour fields (`timeColor`, `dateColor`,
`temperatureColor`, `humidityColor`, `batteryColor`) the colour picker cannot show "nothing", so
`#000000` stands in for `null` - meaning *use the global text colour*. For `colorCorrection` and
`colorTint`, `#FFFFFF` stands in for `null` - meaning *off*. The UI converts back to `null` when it
saves. The API tracks the `null` state separately from the colour value, so to set one of these to
true black, PATCH it directly:

```bash
curl -X PATCH http://<awtrix-ip>/api/v1/settings \
  -H 'Content-Type: application/json' \
  -d '{"timeColor":"#000000"}'
```

Exhaustive tables - every key, type, range, default and unit:

- [Settings → Brightness](../reference/settings.md#brightness)
- [Settings → App rotation](../reference/settings.md#app-rotation)
- [Settings → Clock app](../reference/settings.md#clock-app), [Clock text](../reference/settings.md#clock-text), [Date text](../reference/settings.md#date-text), [Weekday bar](../reference/settings.md#weekday-bar)
- [Settings → Global text](../reference/settings.md#global-text)
- [Settings → Sensor apps](../reference/settings.md#sensor-apps)
- [Settings → Panel](../reference/settings.md#panel) - the **Colour** section: saturation, gamma, correction, tint
- [Visuals → Weather overlays](../reference/visuals.md#weather-overlays) · [Transitions](../reference/visuals.md#transitions)

## System

Eleven sections: **WiFi**, **Web server**, **MQTT**, **Time**, **Panel**,
**Brightness & sensors**, **GPIO**, **Buttons**, **Audio**, **Scripting**, **Misc** -
plus maintenance and backup.

**Audio** is the one section that spans both APIs: the sound switch, the two volumes and the radio
track info live in `/api/v1/settings`, `dfplayer` in `/api/v1/system`. The page splits a save
between the two endpoints for you.

The MQTT section carries a live connection badge above its fields: whether the broker answered, the
endpoint it connected to, and the reason if it did not.

When a save touched `/api/v1/system`, an amber banner appears: **a reboot is required to apply the
new configuration**, with a **Reboot now** button. Changing only the sound settings applies at once
and raises no banner.

An **Advanced** section appears only when AWTRIX reports a field this page has no widget for -
after a firmware update that added one, for instance. The value is offered raw, under its own name,
so a new setting is never unreachable. A UI in step with its firmware shows no Advanced section at
all.

Field tables: [System configuration](../reference/system.md) -
[Wi-Fi](../reference/system.md#wi-fi),
[MQTT and Home Assistant](../reference/system.md#mqtt-and-home-assistant),
[Time](../reference/system.md#time),
[Identity, web server and authentication](../reference/system.md#identity-web-server-and-authentication),
[Panel and orientation](../reference/system.md#panel-and-orientation),
[Sensor calibration](../reference/system.md#sensor-calibration),
[Auto-brightness](../reference/system.md#auto-brightness),
[Buttons](../reference/system.md#buttons),
[Sound hardware](../reference/system.md#sound-hardware) · [Settings → Sound](../reference/settings.md#sound),
[Miscellaneous](../reference/system.md#miscellaneous).

### Passwords

`GET /api/v1/system` never returns passwords, so the Wi-Fi, MQTT and auth password fields always
render empty with a placeholder: *(unchanged - leave empty to keep)*. An untouched password field is
never dirty and never sent - leaving it blank keeps the stored value.

The one exception is a backup download, which asks for `GET /api/v1/system?secrets=1` so the real
passwords go into the file. In provisioning mode the `secrets` parameter is ignored - the request
succeeds, but the passwords are left out - because that access point is open.

### Panel setup

**Panel** describes your matrix as panels rather than as a preset: how wide one panel is, how many
of them the cable runs through, which corner the first LED sits in, whether the strip runs along
rows or columns, and whether every second run comes back the other way. A read-only **Matrix size**
line above the fields does the sum while you type - `32 × 8 = 256 LEDs` - and turns red if the
total width leaves the 32-128 range AWTRIX accepts, so an invalid combination is never
sent.

The built-in apps are drawn for a 32-pixel panel and that layout is centred on anything wider, so a
64 or 128 pixel matrix shows the same clock, date and sensor faces with empty space either side
rather than a picture stuck to the left edge.

**Mirror** and **Rotate 180°** sit at the bottom of the same section, because they are about the
picture rather than the cable: **Mirror** flips it left to right, **Rotate 180°** turns it upside
down and swaps the left and right buttons, which is what an upside-down panel needs.

Every field except the total width takes effect on the next frame, so you can flip **Serpentine**
and watch the panel to see whether that was the problem. Field table:
[Panel and orientation](../reference/system.md#panel-and-orientation).

### Wi-Fi scan

The **Scan** button next to the SSID field asks AWTRIX to survey the air
(`GET /api/v1/system/wifi-scan`). The scan is asynchronous - the UI polls until it completes, then
fills a dropdown sorted by signal strength, with a 🔒 on encrypted networks.

### GPIO

A full pin map - matrix data, three buttons, battery ADC, light sensor ADC, buzzer, I²C bus,
DFPlayer serial and the three I²S pins for an external audio DAC. Defaults are the stock pin map
for the compiled chip.

**Matrix data** is a dropdown, not a free number: only pins with a compiled-in LED driver are
offered. The others are plain numbers where `-1` means "not connected".

Two **Preset wiring** buttons (Ulanzi TC001, AWTRIX 2 mainboard) fill the fields *without* saving -
the fields stay marked dirty so you review the map before it reaches AWTRIX. They appear on
ESP32 boards, where those wirings are real.

Conflicting or impossible assignments are rejected on save, and GPIO changes apply after a reboot.

Details and recovery: [GPIO & boards](../reference/gpio.md) -
[Board presets](../reference/gpio.md#board-presets),
[Validation rules](../reference/gpio.md#validation-rules),
[Recovery from a bad map](../reference/gpio.md#recovery-from-a-bad-map).

### Maintenance

| Action | What happens |
|---|---|
| **Upload firmware (.bin)** | Uploads to `/update` with a live progress bar; AWTRIX reboots into the new firmware |
| **Reboot** | Two-step confirm, then `POST /api/v1/device/reboot`; the page reloads itself |
| **Reset settings** | Two-step confirm, then `POST /api/v1/settings/reset` - display settings only, network survives |
| **Factory reset** | Erases **everything** (Wi-Fi, files, settings). No two-step button: you must type the hostname |

!!! danger "Factory reset is not undoable"
    Typing the hostname *is* the confirmation. It erases stored Wi-Fi credentials, every uploaded
    file and all settings, and drops AWTRIX back into provisioning mode.

See [Persistence and resets](../reference/system.md#persistence-and-resets) and
[Firmware upload](../reference/http.md#firmware-upload).

### Backup and restore

**Create backup** downloads a `.zip` of whatever you tick: Wi-Fi, settings, icons, melodies,
palettes, scripts, app order. The file is assembled in your browser, so nothing is stored on
AWTRIX and nothing leaves your network.

**Restore backup** takes a `.zip` back. Anything that could not be applied is reported as a warning,
and if the restore touched Wi-Fi, system config, settings or the app loop you are offered a reboot.

If you include **Wi-Fi** or **settings**, the file holds your Wi-Fi, MQTT and web-login passwords in
plain form. Keep it where you would keep the passwords themselves.

## Icons

A storage bar (used / total / free, turning red past 90 % full), a drag-and-drop upload zone, and a
grid of every icon on AWTRIX.

Icons are `.png`, `.jpg`, `.jpeg` or `.gif`, 8×8 for a static icon. PNG and JPG are turned into a
GIF as they upload - sharper on the panel and smaller on AWTRIX - so `smiley.png` becomes
`smiley.gif`, replacing an older `smiley.jpg` if you had one. Animated GIFs stay animated, and a
full-width (32×8) animated GIF is also accepted - it renders as a background across the whole panel
rather than a single icon tile; see [Payload → Icon](../reference/payload.md#icon). Each tile has
three buttons: an **eye** shows the icon on the panel for three seconds, a **pencil** opens it in
the [Icon Editor](#icon-editor), and a **bin** deletes it (two-step confirm).

Files upload one at a time, each with its own progress line.

You can do the same from a shell:

```bash
curl -X POST 'http://<awtrix-ip>/api/v1/files?dir=/ICONS' \
  -F 'file=@smiley.jpg'
```

**LaMetric icon download** fetches an icon from the LaMetric gallery *in your browser* - AWTRIX
never talks to the internet for this - converts it to GIF, then uploads it. Paste a numeric icon
ID, hit **Fetch**, preview it, then **Save to AWTRIX**. It is disabled when your browser is offline, so it is unavailable
in provisioning mode.

More: [HTTP API → Files](../reference/http.md#files) · [Payload → Icon](../reference/payload.md#icon).

## Icon Editor

A pixel editor for drawing 8×8 or 32×8 icons and saving them straight to AWTRIX - the visual
alternative to uploading a finished file. Draw, give the icon a name, and save; it appears in the
[Icons](#icons) tab, ready to use. Press the pencil on any icon tile to open it here and edit it.
A **Live** toggle mirrors the drawing onto the real matrix while you edit.

The editor itself (powered by the open-source [Piskel](https://github.com/piskelapp/piskel)) is
loaded into the tab from the internet. Without internet access the tab reports that the editor did
not load; everything else in the web UI is served by AWTRIX and keeps working.

More: [Guides → Icon editor](../guides/icon-editor.md).

## Audio

Everything that makes noise, in one tab with three sections: **MP3 sounds**, **Melodies** and
**Radio**. Sections only appear when the build can use them - a device without a speaker (I2S DAC)
shows just the melody editor. When sound is switched off in the settings, a line at the top of the
tab says so.

### MP3 sounds

Short MP3 clips stored on AWTRIX, managed like icons: drag files onto the upload zone or click it
to choose, and every clip appears as a row with its size. Only on builds with a speaker.

| Button | What it does |
|---|---|
| **🔊** | Plays the clip **in your browser** |
| **▶** | Plays it **on AWTRIX** - the row is marked while it plays |
| **Bin** | Deletes it (two-step confirm) |
| **■ Stop** | Stops playback on AWTRIX |

The file name (without `.mp3`) is the clip's address: `ding.mp3` plays as `sound:"ding"` in a
notification or `{"name":"ding"}` on the API. If the radio is streaming, a clip interrupts it and
the stream reconnects afterwards. More: [Sounds](../guides/sounds.md).

### Melodies

An editor, not a file list. One row per melody on AWTRIX, each one a `/MELODIES/<name>.txt` you
can change in place. **+ New melody** adds an empty row.

| Field or button | What it does |
|---|---|
| **Name** | The melody's address - what `sound:"<name>"` in a notification refers to. 1–24 characters of `A-Z`, `a-z`, `0-9`, `_`, `-` |
| **RTTTL** | Only the part *after* the name: `d=4,o=5,b=100:e,c`. AWTRIX puts the name back on when it saves |
| **🔊** | Plays it **in your browser**, through WebAudio |
| **▶** | Plays it **on AWTRIX** |
| **💾** | Saves that one row. Lights up once the row is changed and valid |
| **Bin** | Deletes it (two-step confirm) |
| **■ Stop** | Silences the browser and AWTRIX |

The melody is checked while you type. A row that does not parse is outlined, and the line beneath it
names the problem - `'h' is not a note` - instead of just calling it invalid. When it does parse,
that same line reads `2 notes · 2.4 s`.

Both play buttons send **what is in the fields right now**, so you can hear an edit before you commit
it. Since 🔊 never leaves the browser, you can write a melody with AWTRIX muted or out of earshot.

Drop a complete three-part string - `jackpot:d=8,o=5,b=120:16c,16e,16g,c6` - into the RTTTL field
and it is split for you: the name goes to the name field, the rest stays put.

**Renaming** is just editing the name and saving; AWTRIX stores the new file and removes the old.

A melody that does not parse is still shown, with the parser's complaint, so you can fix it.

More: [Sounds & melodies](../guides/sounds.md) · [HTTP API → Sounds](../reference/http.md#sounds).

### Radio

One list of internet radio stations, each a name and a stream URL. **▶** on a row starts it,
**Stop** stops playback, and **Save stations** writes the whole list at once. A line above the list
says what is playing, including the track title when the stream sends one.

The section is only there on builds that can decode a stream. Wiring, station formats and limits:
[Internet radio](../guides/radio.md).

## Palettes

Every palette AWTRIX knows, each drawn as the ramp it really paints, beside one editor. A palette
is 1 to 16 colour **stops** - two of them are a gradient - and the eight built-ins can be replaced and
restored from here.

Walkthrough: [Palette editor](../guides/palette-editor.md) · reference:
[Visuals → Palettes](../reference/visuals.md#palettes).

## Log

The AWTRIX log, polled once a second while the tab is visible. Only new lines are fetched
(`GET /api/v1/logs?after=<n>`), so it is cheap to leave open.

**Auto-scroll** is a switch, but it also follows you: scroll up and it pauses, scroll back to the
bottom and it resumes. **Copy** puts the whole buffer on your clipboard - handy for bug reports.
**Clear** empties your browser's copy only; the ring buffer on AWTRIX is untouched.

Verbose logging lives in **System → Misc → Debug mode**.

## Provisioning mode

Fresh out of the box - or after a factory reset - AWTRIX opens its own access point instead of
joining a network. Connect to it and the setup page opens by itself: it answers your
platform's connectivity check (iOS, Android and Windows all probe a known URL) with a redirect to its
own address.

The UI detects this and collapses to the one page that can do anything here: **System**, carrying a
blue banner - *Provisioning mode: connect AWTRIX to your WiFi below.* - the Wi-Fi settings, a
**Reboot** that applies them, and **Restore** for putting a backup back after a wipe.
The other tabs are hidden, because the API refuses every write behind them until AWTRIX is on a
network.

Save your credentials, then reboot. AWTRIX leaves the access point and comes back on your
network.

A configured username and password are enforced on the access point exactly as they are on your
network. See [Authentication](../reference/http.md#authentication).

## Conventions this UI follows

The web UI is a thin client over `/api/v1`, and it inherits the API's conventions wholesale:

- Every field row prints its **API key** next to the label - the form maps 1:1 to the reference docs.
- Errors appear as toasts carrying the API's own message and offending field, straight out of the
  `{"error":{"code","message","field"}}` body. See [Errors](../reference/errors.md).
- Settings are validated as a set: a rejected `PATCH /api/v1/settings` applies **nothing**.
- Destructive actions never use a browser `confirm()` - they arm on first click and act on the
  second, within a three-second window.

## Related

- [HTTP API v1](../reference/http.md) - every route, with curl
- [MQTT topics](../reference/mqtt.md) - the same commands over a broker
- [App & notification payload](../reference/payload.md) - what you can put on the screen
