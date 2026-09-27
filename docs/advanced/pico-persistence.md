# Pico persistence

The Galactic Unicorn builds reserve 512 KiB of LittleFS. Startup mounts it before
loading device config, settings and app order. Like the ESP32 format-on-failed-mount
policy, an unmountable volume is formatted (not just a blank first boot). A corrupt
filesystem can therefore lose all its files; keep backups. Directories `/ICONS`,
`/PALETTES`, `/MELODIES`, `/SCRIPTS` and `/NVS` are created at mount time.

Device config (including WiFi credentials) and settings use the existing persistence
interfaces. `platform/Preferences.h` selects ESP32 NVS or the Pico adapter. The Pico
adapter implements only the Preferences subset AWTRIX uses: begin/end, strings,
bool, signed/unsigned 32-bit values, float32, uint8/uint16, isKey/remove/clear.
One namespace is an atomic transaction committed at **end()**, not at each put.
Call end explicitly. Unchanged values do not cause a write. A failed commit is
logged without printing keys or values; the existing void save APIs cannot report
it to the caller. All access must remain on core 0, not in interrupts or core 1.

Snapshots live at `/NVS/<namespace>.bin`, with a 16 KiB limit. The portable AXKV v1
format is eight header bytes (`AXKV`, version 1, three zero bytes), followed by
records (one-byte key length, one-byte type, two-byte little-endian payload length,
key bytes, payload bytes), followed by a little-endian CRC32 of all preceding bytes.
Numeric payloads are little endian; float payloads contain IEEE-754 binary32 bits.
Keys and namespaces are 1–15 ASCII letters/digits/underscore/hyphen. Unknown types,
duplicate keys, malformed lengths, invalid names, truncation, unsupported headers
and checksum failures reject the whole namespace: callers retain defaults. Corrupt
snapshots are not silently overwritten. Reset settings/factory reset can remove them.
Credentials are plaintext, just as ordinary unencrypted NVS is; do not publish a dump.

Writes stage a sibling `.tmp`, flush/close, verify its bytes, then rename over the
previous snapshot. Leftover temporary files are ignored at boot. LittleFS's atomic
rename preserves the old snapshot on interrupted publication. Host tests exercise
round trips through a fresh store instance, read-only/type defaults, namespace
isolation, corruption and failed replacement; physical power-cut testing still
requires a board.

An adapter-level host test also runs the production DeviceConfig, NvsSettings,
LittleFS adapter and restore sink against an explicitly simulated filesystem. It
checks credentials, numeric types, non-default settings, height, short writes,
rename failures and aborted asset restores. On all platforms, `panelHeight` uses
the fresh on-flash key `pheight`. The legacy per-tile height key `ph` is never read
or migrated and is deleted by `DeviceConfig::save()`. A store with only `ph` keeps
the default runtime height of 8; `pheight` round-trips independently, including 11.
The JSON/API and backup/restore field remains `panelHeight`, not the on-flash key.

App order uses the shared `/apploop.json` store. Scripts use the shared ScriptStore under
`/SCRIPTS`; RadioStore is a no-op implementation on Pico (radio remains unavailable). The shared
`LittleFsRestoreSink` stages asset entries and renames complete files, so aborting
an entry does not delete an existing asset. `FsRestoreSink`, config JSON and the
portable backup parser compile for Pico. They are reached through the existing
backup and restore routes of the HTTP API; there is no new endpoint or backup
format.

Settings changes are coalesced over a 1.5 second save interval. Abrupt power loss
inside that window can lose the most recent changes. On a Pico W, settings, Wi-Fi
credentials and scripts survive reboots and reflashing an application UF2; a power
cut in the middle of a write has not been tested on hardware.
