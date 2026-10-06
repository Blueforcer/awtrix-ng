# Icon editor

The **Icon Editor** tab in the web UI lets you draw icons and animations and save them straight to
AWTRIX.

## What you get

Draw a sun, save it as `sun`, and every app and notification can show it by that name:

<!-- panel -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"icon":"sun","text":"21°C"}'
```

Your drawing stands at the left edge, and the text in the space to its right.

## How it behaves

The editor saves every drawing as a GIF in `/ICONS` on AWTRIX, so pixel edges stay sharp and
animation and transparency are kept. The icon works as `"icon":"<name>"` in any app or
notification. Next to text, an 8×8 icon takes the left 8 columns and a gap of 1<!-- only tc002 -->,
at double size 16 columns and a gap of 2<!-- /only --> ([Icon and text](display.md#icon-and-text)).
[Icons → Size](icons.md#size) shows what the other sizes do. Saving changes only the file on your
device and never publishes anything. The editor is the open-source
[Piskel](https://github.com/piskelapp/piskel) editor in an
[AWTRIX version](https://github.com/Blueforcer/awtrix-piskel), loads from the internet and follows
the web UI's light or dark theme.

## Draw and save an icon

1. Open the **Icon Editor** tab.
2. Choose a size under **Resize**. There is a button for each size that fits your display:
   <!-- only esp32 esp32-s3 -->8×8, 32×8 and the whole display, for example 64×8.<!-- /only --><!-- only tc002 -->8×8, 32×8, 16×16 and the whole display, 52×16.<!-- /only -->
   You can also type your own width × height, up to the size of your display.
3. Draw.
4. Type a name and save.

The icon shows up in the [Icons](../getting-started/web-ui.md#icons) tab at once.

## Edit an icon you already have

1. In the [Icons](../getting-started/web-ui.md#icons) tab, open the icon's **⋯** menu and choose
   **Edit**. The icon opens in the editor.
2. Change it.
3. Save under the same name to replace it, or under a new name to keep both.

In the Icons tab, an icon from the Hub carries a **Hub** badge. Once you change it, it says
*Changed on the clock*, and its menu offers **Share as a new icon** instead of **Publish to Hub**.
Icons you made yourself carry no badge.

Installing a Hub icon never quietly replaces a different icon of the same name on AWTRIX. A numeric
file name keeps working in your apps and scripts even when you publish the drawing under a
descriptive Hub name. If an icon is removed from the Hub, your copy on AWTRIX stays.

## Watch your drawing on the display {#live-preview-on-the-display}

1. Turn on **Live** in the editor.
2. Draw. The display shows the frame you are editing, and when the animation plays, the display
   plays it too.
3. Turn **Live** off, or leave the tab, and the display goes back to its normal apps.

Each Live update has a size limit:

- A long animation that does not fit shows only the current frame. The web UI tells you once each
  time you turn **Live** on: *The animation is too large for Live, so the display shows its current
  frame.*
- A large drawing may not fit at all. The web UI then says why, *Too many pixels for one Live
  update* or *More colours than Live can carry* (for example a photo), and the display keeps the
  last picture that fit.

Turning **Live** off and on again shows the message again.

## Share an icon with everyone

Publishing shares your drawing in the **AWTRIX Hub** gallery.

1. Create a connection key in [your Hub account](https://awtrix.de/account/settings).
2. Save it in the web UI under **System → AWTRIX Hub**. The key stays in this browser, and the
   editor itself never sees it.
3. Give the icon a descriptive name, such as “Sunny sky”, and publish.

A number alone, such as “12345”, is not accepted as a name. Rename the icon first. Numbers can be
part of a name, for example “Battery 50”.

If the same image or animation is already in the Hub, the Hub links to it instead of adding a
copy. If you edit a Hub icon and publish it, the Hub adds it as a new variation and keeps the
original. As the author, you can choose **Update my published icon** to keep the same public ID.
If the icon was changed in another window since you opened it, the update is refused and your
drawing stays open. Open the current version and try again.

## Draw on the AWTRIX Hub instead

You can also use the editor on the Hub itself, where your Hub sign-in is enough. There, **New
icon** asks for the size first:
<!-- only esp32 esp32-s3 -->8×8, 32×8, or your own width × height.<!-- /only --><!-- only tc002 -->8×8, 16×16, 52×16, or your own width × height up to 52×16.<!-- /only -->
The size of the open drawing is shown next to its name. The optional **Device** live preview
connects from your browser straight to your AWTRIX NG. Your device login never goes to the Hub.

## Good to know

- **Without internet access, the tab says the editor could not load, while the rest of the web UI
  keeps working.** Use a network with internet access.
- **Publishing fails without the connection key, or with a name made only of digits.** Check the
  key under **System → AWTRIX Hub**, and give the icon a descriptive name.
- **A `.jpg` icon that you open and save under its own name becomes a `.gif`, and the `.jpg` is
  deleted.** Save under a new name to keep the JPEG.

## Details

- [Icons → Choose a format and size](icons.md#icon-formats): how each size shows, and how a GIF
  plays
- [Payload → Icon](../reference/payload.md#icon): every icon key of an app or a notification
- [Limits → Display](../reference/limits.md#display): the largest GIF your display shows

## Related

- [Icons](icons.md): uploading finished files, and using an icon in a payload
- [The web UI → Icons](../getting-started/web-ui.md#icons): the tab your saved icons land in
- [Palette editor](palette-editor.md): the same idea for color ramps
