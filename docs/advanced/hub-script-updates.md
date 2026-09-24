# Hub script updates

The Web UI checks linked scripts on opening Scripts, with two concurrent requests.
An explicit check refreshes the results. Updates always require a click. The Hub
never contacts the display and receives neither local code nor device credentials.
As with other browser CORS requests, the browser's Origin header can contain the
local Web UI address; that address is not sent in the request payload.

## Identity and release contract

Hub installs and NG `.ax` downloads prepend `# @hub <public_id> <sha256>` to the
exact published source. The first line alone is metadata; SHA256 describes all
remaining bytes, including line endings. The name on the device is independent.
This marker records origin, not a signature or an ownership claim. Local code
changes retain the marker and are detected by a different hash.

`GET https://awtrix.de/api/v1/scripts/{id}/release` returns
`{id,name,revision,sha256,notes}`. `GET .../source` returns the exact raw source and
requires `Authorization: Bearer <Hub connection key>`. Both endpoints serve public NG
contributions only, return 404 for unavailable content, use no-store caching, and support
CORS without cookies. A source hash
mismatch aborts installation if a publisher changed the release during download.
Only code changes increment the release number; descriptions and cover images do not.

## Device operation

`PUT /api/v1/apps/script-update/{name}` accepts JSON:
`{"expected_source":"complete current local source","source":"complete new source"}`.
The normal device authentication applies. The command is serialized with edits:
404 means missing, 409 means changed since inspection, 422 means invalid source
or a failed installation with diagnostic text, 503 means scripting unavailable,
and 507 means insufficient capacity. Success is 200 `{"ok":true}`.
Capabilities expose `scriptUpdates: true` when scripting is compiled in. Galactic
Unicorn builds expose `scripting: false` and `scriptUpdates: false`; script routes
return HTTP 503 `unavailable`. Old firmware must be upgraded first.
For a separate copy, `expected_source: null` creates only a missing script; an
existing name returns 409 and remains unchanged. Failed new installs are removed.
The request uses the dynamically bounded source arena; the combined old/new JSON
must fit the device's current memory budget. There is no unsafe legacy fallback.

The previous source remains on disk until the replacement loads. A compile/setup
error reinstalls the old source with its previous settings. Compatible settings
are kept and new defaults are seeded by the existing settings engine. Persisting
source on ESP32 uses a verified temporary file followed by rename. Settings writes
retain their existing deferred persistence semantics; this is not a power-loss
transaction across code, script stores, icons and external script side effects.
Errors that first appear in later timers/network callbacks cannot be detected at
installation time. No release history or permanent undo archive is kept.

## Icons and local work

All declared `@icons` are installed before replacing code, requiring the existing
Hub connection key. Existing differing icons stop the update: an update must not
silently change a shared or locally modified icon. Resolve it explicitly in Icons
and try again. Successfully added icons can remain after a later failure.

Locally modified scripts can install a separate Hub copy. The current editor draft
is retained; settings of a new copy start from its defaults. Renaming a linked
script preserves the embedded identity. Deleting a Hub post leaves local scripts
running. Older unlinked scripts stay unlinked; reimport a fresh Hub download to
establish identity. Never guess identity from a filename.

## Validation plan

- Native router, settings and script-host tests, including conflict and rollback.
- Device HTTP test for updates, persistence, preserved settings and failed loads.
- Web UI tests for origin parsing, local edits, source races and credential isolation.
- Hub feature tests for public-only release access, code revisions, CORS and downloads.
- Hub build and firmware build; visual inspection of the Web UI update controls.
