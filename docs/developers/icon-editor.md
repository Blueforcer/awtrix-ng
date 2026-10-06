# Icon editor (Piskel)

The **Icon Editor** tab of the web UI embeds a fork of the Piskel pixel editor in an `<iframe>`.
Piskel is not bundled into the firmware. The fork lives in its own repository and the AWTRIX Hub
hosts it:

- Fork: **https://github.com/Blueforcer/awtrix-piskel** (Apache-2.0; its README lists every change
  against upstream Piskel). The editor side of the protocol is `src/js/embed-bridge.js` there.
- Deployment: **https://awtrix.de/piskel/index.html**, the default `PISKEL_URL_DEFAULT` in
  [`webui/index.html`](https://github.com/Blueforcer/awtrix-ng/blob/main/webui/index.html). The
  file name is spelled out because a bare `/piskel/` is not guaranteed to resolve to the directory
  index, and a miss would frame a 404 page instead of the editor.

This repository holds the AWTRIX side of the bridge (in `webui/index.html`) and a test stub in
[`piskel-fork/stub/`](https://github.com/Blueforcer/awtrix-ng/tree/main/piskel-fork/stub). The
user view is in [Icon editor](../guides/icon-editor.md).

## How it works

The AWTRIX page is same-origin with the clock. It does every save and load through the clock's
`/api/v1/files` API. The editor only draws and exchanges image bytes with the page over
`postMessage`.

Every message carries the namespace `ns:'awtrix'`. The AWTRIX page checks `event.origin` against
the editor URL and `event.source === iframe.contentWindow`. The editor checks
`event.source === window.parent`.

The iframe `src` carries `?theme=<dark|light>&sizes=8x8,32x8`, so the editor paints correctly
before the first message arrives.

### Editor to AWTRIX

| Message | Meaning |
|---|---|
| `{type:'ready'}` | The editor has initialised. |
| `{type:'save', name, mime:'image/gif', dataBase64, origin?}` | Save the current sprite on the clock, without publishing. |
| `{type:'publish', requestId, name, mime:'image/gif', dataBase64, based_on?}` | Publish to the Hub through the parent, when `publishViaParent` is on. |
| `{type:'list'}` | Ask for the clock's icon list. |
| `{type:'load', name}` | Ask for one icon's bytes to edit. |
| `{type:'live', mode:'bitmap', w, h, dataBase64}` | Mirror one still frame to the matrix. `dataBase64` is `w × h × 3` raw RGB888 bytes, row by row. |
| `{type:'live', mode:'gif', mime:'image/gif', dataBase64}` | Mirror a looping animation to the matrix. |
| `{type:'live-off'}` | Stop mirroring and clear the matrix preview. |

### AWTRIX to editor

| Message | Meaning |
|---|---|
| `{type:'theme', theme}` | The web UI theme changed; `theme` is `'dark'` or `'light'`. |
| `{type:'config', sizes:['8x8','32x8'], publishViaParent:true}` | Canvas sizes on offer; publishing goes through the parent. |
| `{type:'list-result', files:[{name,size}], usedBytes, totalBytes}` | The icon list. |
| `{type:'load-result', name, mime, dataBase64, origin?, based_on?}` | One icon's bytes. |
| `{type:'save-result', ok, name, error?}` | Result of a save. |
| `{type:'publish-result', requestId, ok, status?, slug?, pr?, origin?, error?, message?}` | Result of a publication. |

### Publishing and origin

The parent page keeps the Hub token; the editor never receives it. The parent sends publication
requests with `response=resolve`. A successful result is `created` or `existing`; neither a local
save nor an `existing` result creates another publication. Publishing needs a descriptive name
containing a letter. Numeric LaMetric file names stay valid for local saves. When the editor is
opened on the Hub itself, it publishes directly with its same-origin session.

`origin` is `{hub, slug, sha256}`, with the hash of the local bytes at the time the icon was linked.
The parent stores it with the local file name through `/api/v1/icons/origins`. Later saves keep
the reference, so the gallery can recognise local changes. Creating or importing another drawing
clears the reference; editing and Undo keep it.

### Live preview

The AWTRIX page turns `live` into a held notification named `draw-preview` that replaces itself in
place (`hold:true, stack:false`). A `bitmap` becomes one `draw` / `db` blit; a `gif` travels as an
inline `icon` the device loops. `live-off`, or leaving the tab, sends
`DELETE /api/v1/notifications/draw-preview`.

Both payloads are single base64 strings on purpose. An array with one element per pixel overflows
the device's JSON document pool at 32×8 and returns `413 payloadTooLarge`.

## Testing with the stub

[`piskel-fork/stub/index.html`](https://github.com/Blueforcer/awtrix-ng/blob/main/piskel-fork/stub/index.html)
is a test double without dependencies that speaks the same protocol. It checks the AWTRIX side
without the real editor:

1. Open the web UI of an AWTRIX NG: a clock on your network, or `awtrix-linux` from the CMake
   build (see [Building from source](building.md#awtrix-linux-with-cmake)) at
   `http://localhost:8080`.
2. Serve the repository: `python -m http.server 8090` from the repository root.
3. In that web UI, run `localStorage.awtrixPiskelUrl = 'http://localhost:8090/piskel-fork/stub/'`
   in the browser console and open the **Icon Editor** tab.
4. Draw (mouse or touch), name it and click **Save to AWTRIX**; it appears in the **Icons** tab.
   **Open…** lists the icons on the clock and loads one back. The header theme toggle reaches the
   stub.

Remove the `awtrixPiskelUrl` entry afterwards to return to the hosted editor.

## Related

- [Icon editor](../guides/icon-editor.md) for users
- [Icons](../guides/icons.md)
- [HTTP API reference](../reference/http.md)
