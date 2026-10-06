# Web UI sources

Edit the files in `src/`. `src/manifest.json` lists their assembly order: the HTML
shell and stylesheet, shared helpers, settings and the individual tabs. The
JavaScript pieces share the shell's closure, so function declarations can refer
to other tabs without adding browser module requests.

`index.html` is generated and committed. Run `python scripts/webui_source.py`
from the repository root to refresh it, or use `--check` to check it without
writing. PlatformIO, the Linux CMake target and the jsdom harness run this same
assembler. The firmware builder minifies the result and generates
`src/transport/http/WebUiAsset.h`; the TC002 bundle checks both generated files
before shipping the full gzip stream.

`src/generated/berry-api.js` comes from the device bindings on every assembly.
Regenerate `src/generated/timezones.js` with `python scripts/tz_data.py --inject`
after updating the pinned `tzdata` package. Neither generated fragment is edited
by hand. Keep the `//linux:begin` / `//linux:end` regions around Linux-only code;
they are removed from the ESP32 asset by `scripts/build_webui.py`.
