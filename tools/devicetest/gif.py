#!/usr/bin/env python3
"""Check GIF pixels on a device using temporary pushed apps and native test fixtures.

    python tools/devicetest/gif.py --host awtrix-ng.local

Exercises resident static images, streamed global/local RGB palettes, full-width
animation, and reopening. No files are uploaded or settings changed. The temporary
app is deleted and the original current app restored even if a check fails.
"""

import argparse
import base64
import json
import os
from pathlib import Path
import re
import time
import uuid

from run import Api


FIXTURES = Path(__file__).resolve().parents[2] / "test/test_gifplayer/gif_fixtures.h"


def fixture(name):
    source = FIXTURES.read_text(encoding="utf-8")
    match = re.search(r"unsigned char " + re.escape(name) + r"\[\] = \{(.*?)\};",
                      source, re.S)
    if not match:
        raise RuntimeError("native GIF fixture not found: " + name)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", match[1]))
    return base64.b64encode(data).decode("ascii")


def checked(api, method, path, body=None):
    status, result = api.call(method, path, None if body is None else json.dumps(body),
                              None if body is None else "application/json")
    if not 200 <= status < 300:
        raise RuntimeError(f"{method} {path}: HTTP {status}: {result}")
    return result


def halves(left, right):
    return tuple(left if x < 4 else right for _ in range(8) for x in range(8))


def wait_frames(api, expected, width, seconds):
    """Require the exact frame sequence, including returning to the first frame."""
    deadline = time.monotonic() + seconds
    next_frame = 0
    pixels = ()
    while time.monotonic() < deadline:
        screen = checked(api, "GET", "/api/v1/display/screen")
        stride, height = screen.get("width", 0), screen.get("height", 0)
        raw = screen.get("pixels", [])
        if stride < width or height != 8 or len(raw) != stride * height:
            raise RuntimeError(f"unexpected framebuffer {stride}x{height}, {len(raw)} pixels")
        pixels = tuple(raw[y * stride + x] for y in range(8) for x in range(width))
        if pixels == expected[next_frame]:
            next_frame += 1
            if next_frame == len(expected):
                return
        time.sleep(0.06)
    colors = ",".join(f"{color:06x}" for color in sorted(set(pixels))[:12])
    raise RuntimeError(f"frame {next_frame + 1}/{len(expected)} not seen exactly; colors={colors}")


def exercise(api, seconds):
    device = checked(api, "GET", "/api/v1/device")
    original = device.get("currentApp", "")
    display = checked(api, "GET", "/api/v1/display")
    if display.get("overlay"):
        raise RuntimeError("a global overlay is active and would change the expected GIF pixels")
    if device.get("matrixPower") is False:
        raise RuntimeError("matrix power is off")
    existing = {app["name"] for app in checked(api, "GET", "/api/v1/apps")}
    name = "tgif_" + uuid.uuid4().hex[:12]
    while name in existing:
        name = "tgif_" + uuid.uuid4().hex[:12]

    red_static = halves(0xFF0000, 0)
    global_rgb = halves(0x140A05, 0x5A96D2)
    local_rgb = halves(0xC8C9CA, 0x214D6F)
    red_wide, blue_wide = (0xFF0000,) * 256, (0x0000FF,) * 256
    cases = [
        ("static transparency", "kGifTransparentStatic", 8, [red_static]),
        ("streamed exact global/local RGB and rewind", "kGifOddPalette", 8,
         [global_rgb, local_rgb, global_rgb]),
        ("full 32x8 streamed animation", "kGif32x8ManyFrames", 32,
         [red_wide, blue_wide, red_wide]),
        ("reopen static after streaming", "kGifTransparentStatic", 8, [red_static]),
    ]
    failures = []
    attempted = False
    passed = 0
    try:
        for label, asset, width, frames in cases:
            payload = {"icon": fixture(asset), "iconMode": "fixed", "text": "",
                       "backgroundColor": "#000000", "durationMs": 60000,
                       "lifetimeMs": 120000, "lifetimeExpiry": "remove"}
            attempted = True
            checked(api, "PUT", "/api/v1/apps/pushed/" + name, payload)
            checked(api, "PUT", "/api/v1/apps/active", {"name": name, "fast": True})
            wait_frames(api, frames, width, seconds)
            passed += 1
            print("PASS " + label, flush=True)
    except (Exception, KeyboardInterrupt) as error:
        failures.append(str(error) or "interrupted")
    finally:
        if attempted:
            try:
                status, body = api.delete("/api/v1/apps/" + name)
                if status not in (200, 204, 404):
                    raise RuntimeError(f"temporary app cleanup: HTTP {status}: {body}")
                remaining = checked(api, "GET", "/api/v1/apps")
                if any(app.get("name") == name for app in remaining):
                    raise RuntimeError("temporary app still exists after deletion")
            except Exception as error:
                failures.append(str(error))
            try:
                current = checked(api, "GET", "/api/v1/device").get("currentApp")
                if original and current != original:
                    checked(api, "PUT", "/api/v1/apps/active",
                            {"name": original, "fast": True})
            except Exception as error:
                failures.append("restore original app: " + str(error))
    for failure in failures:
        print("FAIL " + failure, flush=True)
    print(f"{passed}/{len(cases)} GIF checks passed; {len(failures)} failures", flush=True)
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default=os.environ.get("AWTRIX", "awtrix-ng.local"))
    parser.add_argument("--auth", help="user:password if the device has a login")
    parser.add_argument("--seconds", type=float, default=6,
                        help="maximum wait per GIF check (default: 6)")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    try:
        return exercise(Api(args.host, args.auth, timeout=8), args.seconds)
    except Exception as error:
        print("FAIL " + str(error), flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
