# Icon editor

The web UI's **Icon Editor** tab is a pixel editor for drawing icons and saving them straight to
AWTRIX - 8×8 for a normal icon, or 32×8 for a full-width one. It is the visual alternative to
uploading a finished `.gif` or `.jpg` (see [Icons & assets](icons.md)).

## Draw and save an icon

1. Open the **Icon Editor** tab.
2. Choose the size - 8×8 or 32×8 - and draw.
3. Type a name and save.

The icon is stored on AWTRIX and appears in the [Icons](../getting-started/web-ui.md#icons) tab
straight away, ready to use in any app or notification as `"icon":"<name>"`.

## Share an icon with everyone

**Save to Cloud**, next to Save to AWTRIX, submits the drawing to the shared icon database instead
of storing it on your clock. Type the name you want it listed under and press it.

Publishing needs an **AWTRIX Hub** account. The editor you see here is framed by a page served from
your clock, and a Hub sign-in cannot travel that far - so from this tab the status line says so and
offers a link to the Hub's own editor, where the same drawing publishes under your account. There it
appears in the collection immediately; there is no review queue, and the status line links straight
to the published icon.

The other answer you may see is a refusal - most often because the identical image is already in the
collection, in which case it names the entry that holds it.

Saving to the cloud does **not** put the icon on your clock; use Save to AWTRIX for that, and do
both if you want it in each place.

## Edit an icon you already have

Press the **pencil** button on any tile in the [Icons](../getting-started/web-ui.md#icons) tab. It
opens in the editor with that icon loaded - change it and save under the same name to replace it, or
a new name to keep both.

## Live preview on the matrix

Turn on the **Live** toggle in the editor to mirror your drawing onto the real matrix while you work.
The frame you are editing shows as you draw; when your icon has several frames and the animation is
playing, the matrix plays the animation too. Turn **Live** off - or just leave the tab - to hand the
display back to the normal app rotation.

## Good to know

- The editor follows the header's **light/dark** theme.
- Icons are always saved as **GIF**: pixel edges stay crisp, and animation and transparency are
  kept. Open an old `.jpg` and it is saved as a `.gif`, replacing the `.jpg`.
- The editor is a small web app your browser loads from the internet (an
  [AWTRIX fork](https://github.com/Blueforcer/awtrix-piskel) of the open-source
  [Piskel](https://github.com/piskelapp/piskel) editor). On a network with no internet access the tab
  reports that the editor did not load; the rest of the web UI comes from AWTRIX and keeps working.

## Related

- [Icons & assets](icons.md) - uploading finished files, and using an icon in a payload
- [Web UI tour - Icons](../getting-started/web-ui.md#icons) - the tab your saved icons land in
- [Palette editor](palette-editor.md) - the same idea for colour ramps
