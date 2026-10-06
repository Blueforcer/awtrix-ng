# Host tests

The host tests check the host services that `awtrix-linux` is built on: file storage,
persistence, name resolution and button input. They live in
[`tests/host/`](https://github.com/Blueforcer/awtrix-ng/tree/main/tests/host) and test the code in
[`src/platform/linux/host/`](https://github.com/Blueforcer/awtrix-ng/tree/main/src/platform/linux/host). They
use real temporary files and localhost DNS, and need no device, MQTT broker, public network,
vendor SDK or external test framework.

The repository has other test suites too; [the overview](index.md#tests) lists them all.

## Running them

The project CMake build includes these tests, so `ctest --preset host` runs them with everything
else (see [Building from source](building.md#awtrix-linux-with-cmake)). They also build on their
own:

```text
cmake -S tests/host -B .cache/platform-host-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .cache/platform-host-tests
ctest --test-dir .cache/platform-host-tests --output-on-failure
```

On Linux they build with ASan and UBSan (the address and undefined-behaviour sanitizers). These
catch, among other things, stale memory access during delayed resolver destruction.
`-DAWTRIX_SANITIZE=OFF` turns them off.

| CTest name | Source | Built |
|---|---|---|
| `host-store` | `test_store.cpp` | always |
| `host-resolver` | `test_resolver.cpp` | always |
| `host-persistence` | `test_persistence.cpp` | always |
| `host-buttons` | `test_host_buttons.cpp` | only inside the project build, which provides the core library |

## What they cover

### Storage (`host-store`)

- Binary and empty files, bounded reads.
- Path containment and traversal attempts; symlinks where the system allows them.
- Quota failures that keep the existing content.
- The filesystem reserve: uploads and script sources stay out of it, settings may still use it.
- Script state may use at most three quarters of the reserve and at most 64 KiB per script.
- Atomic replacement while readers are running.
- A failed script-state write stays buffered until a later flush succeeds. With a reserve, that
  flush waits for more free space or a smaller store.
- The trusted command-line asset reader, separately from data-directory access.

The device and host use `fs::usage`, `fs::openRead`, `fs::fileSize` and `fs::isFile` for asset
access. The host maps device paths into its configured data directory and rejects symlinks and
escaping paths. The shared `ScriptStore<Files>` implements `script::IScriptFiles`; its file
backend supplies filesystem operations and the host's upload reserve and retry state. Stored
script filenames allow 1–64 bytes, with no slash, backslash, colon, NUL or `..`. This storage
rule also applies when loading files already on disk; API script names follow their own narrower
validation. Removing a script removes its source and state while retaining its sound files.

### Resolver (`host-resolver`)

Literal addresses, cache identity, cancellation, invalid input and non-blocking destruction with
work still pending. The test waits for delayed workers before it exits, so the sanitizers can see
stale memory access.

### Persistence (`host-persistence`)

The test forces real directory and disk-space errors and checks that:

- each failed document keeps its latest value for a retry;
- a successful retry clears only its own pending status;
- a retry keeps the old content on disk until it succeeds;
- credentials never reach the persistence log.

A failed save of settings, configuration, app order or radio stations stays visible through
`host::persistence::pending` / `hasPending`. `flushPending` has to run periodically and succeed
before a clean shutdown is reported. This retry state lives in memory; it is not a journal that
survives a crash.

### Buttons (`host-buttons`)

`HostButtonInput` against the real engine: actions queue until the engine ticks, several events
in one frame each count, press and release edges stay in order, left/right/select map to the shared
navigation commands (including rotation and swapped buttons), `blockNavigation`, the 300 ms
double press that toggles the display, and a script that consumes a button before any default
action.

## Limits of what is tested

The data root is set at startup. Path containment assumes the service owner controls that root and
its parent directories; it is not a boundary against another local user who can replace those
directories while the service runs. Atomic replacement of one file does not make several files one
transactional snapshot. Disk errors are reported to the caller; behaviour on power loss depends on
the real storage and is not covered here.

## Related

- [Building from source](building.md)
- [AWTRIX on Linux](site:tc002/developers/linux/)
