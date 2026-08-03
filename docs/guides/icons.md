# Icons & assets

AWTRIX ships with an empty `/ICONS` directory. This page is about filling it: getting an
8×8 image in there, giving it a name, and putting that name in a payload.

## Upload an icon

Two commands: upload the file, then use it.

```bash
# 1. upload an 8x8 JPEG. The file name becomes the icon ID.
curl -X POST "http://<awtrix-ip>/api/v1/files?dir=/ICONS" \
  -F "file=@1234.jpg"

# 2. use it
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d '{"text":"Mail","icon":"1234"}'
```

The icon ID is **the file name without the extension**. `1234.jpg` on disk is `"icon":"1234"`
in a payload - never `"1234.jpg"`, never a path. IDs are matched literally and are
case-sensitive, so `Mail.jpg` is `"Mail"`.

The `?dir=` query parameter defaults to `/ICONS`, so it can be omitted for icons - spell it out
when you upload to `/MELODIES` or `/PALETTES`. The multipart **field name is irrelevant**
(`-F "file=@…"`, `-F "whatever=@…"` - both work); only the file name matters.

The built-in web UI at `http://<awtrix-ip>/` has a file manager for `/ICONS`, `/MELODIES` and
`/PALETTES` that drives the same endpoint, if you would rather drag and drop. Its **Icon Editor**
tab can also draw an 8×8 or 32×8 icon from scratch (or edit an existing one) and save it straight to
`/ICONS` - see [Icon editor](icon-editor.md).

Every upload is checked against the format its target folder expects: a GIF or JPEG for `/ICONS`,
valid RTTTL for `/MELODIES`, printable text for `/PALETTES`. A mismatch is rejected with
`415 unsupportedMediaType` and the partial file is removed. A failed write - a full filesystem,
usually - answers `500 internalError` rather than a false success. There is no size limit, and
the check cannot catch a well-formed but visually broken icon.

## Icon formats

Two formats are decodable, and an ID is resolved by trying both extensions in order:

| Tried | Path | Format |
|---|---|---|
| 1st | `/ICONS/<id>.gif` | animated GIF |
| 2nd | `/ICONS/<id>.jpg` | static JPEG |

**GIF wins.** `<id>.gif` is always tried first, and `<id>.jpg` only if that fails, so `mail.gif`
and `mail.jpg` cannot coexist under the ID `mail` - the JPEG becomes unreachable.

PNG is refused: uploading one is rejected with `415 unsupportedMediaType` and nothing is stored.
Renaming `icon.png` to `icon.jpg` does not get it past, because the check reads the file's
contents rather than its name. Convert to JPEG or GIF first - the web UI's LaMetric importer
does that for you.

### Size

An icon is at most **32×8** pixels, and that ceiling does not move with your panel width. Make
JPEG icons 8×8. A GIF keeps its own size up to 32×8, so a GIF as wide as the panel fills the
display and is drawn as a background behind the text rather than beside it.

A GIF with any frame **larger** than 32×8 does not play at all: it is rejected outright, not
cropped. Resize before uploading rather than relying on AWTRIX.

There is no file-size limit, but a large or long animation costs far more memory than a small
one. If icons start failing to load, that is the budget you are against.

### GIF playback

| Behaviour | Detail |
|---|---|
| Looping | Infinite; the loop count in the file is ignored |
| Frame delay | Taken from the GIF. A delay of `0` or less becomes **100 ms** |
| Transparency | Transparent pixels keep whatever the previous frame drew there |

## Use an icon in a payload

The `icon` key is accepted by notifications and pushed apps alike:

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H "Content-Type: application/json" \
  -d '{"text":"Long headline that scrolls","icon":"1234","iconMode":"push"}'
```

Everything about how an icon renders and lays out - the `icon`, `iconMode` and `iconOffsetX`
keys, the 9px text column, the full-width GIF background, and what happens when an icon is
missing or fails to decode - is covered in the
[payload reference → Icon](../reference/payload.md#icon).

## Inline base64 icons

You do not have to upload a file at all. The mode is chosen **purely by the length of the
`icon` string**:

* **64 characters or fewer** → a file ID, resolved as above.
* **more than 64 characters** → the string is decoded as base64 image data, then played as an
  animated GIF or decoded as a JPEG depending on what the bytes turn out to be.

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H "Content-Type: application/json" \
  -d "{\"text\":\"Inline\",\"icon\":\"$(base64 -w0 1234.jpg)\"}"
```

This suits a one-shot notification from a script that has the image to hand and does not want to
leave a file behind. The trade-off is payload size: the image travels with every request.

The threshold cuts both ways, so keep file names short. An icon ID longer than 64 characters is
treated as base64 data, fails to decode, and the page renders without an icon.

## List and delete

```bash
# what is on AWTRIX, and how full is it?
curl "http://<awtrix-ip>/api/v1/files?dir=/ICONS"
```

The response carries a `files` array of `{"name": …, "size": …}` entries plus `usedBytes` and
`totalBytes` for the whole partition. Listing a directory that does not exist returns `200` with
an empty `files` array, not a 404.

```bash
# remove one - note this takes a full path, not an ID
curl -X DELETE "http://<awtrix-ip>/api/v1/files?path=/ICONS/1234.jpg"
```

`DELETE` takes `?path=` (a full path) while `GET`/`POST` take `?dir=` (a directory).

Parameter tables and every status code:
[HTTP reference → Files](../reference/http.md#files).

## Where assets live

AWTRIX creates four directories at boot:

| Directory | Holds | Extension |
|---|---|---|
| `/ICONS` | icons | `.gif`, `.jpg` |
| `/MELODIES` | RTTTL melodies | `.txt` |
| `/PALETTES` | custom palettes | `.txt` |
| `/SCRIPTS` | Berry sources and their stores | `.ax`, `.json` |

```bash
# download an icon back off AWTRIX
curl http://<awtrix-ip>/ICONS/1234.jpg -o 1234.jpg
```

`/ICONS/`, `/MELODIES/` and `/PALETTES/` are served over `GET` in every mode. `/SCRIPTS/*` and
the app-order file `/apploop.json` are served outside provisioning AP mode only; a script's
source can also be read with
[`GET /api/v1/apps/script/{name}`](../reference/http.md#get-apiv1appsscriptname).
Authentication, when configured, applies to these routes like every other route.

Full route details, status codes and MIME mapping:
[HTTP reference → Web UI and static assets](../reference/http.md#web-ui-and-static-assets).

## Storage budget

Icons, melodies, palettes and Berry scripts share whatever flash AWTRIX leaves over. On a
4 MB ESP32 that is **512 KB**, which is tight: a few dozen 8×8 JPEGs, or
considerably fewer animated GIFs. Larger boards get more; see
[Limits → Storage](../reference/limits.md#storage).

`usedBytes` and `totalBytes` from `GET /api/v1/files` are the authoritative numbers, and the web
UI renders them as a storage bar. Nothing enforces the budget for you - an upload that does not
fit fails with `500 internalError` rather than being refused up front.

A [factory reset](../reference/system.md#persistence-and-resets) formats the filesystem and
destroys every asset. `POST /api/v1/settings/reset` does not.

## Security

The file API is confined to the asset folders. A multipart file name containing `..`, an absolute
path, or anything resolving outside `/ICONS`, `/MELODIES` or `/PALETTES` is rejected with
`400 invalidPath`, and read and delete are held to the same allowlist.

!!! warning "Authentication is off until you configure it"
    HTTP Basic auth applies to the file routes like every other route - including in
    provisioning AP mode, where uploads are refused outright with `403`. But no user name is
    configured by default, and until you set one the API is open to anyone who can reach
    AWTRIX. See [Authentication](../reference/http.md#authentication).

## Related

* [Payload reference → Icon](../reference/payload.md#icon) - every icon key, with ranges and defaults
* [HTTP reference → Files](../reference/http.md#files) - the three file routes in full
* [Sounds & melodies](sounds.md) - `/MELODIES` and the RTTTL format
* [Text & colors](text.md) - the 9px column an icon takes from your text
