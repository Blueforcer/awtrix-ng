"""Inject the firmware version macro from the repo-root `version` file.

`AWTRIX_NG_VERSION` used to be a hardcoded `#define` in src/AppConfig.h that a
release had to hand-sync with the repo-root `version` file -- two sources of
truth that drifted. This pre-script makes the `version` file the single
source: it reads the string and defines
`AWTRIX_NG_VERSION` for the build, so AppConfig.h's `#define` is only a fallback
for a bare/IDE compile that runs no PlatformIO scripts.

Runs two ways:

  * as a PlatformIO pre-script (every env), defining the macro for the build
  * standalone, printing the version -- handy for release/CI tooling:

        python scripts/gen_version.py
"""

import os
import sys

FALLBACK = "0.0.0-dev"


def read_version(root):
    """The trimmed contents of `<root>/version`, or None if missing/empty."""
    try:
        with open(os.path.join(root, "version"), encoding="utf-8") as f:
            return f.read().strip() or None
    except OSError:
        return None


def _platformio():
    version = read_version(env.subst("$PROJECT_DIR"))  # noqa: F821
    if not version:
        raise SystemExit("gen_version: repo-root `version` file is missing or empty")
    env.Append(CPPDEFINES=[("AWTRIX_NG_VERSION", env.StringifyMacro(version))])  # noqa: F821
    print("version: AWTRIX_NG_VERSION = %s (from ./version)" % version)


try:
    Import("env")  # noqa: F821
except NameError:
    if __name__ == "__main__":
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        sys.stdout.write((read_version(root) or FALLBACK) + "\n")
else:
    _platformio()
