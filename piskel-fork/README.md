# AWTRIX NG - Piskel icon editor

The **Icon Editor** tab of the AWTRIX NG web UI embeds a Piskel fork in an
`<iframe>`. The fork lives in its own repository and is hosted by the AWTRIX
Hub:

- Fork: **https://github.com/Blueforcer/awtrix-piskel** (Apache-2.0; the fork's
  README lists every modification relative to upstream Piskel)
- Deployment: **https://hub.flows.blueforcer.de/piskel/index.html** - the firmware
  default (`PISKEL_URL_DEFAULT` in `webui/index.html`). The file name is spelled
  out because nothing guarantees that a bare `/piskel/` resolves to the
  directory index, and a miss would frame a 404 instead of the editor.

Piskel is **not** bundled into the firmware. The AWTRIX page - same-origin
with the clock - brokers every save/load over the clock's `/api/v1/files` API;
the editor only draws and exchanges image bytes over `postMessage`.

## The postMessage contract

Namespace `awtrix` on every message. The AWTRIX page validates `event.origin`
against the editor URL; the editor validates `event.source === window.parent`.
Full reference: `docs/guides/icon-editor.md` in this repo, and
`src/js/embed-bridge.js` in the fork.

**Editor → AWTRIX**
- `{ns:'awtrix', type:'ready'}` - sent once the editor has initialised.
- `{ns:'awtrix', type:'save', name, mime:'image/gif', dataBase64}` - save the current sprite.
- `{ns:'awtrix', type:'list'}` - ask for the clock's icon list.
- `{ns:'awtrix', type:'load', name}` - ask for one icon's bytes to edit.
- `{ns:'awtrix', type:'live', mode:'bitmap', w, h, dataBase64}` - mirror one still frame to the matrix. `dataBase64` is `w × h × 3` raw RGB888 bytes, row-major.
- `{ns:'awtrix', type:'live', mode:'gif', mime:'image/gif', dataBase64}` - mirror a looping animation to the matrix.
- `{ns:'awtrix', type:'live-off'}` - stop mirroring / clear the matrix preview.

**AWTRIX → Editor**
- `{ns:'awtrix', type:'theme', theme:'dark'|'light'}`
- `{ns:'awtrix', type:'config', sizes:['8x8','32x8']}`
- `{ns:'awtrix', type:'list-result', files:[{name,size}], usedBytes, totalBytes}`
- `{ns:'awtrix', type:'load-result', name, mime, dataBase64}`
- `{ns:'awtrix', type:'save-result', ok, name, error?}`

The iframe `src` also carries `?theme=<dark|light>&sizes=8x8,32x8` so the
editor paints correctly before the first message arrives. The AWTRIX side
turns `live` into a held, replace-in-place notification named `draw-preview`
(`hold:true, stack:false`) - a `bitmap` becomes one `draw`/`db` blit, a `gif`
travels as an inline `icon` the device loops - and `live-off` (or leaving the
tab) into `DELETE /api/v1/notifications/draw-preview`.

Both payloads are single JSON strings on purpose: a pixel-per-element array
overflows the device's JSON document pool at 32×8 and returns
`413 payloadTooLarge`.

## Testing with the stub (no fork needed)

`stub/index.html` is a dependency-free test double that speaks the same
protocol, to verify the AWTRIX side without the real editor:

1. Build and run the AWTRIX simulator (`pio run -e native_sim`, run the
   binary, open `http://localhost:8080`).
2. Serve this repo: `python -m http.server 8090` from the repo root.
3. In the simulator, set `localStorage.awtrixPiskelUrl =
   'http://localhost:8090/piskel-fork/stub/'` in the browser console and open
   the **Icon Editor** tab.
4. Draw (mouse or touch), name it, **Save to AWTRIX** → it appears in the
   **Icons** tab. **Open…** lists icons on the clock and loads one back. The
   header theme toggle propagates to the stub.
