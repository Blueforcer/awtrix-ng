# Icons

This page shows how to get icons onto AWTRIX and show them next to the text of an app or a
notification.

## What you get

Install the icon `sun` from the [AWTRIX Hub](#install-from-the-awtrix-hub), then send:

<!-- panel -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/weather \
  -H 'Content-Type: application/json' \
  -d '{"icon":"sun","text":"21°C"}'
```

The sun stands at the left edge, and the text in the space to its right.

## How it behaves

A new AWTRIX has no icons: you add the ones you want, from the AWTRIX Hub, as files of your own,
or drawn in the [Icon editor](icon-editor.md). Each icon is a GIF or JPEG file in `/ICONS`, and
its file name without the extension is its ID: `sun.gif` is `"icon":"sun"`. The `icon` key puts
the icon at the left edge. An 8×8 icon takes the left 8 columns and a gap of 1, and the text uses
the rest ([Icon and text](display.md#icon-and-text)).
<!-- only tc002 -->
At double size, the icon covers the left 16 columns and the gap 2, so the space for the text
starts at column 18.
<!-- /only -->
Drawing commands ignore the icon and count from the left edge<!-- only tc002 --> of the 26 × 8
grid<!-- /only -->, so a shape next to an 8×8 icon starts at `x = 9`.

## Install from the AWTRIX Hub

The [AWTRIX Hub](https://awtrix.de/icons) is a free community collection of icons.

1. In the web UI, open **Icons → Add → Icons for this device**, or go to the
   [AWTRIX Hub](https://awtrix.de/icons) directly.
2. Search for an icon and open it.
3. Press **Send to AWTRIX**.

Your browser downloads the icon and copies it to AWTRIX over your local network. AWTRIX itself
never contacts the Hub.

Downloading needs a Hub sign-in. Reloading an installed Hub icon, publishing, and installing the
icons a script asks for with [`# @icons`](scripting/drawing.md#the-icons-your-script-needs) need a Hub
connection key:

1. Create a key in [your Hub account](https://awtrix.de/account/settings).
2. Paste it in the web UI under **System → AWTRIX Hub**.

The key stays in this browser and is not stored on AWTRIX.

Icons from the Hub carry a **Hub** badge in the web UI. Use the icon's name in a payload or in a
script's `# @icons` line.

### Share an icon in the Hub

Every icon on the device has **Publish to Hub** in its **⋯** menu, and the
[Icon editor](icon-editor.md) can publish your drawing. Both share the icon in the Hub under a
display name you choose.

You need an AWTRIX Hub account and the connection key under **System → AWTRIX Hub**. The icon must
be a GIF of at most 52×16 pixels and under 64 KB. If the exact same image is already in the Hub,
it is refused and the Hub names the icon that has it. Accepted icons are published at once. There
is no review, and unsuitable icons are removed afterwards.

## Upload an icon

1. In the web UI, open **Icons → Add**.
2. Drop your files on the upload area.

The web UI takes GIF, PNG and JPG and turns PNG and JPG into GIF for you, so `mail.png` lands as
`mail.gif`. To draw an icon yourself, or change one, use the [Icon editor](icon-editor.md). It
offers sizes up to your whole display<!-- only tc002 -->, 52×16<!-- /only -->.

### With curl

Upload the file, then use it:

```bash
# 1. upload an 8x8 JPEG. The file name becomes the icon ID.
curl -X POST "http://<awtrix-ip>/api/v1/files?dir=/ICONS" \
  -F "file=@1234.jpg"

# 2. use it
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"Mail","icon":"1234"}'
```

The icon ID is **the file name without the extension**. `1234.jpg` is `"icon":"1234"` in a
payload, never `"1234.jpg"` and never a path. IDs are case-sensitive, so `Mail.jpg` is `"Mail"`.

`?dir=` defaults to `/ICONS`, so you can leave it out for icons. The name of the form field does
not matter, only the file name counts.

## Choose a format and size {#icon-formats}

AWTRIX shows two formats. For an ID it looks for these files, in this order:

| Order | Path | Format |
|---|---|---|
| 1st | `/ICONS/<id>.gif` | GIF, animated or still |
| 2nd | `/ICONS/<id>.jpg` | still JPEG |

**Use GIF.** At icon size a JPEG looks blurry and is larger. Use `.jpg` only for icons you already
have.

### Size

- A **JPEG** icon in the `icon` field or in Berry `icon()` should be 8×8.
- A **GIF** keeps its own size, up to the **width and height of your display**.
  <!-- only esp32 esp32-s3 -->A 64×8 GIF plays at full size on a 64×8 display.<!-- /only --><!-- only tc002 -->A 52×16 GIF fills the whole display.<!-- /only -->
  In the `icon` field, a GIF as wide as the display is drawn as a background behind the text.
  <!-- only tc002 -->Pushed apps and notifications are drawn at double size, as on a 26×8 display: an
  icon up to 26×8 is enlarged with the app, and a 26×8 GIF is its background. An app with a bigger
  icon, such as a 52×16 GIF, keeps its size, as it does with
  [`enlargeApps`](../reference/settings.md#global-text) off.<!-- /only -->
- The same limits apply to script icons<!-- only esp32 esp32-s3 -->, pushed apps and notifications<!-- /only -->.
<!-- only tc002 -->
- In a [layout](layouts.md) image region, JPEG and GIF keep their own size up to the display size.
  They are aligned and cut off at the region's edge, not scaled.
<!-- /only -->
- The web UI converts PNG and JPEG up to the display size into GIF without resizing, and refuses
  larger images.

Keep every animation frame within your display's width and height, and resize large GIFs before
you upload them. If an image does not load, try a smaller or shorter GIF.

### GIF playback

| Behavior | Detail |
|---|---|
| Looping | Plays forever, and the loop count in the file is ignored |
| Frame delay | Each GIF uses its own frame timings. A delay of `0` becomes **100 ms** |
| Colors | Each GIF keeps its own colors, also when several GIFs are shown |
| Transparency | Transparent pixels keep what the previous frame drew there |

## Show an icon next to the text {#use-an-icon-in-a-payload}

Notifications and pushed apps both take the `icon` key, as in [What you get](#what-you-get). By
default the icon stays where it is while long text moves. With `"iconMode":"push"`, the moving
text pushes the icon out of the display, and the icon comes back on every run:

<!-- panel motion=4 -->
```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H "Content-Type: application/json" \
  -d '{"text":"Long headline that scrolls","icon":"sun","iconMode":"push"}'
```

### More icons where you want them

The `icons` array shows up to four more images, each at its own `x` and `y`. Each one animates on
its own, and they take no space from the text:

<!-- panel -->
```json
{
  "icons": [
    {"icon": "sun", "x": 0, "y": 0},
    {"icon": "sun", "x": 16, "y": 0}
  ]
}
```

<!-- only tc002 -->
At double size, `x` and `y` count in the 26 × 8 grid, so `"x": 16` lands at column 32 of the
display.
<!-- /only -->
You can use the `icon` key in the same payload.

## Send the icon inside the payload {#inline-icons}

You can also send the image inside the payload, without uploading a file. Write it as a data URL
in place of the ID:

* `data:image/gif;base64,` followed by a base64-encoded GIF
* `data:image/jpeg;base64,` followed by a base64-encoded JPEG

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d "{\"text\":\"Inline\",\"icon\":\"data:image/jpeg;base64,$(base64 -w0 1234.jpg)\"}"
```

This suits a one-off notification that should not leave a file behind. The image travels with
every request, so the payload is larger. Script apps can pass the same data URL to `icon()`.

<!-- only tc002 -->
## Show a picture from the internet {#pictures-from-the-internet}

Instead of an icon name, you can give the web address of a picture. The clock downloads the
picture and shows it as the icon. This is handy for album covers, camera snapshots and logos.

### How to use it

Write the address where you would write the icon name:

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"Now playing","icon":"https://example.com/cover.jpg"}'
```

An address works everywhere an icon name does:

- `icon` and `icons` in [apps and notifications](../reference/payload.md#icon)
- the `icon` of a [layout](layouts.md) region
- `icon()` in a [script](scripting/drawing.md#icons)

### What you see

- The picture fills a square as tall as the display, 16 × 16 pixels. In a layout region, it fills
  the whole region.
- A picture of another shape keeps its middle. The clock cuts off the edges.
- The square appears right away. It stays empty until the picture has arrived, so the text next
  to it does not jump.
- An animated GIF keeps playing. It is not resized but plays at its own size in the middle of the
  square.

### Which pictures work

- **JPEG, PNG and GIF.** The clock checks the file itself, so the address does not have to end in
  `.jpg`.
- **A GIF** must fit into the square and be no larger than 8 KB.
- **Progressive JPEG** works too, up to 1920 × 1280 pixels for a usual photo. A file saved with
  more color detail allows less.
- **Not supported** is one rare kind of file: interlaced PNG. Save such a picture again as a normal
  PNG.

### How the clock downloads

- The clock downloads each picture once and keeps it in memory. After a restart it downloads it
  again.
- The same address in two sizes counts as two pictures. For example, a cover used as an icon
  and in a larger layout region is downloaded twice.
- The clock follows redirects.
- The clock checks HTTPS certificates. A server with a self-signed certificate does not work.

### The address

- It starts with `http://` or `https://`.
- It has at most 2048 characters.
- It has no spaces or special characters. Write them in the encoded form, for example `%20`
  for a space.

### When the square stays empty

The clock tries again by itself while the icon is shown: after 30 seconds, then after
2 minutes, then every 10 minutes.

To find the cause, open the **Log** tab in the web UI. A failed picture writes one line that
starts with `picture from` and the name of the server. The rest of the address is left out,
because it often holds a password or token.

| The log says | What to do |
| --- | --- |
| `HTTP 404` or another number | The address is wrong, or the picture is gone. Check it in a browser. |
| `no answer` | The clock cannot reach the server. Check the address and the network. |
| `larger than 1 MB` | Use a smaller file. |
| `too many pixels` | Use a picture with fewer pixels. |
| `GIF larger than 16x16 or 8 KB` | Make the GIF smaller. |
| `not a JPEG, PNG or GIF the display can read` | Save the picture again as a normal JPEG or PNG. |

Album covers from Home Assistant: [Home Assistant → Showing album covers](home-assistant.md#showing-album-covers).
<!-- /only -->

## List and delete icons {#list-and-delete}

1. In the web UI, open the **Icons** tab. It lists every icon on AWTRIX.
2. To delete one, open its **⋯** menu, choose **Delete**, and press it again to confirm.

### With curl

```bash
# what is on AWTRIX, and how full is it?
curl "http://<awtrix-ip>/api/v1/files?dir=/ICONS"
```

The answer has a `files` array of `{"name": …, "size": …}` entries, plus `usedBytes` and
`totalBytes` for the whole storage.

```bash
# remove one: this takes a full path, not an ID
curl -X DELETE "http://<awtrix-ip>/api/v1/files?path=/ICONS/1234.jpg"
```

`DELETE` takes `?path=`, a full path. `GET` and `POST` take `?dir=`, a folder.

## Download an icon {#where-assets-live}

In the **Icons** tab, open the icon's **⋯** menu and choose **Download icon**.

### With curl

```bash
# download an icon from AWTRIX
curl http://<awtrix-ip>/ICONS/1234.jpg -o 1234.jpg
```

AWTRIX creates four folders at boot:

| Folder | Holds | Extension |
|---|---|---|
| `/ICONS` | icons | `.gif`, `.jpg` |
| `/MELODIES` | RTTTL melodies | `.txt` |
| `/PALETTES` | custom palettes | `.txt` |
| `/SCRIPTS` | Berry scripts and their saved data | `.ax`, `.json` |

`/ICONS/`, `/MELODIES/` and `/PALETTES/` can always be downloaded this way. `/SCRIPTS/*` and the
app order file `/apploop.json` can be downloaded except in setup mode, while AWTRIX runs its own
setup access point. A script's code is also available at
[`GET /api/v1/apps/script/{name}`](../reference/http.md#get-apiv1appsscriptname). If you set a web
login, it applies here too.

## Check the free space {#storage-space}

Icons, melodies, palettes and scripts share the free storage on AWTRIX.
<!-- only esp32 esp32-s3 -->
On a board with 4 MB flash that is **512 KB**, enough for a few dozen 8×8 JPEGs, or fewer animated
GIFs. Larger boards have more.
<!-- /only -->

The web UI shows the storage bar in the **Icons** tab. The API reports it as `usedBytes` and
`totalBytes` from `GET /api/v1/files`. An upload that does not fit is refused, so check the free
space before large uploads.

## Good to know

- **An ID that matches no file shows no icon, and the text is centered on the whole display.**
  Write the file name without the extension, with the same capitals: `Mail.gif` is `"Mail"`.
- **When both `mail.gif` and `mail.jpg` exist, `mail` always shows the GIF.** Delete the one you
  do not want.
- **The API refuses a PNG with `415 unsupportedMediaType`, also when you rename it to `.jpg`,
  because AWTRIX looks at the content.** Convert it to GIF first, or upload it in the web UI,
  which converts it for you.
- **After a rename in the icon's ⋯ menu, apps and scripts that use the old name show no icon.**
  Change them to the new name.
- **Until you set a web login, anyone who can reach AWTRIX can upload and delete files.** Set one,
  see [Authentication](../reference/http.md#authentication).

## Details

- [Payload → Icon](../reference/payload.md#icon): `icon`, `iconMode`, `iconOffsetX` and `iconGap`,
  the space an icon takes, the full-width GIF background, and a missing icon
- [Payload → Multiple icons](../reference/payload.md#multiple-icons): the `icons` array, its
  drawing order and limits
- [HTTP reference → Files](../reference/http.md#files): uploading, listing and deleting, with every
  status code
- [HTTP reference → Web UI and static assets](../reference/http.md#web-ui-and-static-assets):
  downloading files
- [Limits → Storage](../reference/limits.md#storage) and [Limits → Display](../reference/limits.md#display):
  the storage of each board, and the largest GIF
<!-- only tc002 -->
- [Limits → Pictures from the internet](../reference/limits.md#pictures-from-the-internet): every
  size and limit of a picture from a web address
<!-- /only -->
- [Persistence and resets](../reference/system.md#persistence-and-resets): a factory reset erases
  every file, a settings reset keeps them

## Related

- [How the display works](display.md): where the icon and the text sit
- [Icon editor](icon-editor.md): draw and edit icons in the web UI
- [Text & colors](text.md): the text next to the icon
- [Sound](sounds.md): `/MELODIES` and the RTTTL format
