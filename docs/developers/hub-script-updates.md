# Hub script updates

Scripts installed from the AWTRIX Hub (awtrix.de) remember where they came from, and the web UI
offers an update when the Hub publishes a newer release. Three parts work together: the Scripts
tab in [`webui/index.html`](https://github.com/Blueforcer/awtrix-ng/blob/main/webui/index.html),
the Hub's release endpoints, and the device route `PUT /api/v1/apps/script-update/{name}`
(routed in
[`src/core/api/ApiRouter.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/core/api/ApiRouter.cpp),
executed as `ScriptUpdate` in
[`src/core/Dispatcher.cpp`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/core/Dispatcher.cpp)).
The user view is in [Hub script updates](../guides/hub-script-updates.md).

## How it works

The browser does all the talking. It asks the Hub for release information and fetches the new
source, then hands old and new source to the device. The Hub never contacts the device and never
receives local code or device credentials. Like any browser CORS (cross-origin) request, the
`Origin` header can carry the local web UI address; the request body does not.

The web UI checks linked scripts when the Scripts tab opens, with at most two requests at a time.
An explicit check refreshes the results. An update always needs a click.

### Identity marker

Hub installs and NG `.ax` downloads put one line in front of the exact published source:

```text
# @hub <public_id> <sha256>
```

`public_id` is 12 letters and digits. A Hub entry can offer several variants of a script, for
example one per panel size; each variant has its own `public_id`, and the first one shares the id
of the entry. The SHA-256 covers every byte after that first line, including line endings. The
script's name on the device is independent of the marker.

The marker records origin. It is not a signature and not an ownership claim. A local edit keeps
the marker and is detected because the hash no longer matches. Renaming a linked script keeps the
marker. Scripts without a marker stay unlinked; the web UI never guesses an identity from a file
name. Re-importing a fresh Hub download links an older script.

### Hub endpoints

| Request | Answer |
|---|---|
| `GET https://awtrix.de/api/v1/scripts/{id}/release` | `{id, name, revision, sha256, notes}` |
| `GET https://awtrix.de/api/v1/scripts/{id}/source` | the exact raw source; needs `Authorization: Bearer <Hub connection key>` |

Both serve public NG contributions only, answer 404 for anything unavailable, send `no-store`
caching headers, and allow CORS without cookies. Only code changes increase the revision;
descriptions and cover images do not. If the downloaded source does not match the release hash
(the publisher changed it mid-download), the installation stops.

### Device side

The route, its request body and its status codes are documented in the
[HTTP API reference](../reference/http.md). The device advertises it with
`capabilities.scriptUpdates: true`; older firmware has to be updated first, and there is no
fallback to a plain script upload.

Internally:

- The command runs through the same queue as script edits, so an update and an edit never
  interleave. `expected_source` is compared with the stored source; a mismatch answers 409.
- `expected_source: null` creates a separate copy only when no script of that name exists. A new
  install that fails is removed again.
- The request is read into the script source arena, which is sized from the request's
  `Content-Length`. Old and new source together must fit the device's current memory budget.
- The old source stays on disk until the new one loads. If the new script fails to compile or its
  `setup()` fails, the old source is reinstalled with its previous settings.
- Settings that still fit are kept; new settings start from their declared defaults.
- On the ESP32 the source is written to a temporary file, verified and renamed into place.
  Settings keep their usual deferred write. An update is therefore not a power-loss-safe
  transaction across code, script stores, icons and anything the script did outside the device.
- Errors that first appear later, in timers or network callbacks, cannot be caught at install
  time. There is no release history and no undo archive.

### Icons

The web UI installs every icon the new release declares in `@icons` before it replaces the code,
using the Hub connection key. If an icon of the same name already exists with different content,
the update stops: an update must not silently replace a shared or locally edited icon. The user
resolves it in the Icons tab and tries again. Icons added before a later failure stay.

### Local edits

A locally changed script can install the Hub release as a separate copy. The editor keeps the
current draft; the copy's settings start from its defaults. Deleting a Hub post leaves installed
scripts running.

## Tests

| Where | What |
|---|---|
| [`test/test_apirouter`](https://github.com/Blueforcer/awtrix-ng/tree/main/test/test_apirouter) | Routing and validation of the update route |
| [`tests/linux/test_contract.py`](https://github.com/Blueforcer/awtrix-ng/blob/main/tests/linux/test_contract.py) | Updates over HTTP against `awtrix-linux`: persistence, kept settings, conflicts and failed loads |
| [`webui/test/hub-script-updates.test.js`](https://github.com/Blueforcer/awtrix-ng/blob/main/webui/test/hub-script-updates.test.js) | Marker parsing, local edits, the token gate, copies, conflicts, an in-flight draft and network failures |

Run them with `python scripts/test_native.py -R apirouter`, `ctest --preset host` and `npm test` in
`webui/test`. See [Building from source](building.md).

## Related

- [Hub script updates](../guides/hub-script-updates.md) for users
- [HTTP API reference](../reference/http.md)
- [Scripting guide](../guides/scripting/index.md)
