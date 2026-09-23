#!/usr/bin/env python3
"""Render a real icon/text notification at 53x11 and print the screen as ASCII.

Run in the build container after native_sim is built. No network assets or
third-party Python packages are needed; the GIF comes from the test fixtures.
"""
import base64
import json
from pathlib import Path
import re
import tempfile
import time
import urllib.request

from test_panel_geometry import request, simulator


def main():
    fixture = Path("test/test_gifplayer/gif_fixtures.h").read_text()
    source = fixture.split("kGif8x8TwoFrames[] = {", 1)[1].split("};", 1)[0]
    icon = base64.b64encode(bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", source))).decode()
    with tempfile.TemporaryDirectory(prefix="awtrix-tall-notification-") as directory:
        data = Path(directory)
        with simulator(data) as base:
            request(base, "/api/v1/system", {"panelWidth": 53, "panelHeight": 11})
        with simulator(data) as base:
            payload = {"text": "HELLO", "icon": icon, "hold": True,
                       "textColor": "#FFFFFF", "backgroundColor": "#000000"}
            req = urllib.request.Request(base + "/api/v1/notifications", method="POST",
                                         data=json.dumps(payload).encode(),
                                         headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=3) as response:
                assert response.status == 200
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                frame = request(base, "/api/v1/display/screen")
                assert (frame["width"], frame["height"]) == (53, 11)
                pixels = frame["pixels"]
                assert len(pixels) == 53 * 11
                # Screen API publishes packed RGB integers. Wait out boot/transition.
                rows = [pixels[y * 53:(y + 1) * 53] for y in range(11)]
                if (not any(rows[0] + rows[9] + rows[10])
                        and all(all(row[:8]) for row in rows[1:9])
                        and any(any(row[9:]) for row in rows[1:9])):
                    print("PASS notification screen 53x11: icon/text in rows 1-8")
                    for row in rows:
                        print("".join("#" if pixel else "." for pixel in row))
                    return
                time.sleep(0.1)
            raise AssertionError("notification did not settle into the eight-row band")


if __name__ == "__main__":
    main()
