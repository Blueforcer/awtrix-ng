# Device test scripts

A Berry test suite that runs on a real device and is driven entirely over the REST API - no web UI,
no serial, nothing to look at on the panel. It covers the parts of the scripting subsystem the
native tests cannot reach: `@config` on apps and on modules, `@module` imports, `@headless`,
`should_show()` / `duration()`, script HTTP, error latching, and what actually lands in the
framebuffer.

```bash
python tools/devicetest/run.py --host awtrix-ng.local
```

| Flag | Meaning |
|---|---|
| `--host` | device IP or hostname; defaults to `$AWTRIX`, then `awtrix-ng.local` |
| `--auth user:pass` | for a device with a login |
| `--keep` | leave the scripts installed afterwards |
| `--slow` | also run the rotation test (~5 min: it waits for real turns) |
| `--only a,b` | run named tests only - `inventory schema patch reject module headless http warnings errors pixels rotation` |

Every run deletes each test script before re-installing it, so the saved store goes with it and the
run starts on the declared defaults whether or not the last one used `--keep`. Exit code is the
number of failures, capped at 1.

## The scripts

| File | Installs as | What it is there to prove |
|---|---|---|
| `tfmt.ax` | module `tfmt` | a module with settings of its own; helpers other apps import |
| `tcfg.ax` | app | all six `@config` types, each echoed into `shared` so a patch can be followed all the way into the running script |
| `tbg.ax` | app | `@headless true`: no `draw()`, never a turn, `loop()` still runs |
| `tuse.ax` | app | `import tfmt` - module code and module settings reaching an app |
| `thttp.ax` | app | `http.get()` with `find`/`keep` against the device's own `/api/v1/device` |
| `twarn.ax` | app | eight broken `@config` lines, one per failure mode |
| `tapi.ax` | app | `sensor.*`, `settings.get()`, clock and canvas answers into `shared` |
| `tskip.ax` | app | `should_show()` and `duration()` driven by its own settings |

The apps are readable on the panel while the suite runs, but nothing in the run depends on that -
every assertion reads the API: `/api/v1/apps`, `/api/v1/apps/{name}/config`,
`/api/v1/scripts/shared` and `/api/v1/display/screen`.

## Filling the device up

Judging the Scripts tab needs a device with more than a handful of files on it. Two ways, both
independent of the test run:

`demo/` holds eight readable apps and modules - a long file name, a module whose import name differs
from its file name, one deliberately broken script for the red row, and a few with settings. Install
them by hand:

```bash
cd tools/devicetest/demo
for f in dutil fmt-with-a-very-long-name dclock dbars dfx dnews demo-long-script-name-for-ui dbroken; do
  curl -X PUT "http://awtrix-ng.local/api/v1/apps/script/$f" \
    -H 'Content-Type: text/plain' --data-binary "@$f.ax"
done
```

`fill.py` generates throwaway files whose content is meaningless on purpose - one `draw()` with a
digit per script, one number per module. It raises `scriptLimit` to 32 first (the firmware maximum)
and stops on the first refusal:

```bash
python tools/devicetest/fill.py --host awtrix-ng.local --scripts 10 --modules 6
python tools/devicetest/fill.py --host awtrix-ng.local --clean
```

`--clean` removes every `fill*` file and leaves the test and demo scripts alone. A full device (32
files) leaves about 35 KB of heap and a largest block near 23 KB - below what a TLS handshake needs,
and low enough that an OTA upload can fail, so clear the fillers before flashing.

## Notes

- The suite writes only to its own `t*` scripts. It never touches device settings, and the only
  rotation change is the one installing an app makes by itself.
- `fill.py` is the exception: it raises `scriptLimit`, and does not lower it again.
- `thttp` is pointed at `--host` by the runner, so the script fetches the device it runs on.
- Eight scripts cost about 29 KB of the Berry heap on an ESP32 without PSRAM. A device already
  carrying scripts may answer `507` on install; the run stops there and says so.
