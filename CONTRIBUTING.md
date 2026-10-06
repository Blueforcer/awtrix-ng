# Contributing to AWTRIX NG

Thanks for being here. Issues and pull requests are both welcome, and most changes
need no hardware: the portable core and its tests run on your computer.

By contributing you agree that your contribution is licensed under the project's
[PolyForm Noncommercial License 1.0.0](LICENSE.md), and you additionally grant
Stephan Mühl (Blueforcer) a non-exclusive, worldwide, perpetual, irrevocable and
transferable right to use, modify and relicense your contribution under other
terms, including commercial ones. You keep the copyright in what you wrote.

Why: the project offers commercial licenses on request, and a license it cannot
grant over every line of the code is worth nothing. Without this you would have
to be asked individually for every such request, forever.

## What is most useful right now

In rough order of value:

1. **On-device test reports.** AWTRIX 2 conversions and DIY panels, a real MQTT
   broker, a real Home Assistant instance — the boards and setups development
   does not run on every day. Say what you ran, what you expected and what
   happened.
2. **Berry scripts worth shipping as examples.**
3. **Bug reports with a reproduction** — a `curl` command beats a description.
4. **Documentation fixes.** Every page is meant to describe what the code
   actually does; a page that promises something the firmware does not do is a
   bug, not a wish.

## Setting up

You need [PlatformIO](https://platformio.org/), Python 3.12+, CMake 3.20+, Ninja
and a GCC C/C++ toolchain:

```bash
pip install -U platformio
```

Node 22+ is needed for a firmware build (the web UI is minified through `npx`
before it is embedded) and for the web UI tests.

## ESP32 firmware and host tests

```bash
pio run  -e awtrix        # ESP32 firmware (stock pin defaults)
pio run  -e awtrix_s3_octal     # ESP32-S3 firmware, octal PSRAM
pio run  -e awtrix_s3_quad      # ESP32-S3 firmware, quad PSRAM
python scripts/test_native.py  # host unit tests for the portable core
```

The host tests are the fastest loop: `python scripts/test_native.py` builds and runs
them in seconds. Linux and TC002 use the shared CMake build; generic headless
Linux is a developer target. See [building from source](docs/developers/building.md)
for Linux dependencies and tests, and the [TC002 developer guide](docs/developers/tc002/index.md)
for its runtime, supervisor and release tools.

## What CI gates on

Every push runs, and your PR needs all of it green:

```bash
python scripts/test_native.py          # host unit tests
pio run -e awtrix                      # ESP32 firmware images
pio run -e awtrix_s3_octal
pio run -e awtrix_s3_quad
python tools/check_docs_sync.py        # docs match the firmware's real fields
python tools/check_berry_api.py        # editor's Berry API table is current
python tools/gen_agent_skill.py --check
python tools/check_prelude_solidified.py
python tools/check_font_sync.py
python tools/check_partitions.py
python tools/docs/build_site.py       # documentation, one build per clock
node --test "tools/flowconv/*.test.mjs"
cd webui/test && npm ci && npm test
```

CI also configures and builds the Linux targets, runs their CTest suite and
checks service isolation. Use the commands in the
[developer build guide](docs/developers/building.md) on a Linux host.

### Documentation per clock

The site is built once per clock (`esp32`, `esp32-s3`, `tc002`) from one source
tree, and each build shows only what that firmware can do. Mark what belongs to
which clock:

* a whole page: front matter `only: [tc002]`. Pages without it are in all three.
* a passage, table rows or a few words: `<!-- only esp32 esp32-s3 -->` …
  `<!-- /only -->`, on lines of their own or inline; blocks may nest.
* the OpenAPI file: `x-only: [tc002]` on an operation, parameter, property or
  enum entry.

Write each passage as if the page were only about that clock: no "TC002 only"
or "(ESP32-S3)" asides. `tools/docs/build_site.py` fails on text that names
another clock; real exceptions go into `tools/docs/lint-allow.txt`. Preview one
clock with `AWTRIX_DOCS_VARIANT=tc002 mkdocs serve`.

### Generated files you may have to regenerate

Several checked-in files are produced by generators that **no normal build
runs**, which is exactly why the checks above exist — a stale one fails
silently on the device instead of loudly at build time.

| If you changed… | Regenerate with |
|---|---|
| `src/core/script/Prelude.h`, or a binding name in `ScriptBindings.cpp` | `python scripts/gen_prelude_solidified.py` |
| a legacy BDF directly under `assets/fonts/` | `python scripts/gen_font.py` |
| pinned original catalog under `assets/fonts/matrix-fonts/` | update verified provenance, then `python scripts/gen_matrix_fonts.py`; keep originals unchanged |
| `webui/index.html`, or added a Berry binding | any `pio run -e awtrix*` (the pre-script regenerates the embedded asset and the editor's API table in place) |
| `docs/examples/berry-app-system-prompt.md` | `python tools/gen_agent_skill.py` |
| `scripts/gen_partitions.py` | nothing — `check_partitions.py` regenerates and validates every table |

Commit the regenerated file together with the change that caused it.

## Code style

There is no formatter config, on purpose: the tree is consistent and a
reformatting commit would bury real changes. **Match the surrounding code.**

The load-bearing rules:

- **`src/core/` stays portable.** No Arduino, no FastLED, no board headers, no
  `#ifdef` on a chip. That is what makes it unit-testable on the host, and CI's
  `native` build is what enforces it. Hardware belongs behind the seams in
  `src/hal/` and `src/system/`.
- **Comment what the code cannot say itself.** Names and small functions still
  carry most of the explanation, so don't restate a signature in words. Leave a
  short `//` note above a function or a dense block when a reader would
  otherwise have to reverse-engineer the intent — an invariant, a unit, a wire
  format, or the hardware quirk being worked around. One line is usually enough,
  two is the limit. No Doxygen, no banners, no commented-out code. The reasoning
  behind a *change* still belongs in its commit message and pull request, where
  it stays readable and dated.
- **New behaviour comes with an appropriate host test** in `test/` (portable
  core) or `tests/` (Linux and TC002 contracts), unless it cannot run off-device.
- **The docs are part of the change.** A new field, endpoint or setting lands in
  `docs/` in the same PR — `check_docs_sync.py` will tell you if you forgot.

## Commits and pull requests

Use [Conventional Commits](https://www.conventionalcommits.org/) — one logical
change per commit, present tense, and say *why* in the body when the *what* is
not self-evident:

```
fix(mqtt): publish state as soon as power or an indicator changes
feat(http): reject bodies over 8 KB with 413 instead of a bogus 400
docs(reference): correct the default scroll hold
```

Common scopes: `http`, `mqtt`, `render`, `script`, `webui`, `audio`, `device`,
`docs`, `ci`, `build`.

For the PR itself: describe what changed and how you verified it. If you tested
on hardware, say which board. If you only ran the host tests, say that too — it
is useful information, not an admission.

## Reporting security issues

Do not open a public issue. See [SECURITY.md](SECURITY.md).
