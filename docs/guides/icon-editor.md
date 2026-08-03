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
- Icons you draw are saved as **GIF**: pixel edges stay crisp, and animation and transparency are
  kept. An icon you opened from a `.jpg` - a LaMetric import, or a JPEG you uploaded - is saved back
  as a `.jpg`, which keeps neither.
- The editor is a small web app your browser loads from the internet (an
  [AWTRIX fork](https://github.com/Blueforcer/awtrix-piskel) of the open-source
  [Piskel](https://github.com/piskelapp/piskel) editor). On a network with no internet access the tab
  reports that the editor did not load; the rest of the web UI comes from AWTRIX and keeps working.

## Related

- [Icons & assets](icons.md) - uploading finished files, and using an icon in a payload
- [Web UI tour - Icons](../getting-started/web-ui.md#icons) - the tab your saved icons land in
- [Palette editor](palette-editor.md) - the same idea for colour ramps
