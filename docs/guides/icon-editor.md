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

Publishing shares your drawing in the **AWTRIX Hub** gallery. Give it a descriptive name such as
“Sunny sky”. A LaMetric number such as “12345” is not accepted as a publication name: rename it
before publishing. Numbers can still be part of a name, for example “Battery 50”.

Publishing needs an AWTRIX Hub account. On the device, open **Publish your own icons** in the
Icons tab and connect using your Hub device token. The token stays in this browser; the embedded
editor does not receive it. You can also use the editor on the Hub, where your sign-in is sufficient.

If the same image or animation is already published, the Hub links to that entry instead of adding
a duplicate. Editing a Hub icon and publishing different content creates a new variation, preserving
the original. Saving on AWTRIX only updates your local file and never publishes it.

Authors can also choose **Update my published icon** to keep the same public ID. The Hub
checks the version you opened before accepting the update. If another window has already
changed it, your draft is kept and the update is rejected until you open the current version.

## Keep an editable draft

**Save draft** and **Ctrl+S** preserve the full project in this browser, including layers,
animation timing and Hub origin. **My drafts** lets you return to it later. **Download project**
and **Open project** move editable projects between NG and the Hub. These actions do not
change a device icon or publish it. In the Hub, signed-in drafts also sync privately to your
account; a conflicting edit is kept as a separate browser copy.

The Hub studio always shows a browser preview. Its optional **Preview on your AWTRIX NG**
connection sends a temporary preview directly from your browser; device credentials never go
to the Hub. Large animations that exceed the device's inline payload limit preview as a still
frame on the device while the full animation remains visible in the browser.

## Edit an icon you already have

Choose **Edit** from a tile's actions menu in the [Icons](../getting-started/web-ui.md#icons) tab. It
opens in the editor with that icon loaded - change it and save under the same name to replace it, or
a new name to keep both.

The gallery distinguishes **From the Hub**, **Locally changed**, and **Only on this device** by
checking the file's contents against its saved origin. Installing a Hub icon with the same filename
does not silently replace a different local drawing. Existing numeric filenames keep working with
local apps and scripts even when you publish the drawing under a descriptive Hub name.

For linked icons, **Reload from Hub** fetches the latest image even when its ID is unchanged.
Local edits require explicit replacement. Existing scripts keep using their local file until you
reload it; the `@icons` line and icon ID stay the same. A removed Hub entry does not delete an
installed copy or trigger a background notice.

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
