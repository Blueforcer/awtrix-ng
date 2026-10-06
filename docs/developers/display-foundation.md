# Display foundation

The display foundation shares panel geometry, frame buffers, clipping and fonts between the ESP32
firmware and `awtrix-linux`. Prepared layouts and their memory budget belong to Linux targets
with a display more than 8 pixels high. The code lives in
[`src/core/render/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/core/render),
[`src/platform/linux/layout/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/linux/layout) and
[`src/media/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/media). The public contract
and examples for layouts are in [Layouts](site:tc002/guides/layouts/); this page explains how the code
is divided and how it behaves when things fail.

## Who owns what

| Responsibility | Owner |
|---|---|
| Legacy JSON normalisation and drawing | `core/payload/PayloadParser`, the legacy branches in `core/render/RenderPipeline` |
| Registered payload fields and prepared content | `core/payload/PayloadParser`, `core/render/PageContent` |
| Native layout JSON validation | `platform/linux/layout/LayoutJson`, `LayoutPayload` |
| Metrics, prepared regions and independent scrollers | `platform/linux/layout/Layout` |
| Checked data and shared ownership | `core/memory/CheckedStorage`, `CheckedShared` |
| Pixel clipping and font rendering | `core/render/Canvas`, `TextRenderer`, `FontCatalog` |
| Script ownership and handle limits | `platform/linux/layout/LayoutScripting`, `ScriptLayouts`, `ScriptLayoutBindings` |
| Rotation, notifications, transitions and global overlays | `CoreEngine`, `RenderPipeline` |
| Image metadata and bounded decoders | `media/ImageInfo`, `DevicePageIcon`, `GifPlayer` |
| Active geometry, frame allocation and electrical output | `core/render/DisplayProfile`, `FrameMemory`, the board and renderer adapters |

There is one notification queue and no widget tree. A prepared layout is a flat list of regions
in draw order. Ordinary frames do not parse JSON and do not copy the whole display per region.
Canvas views borrow their parent's buffer and clip in both axes. Frame composition owns the
transition and power-animation buffers.

Legacy payloads keep their coordinates, baselines, icon pushes, chart scales and scroll rules.
The `small` and `large` fonts keep their original tables and IDs; the ten Matrix fonts come from
a separate generator with stable names (see [Fonts](fonts.md)). Every display size uses native
coordinates. Extra space on a larger panel is used only when the sender uses layout regions or
writes code that reads the display size.

## Admission and memory

A native layout is admitted on the main loop. Admission validates the metadata and opens its
media with bounded decoders before the new layout replaces the old one. An invalid update keeps
the page that was shown before.

Pushed pages, notifications and script handles share one layout budget. It includes both pages
of a transition, so replacing a layout briefly needs room for the old and the new one. The budget
is 256 KiB on the TC002; `GET /api/v1/capabilities` reports it
under `layouts.limits`.

The renderer owns each layout's animation state. Completion reports carry a revision, so a report
for a replaced layout is not mistaken for the current one. Notifications keep their own
generation counter.

Large buffers and layout storage have explicit allocation-failure paths. Older subsystems and the
network transports still use ordinary STL allocations, so there is no global guarantee that every
allocation failure is handled.

## Geometry

| Target | Width | Height | Pixel limit |
|---|---|---|---|
| ESP32 / ESP32-S3 | 32–128 | 8 | 1,024 |
| Ulanzi TC002 | 52 | 16 | 832 |
| Linux host | 8–128 | 8–32 | 4,096 |

The limits are `kEspDisplayLimits` in
[`MatrixLayout.h`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/core/render/MatrixLayout.h)
and `hostDisplayLimits` in
[`DisplayProfile.h`](https://github.com/Blueforcer/awtrix-ng/blob/main/src/core/render/DisplayProfile.h).
On the ESP32 a wider display is a horizontal chain of 8-pixel-high panels, driven from one data pin.
NVS erases the key `pheight` that older builds stored for a panel height.

A new width becomes active at the next restart. Until then scripts, the screen API, assets and the
editors keep using the active size, and the configuration API shows the pending one.

Before the display starts, the firmware checks that the frame and output buffers can be
allocated. If they cannot, it falls back to a smaller checked geometry and keeps the configured
value visible instead of rewriting it. On the ESP32 an NVS (non-volatile storage) write counts only
after a successful commit and read-back; a failed write is reported through the configuration
error contract.

## Frame rate

More pixels take longer to send over the single data line. These frame rates were measured with the
`awtrix_s3_octal_probe` build on an ESP32-S3 with 8 MiB PSRAM:

| Geometry | Blank | Two scrollers | Plasma | Mean LED output per frame |
|---|---:|---:|---:|---:|
| 32×8 | 41.6 fps | 41.7 fps | 41.4 fps | 8.1 ms |
| 64×8 | 40.7 fps | 40.7 fps | 40.6 fps | 15.9 ms |
| 128×8 | 27.6 fps | 26.8 fps | 25.4 fps | 31.6 ms |

The frame rate is capped at about 40 fps. Up to 512 pixels the cap holds; above that the LED output
time dominates. The figures describe a device with its network and services running, not an
isolated render loop. They time the electrical output, not an optical measurement of a physical
panel of that size.

The plasma effect caches its axis values for every height and falls back to per-pixel
calculation only if that small cache cannot be allocated.

## Testing

```sh
python scripts/test_native.py -j 8
python tools/check_font_sync.py
python tools/check_docs_sync.py
python tools/check_berry_api.py
python tools/check_prelude_solidified.py
pio run -e awtrix -e awtrix_s3_octal -e awtrix_s3_quad
```

The native suites in [`test/`](https://github.com/Blueforcer/awtrix-ng/tree/main/test) cover
plain payloads, panel mapping, clipping, fonts, checked storage and rejection of unregistered
payload fields. The CMake suites in
[`tests/layout/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/layout) cover prepared
regions, independent scrollers, revisions, atomic replacement, handle cleanup and injected
allocation failures. Layout registration is checked at heights 8, 9 and 16.

The CMake host build runs the HTTP contracts. They compare JSON and Berry output, check that a
rejected update keeps the displayed content, and send native notifications over MQTT. Configure
with `-DAWTRIX_REQUIRE_INTEGRATION_SERVICES=ON` to make a missing MQTT broker a failure instead of
a skip. See [Building from source](building.md#awtrix-linux-with-cmake).

## Related

- [Layouts](site:tc002/guides/layouts/) for the public contract
- [System configuration](../reference/system.md) for the panel settings
- [Fonts](fonts.md)
