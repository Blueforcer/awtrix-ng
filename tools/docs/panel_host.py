"""Run docs examples on awtrix-linux and read what the display shows.

One PanelHost is one runtime at one display size with factory settings, except that the
rotation does not move on by itself. It builds on Runtime from tests/runtime.py, the helper
the runtime tests use. The icons in tools/docs/panel-icons are installed before it starts.
"""

import shutil
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "tests"))

from runtime import Runtime  # noqa: E402

FRAME_MS = 70
FIRST_FRAME_MS = 100
ICONS = HERE / "panel-icons"
WEBUI = REPO / "webui" / "index.html"


class HostError(RuntimeError):
    pass


class PanelHost:
    def __init__(self, binary, width, height, webui=WEBUI):
        self._temporary = tempfile.TemporaryDirectory(prefix="awtrix-panels-")
        self.runtime = Runtime(self._temporary.name, binary, webui)
        self.runtime.width, self.runtime.height = width, height
        self.width, self.height = width, height
        self._shown = 0

    def __enter__(self):
        icons = self.runtime.data / "ICONS"
        icons.mkdir(parents=True, exist_ok=True)
        for icon in ICONS.glob("*.*"):
            shutil.copy(icon, icons / icon.name)
        self.runtime.start()
        self._call("PATCH", "/api/v1/settings", {"autoTransition": False})
        return self

    def __exit__(self, *exc):
        try:
            self.runtime.stop()
        finally:
            self._temporary.cleanup()

    def frames(self, example, at_ms=600, motion_s=None):
        """Show the example; return one frame at at_ms, or one every FRAME_MS for motion_s."""
        self._shown += 1
        name = "doc%d" % self._shown
        if example.kind == "script":
            reply = self._call("PUT", "/api/v1/apps/script/" + name, example.body, "text/plain")
            if reply.get("error"):
                raise HostError("the script does not run: %s" % reply["error"])
            self._call("PUT", "/api/v1/apps/active", {"name": name, "fast": True})
        elif example.kind == "pushed":
            self._call("PUT", "/api/v1/apps/pushed/" + name, example.body)
            self._call("PUT", "/api/v1/apps/active", {"name": name, "fast": True})
        else:
            self._call("POST", "/api/v1/notifications", example.body)
        start = time.monotonic() + FIRST_FRAME_MS / 1000.0
        try:
            if not motion_s:
                _sleep_until(start + at_ms / 1000.0)
                return [self._screen()]
            frames = []
            for i in range(max(1, int(motion_s * 1000 / FRAME_MS))):
                _sleep_until(start + i * FRAME_MS / 1000.0)
                frames.append(self._screen())
            return frames
        finally:
            if example.kind == "notification":
                self._call("DELETE", "/api/v1/notifications/active")
            else:
                self._call("DELETE", "/api/v1/apps/" + name)

    def _screen(self):
        screen = self._call("GET", "/api/v1/display/screen")
        if (screen["width"], screen["height"]) != (self.width, self.height):
            raise HostError("the display is %dx%d, not %dx%d"
                            % (screen["width"], screen["height"], self.width, self.height))
        return screen["pixels"]

    def _call(self, method, path, body=None, content_type="application/json"):
        status, reply = self.runtime.request(method, path, body, content_type)
        if status != 200:
            raise HostError("%s %s: %s %r" % (method, path, status, reply))
        return reply


def _sleep_until(deadline):
    delay = deadline - time.monotonic()
    if delay > 0:
        time.sleep(delay)
