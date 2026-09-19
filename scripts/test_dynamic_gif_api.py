"""Opt-in live GIF regression: python scripts/test_dynamic_gif_api.py --url http://DEVICE.

Temporarily pauses rotation, installs private probe assets/apps, and restores the
original app and transition settings in finally. Does not resize or reboot a
device. Requires an already configured panel at least 32 pixels wide, height >= 8,
enabled scripting, and no global overlay/moodlight. Existing notifications are
never dismissed or replaced. Stop other writers during this short probe.

Only the Python standard library is needed. Optional HTTP Basic credentials are
read from AWTRIX_HTTP_USER/AWTRIX_HTTP_PASSWORD, never written into reports.
"""

from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import secrets
import statistics
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parent.parent
COLORS = (0xE12A47, 0x24CA79)
TILE_PALETTES = ((0xE12A47, 0x7A1231), (0x24CA79, 0x087A35),
                 (0x287CFA, 0x183A91), (0xFAD12D, 0xA15A12))


def animated_gif(width: int, height: int, frame_delay_ms: int = 300,
                 colors: tuple[int, int] = COLORS) -> bytes:
    """Two opaque, full-canvas frames; clear-before-literal keeps LZW at 3 bits."""
    if not 0 < width <= 65535 or not 0 < height <= 65535:
        raise ValueError("GIF dimensions must fit unsigned 16-bit integers")
    if frame_delay_ms <= 0 or frame_delay_ms % 10 or frame_delay_ms > 655350:
        raise ValueError("GIF frame delay must be a positive multiple of 10 ms, at most 655350")
    result = bytearray(b"GIF89a" + struct.pack("<HHBBB", width, height, 0xF0, 0, 0))
    for color in colors:
        result.extend(color.to_bytes(3, "big"))
    result.extend(b"\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00")
    for index in (0, 1):
        result.extend(b"\x21\xf9\x04\x04" + struct.pack("<H", frame_delay_ms // 10) + b"\x00\x00")
        result.extend(b"," + struct.pack("<HHHHB", 0, 0, width, height, 0))
        compressed = bytearray()
        bits = 0
        count = 0
        # Reset after each pixel, so neither dictionary growth nor width changes occur.
        for code in [value for _ in range(width * height) for value in (4, index)] + [5]:
            bits |= code << count
            count += 3
            while count >= 8:
                compressed.append(bits & 255)
                bits >>= 8
                count -= 8
        if count:
            compressed.append(bits & 255)
        result.append(2)
        for offset in range(0, len(compressed), 255):
            block = compressed[offset:offset + 255]
            result.append(len(block))
            result.extend(block)
        result.append(0)
    result.append(0x3B)
    return bytes(result)


class Api:
    def __init__(self, url: str):
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            raise ValueError("--url must be an http(s) device URL")
        if parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise ValueError("Do not include credentials, query parameters or fragments in --url")
        self.url = url.rstrip("/")
        self.headers = {}
        username = os.environ.get("AWTRIX_HTTP_USER")
        if username is not None:
            credentials = username + ":" + os.environ.get("AWTRIX_HTTP_PASSWORD", "")
            self.headers["Authorization"] = "Basic " + base64.b64encode(credentials.encode()).decode()

    def request(self, method: str, path: str, body=None,
                content_type="application/json", allowed=(200,)):
        if isinstance(body, dict):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        headers = dict(self.headers)
        if body is not None:
            headers["Content-Type"] = content_type
        request = urllib.request.Request(self.url + path, data=body, headers=headers, method=method)
        try:
            response = urllib.request.urlopen(request, timeout=10)
        except urllib.error.HTTPError as error:
            response = error
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            # Do not include arbitrary response bodies or authenticated URLs in diagnostics.
            raise RuntimeError(f"{method} {path}: transport failed ({type(error).__name__})") from None
        with response:
            payload = response.read()
            if response.status not in allowed:
                raise RuntimeError(f"{method} {path}: HTTP {response.status}")
            if response.status == 404:
                return None
            try:
                return json.loads(payload)
            except (ValueError, UnicodeDecodeError):
                raise RuntimeError(f"{method} {path}: expected JSON response") from None

    def upload(self, name: str, payload: bytes):
        boundary = "codex_gif_probe_" + secrets.token_hex(8)
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
                f'filename="{name}.gif"\r\nContent-Type: image/gif\r\n\r\n').encode()
        body += payload + f"\r\n--{boundary}--\r\n".encode()
        return self.request("POST", "/api/v1/files?dir=/ICONS", body,
                            f"multipart/form-data; boundary={boundary}")


def observe(api: Api, route: str, panel: tuple[int, int], gif_size: tuple[int, int],
            timeout: float) -> dict:
    width, height = panel
    gif_width, gif_height = gif_size
    expected = {
        color: [color if x < gif_width and y < gif_height else 0
                for y in range(height) for x in range(width)]
        for color in COLORS
    }
    seen = []
    samples = 0
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        screen = api.request("GET", "/api/v1/display/screen")
        if (screen.get("width"), screen.get("height")) != panel:
            raise RuntimeError("Panel dimensions changed during probe")
        samples += 1
        for color, pixels in expected.items():
            if screen.get("pixels") == pixels and (not seen or seen[-1] != color):
                seen.append(color)
        # Observe a complete A-B-A/B-A-B cycle, not just a static successful decode.
        if len(seen) >= 3:
            return {"route": route, "width": gif_width, "height": gif_height,
                    "samples": samples, "frames": [f"{color:06X}" for color in seen],
                    "right_edge_x": gif_width - 1, "full_frame_verified": True,
                    "status": "passed"}
        time.sleep(0.08)
    raise RuntimeError(f"{route} {gif_width}x{gif_height}: no complete animation cycle "
                       f"matching the full framebuffer after {samples} samples "
                       f"({len(seen)} frame changes); check other notifications/overlays")


MEMORY_KEYS = ("freeHeapBytes", "minFreeHeapBytes", "largestFreeBlockBytes",
               "psramTotalBytes", "psramFreeBytes")


def memory_snapshot(api: Api, phase: str, elapsed: float = 0) -> dict:
    device = api.request("GET", "/api/v1/device")
    return {"phase": phase, "elapsed_seconds": round(elapsed, 3),
            **{key: device[key] for key in MEMORY_KEYS if key in device}}


def summarize_memory(samples: list[dict]) -> dict:
    result = {}
    for key in MEMORY_KEYS:
        values = [sample[key] for sample in samples if key in sample]
        if values:
            result[key] = {"first": values[0], "last": values[-1],
                           "delta": values[-1] - values[0], "min": min(values), "max": max(values)}
    return result


def tile_colors(screen: dict, panel: tuple[int, int], placements: list[dict],
                palettes: dict | None = None):
    """Return each uniform tile's color only when the entire framebuffer is valid."""
    if (screen.get("width"), screen.get("height")) != panel:
        raise RuntimeError("Panel dimensions changed during probe")
    width, height = panel
    pixels = screen.get("pixels", [])
    if len(pixels) != width * height:
        return None
    expected = [0] * len(pixels)
    colors = []
    duplicate_colors = {}
    for placement in placements:
        x0, y0 = placement["x"], placement["y"]
        color = pixels[y0 * width + x0]
        palette = palettes[placement["icon"]] if palettes else COLORS
        if color not in palette:
            return None
        # The same cached icon drawn twice should be on the same frame. Distinct IDs
        # deliberately have independent timing and are never compared to one another.
        if duplicate_colors.get(placement["icon"], color) != color:
            return None
        duplicate_colors[placement["icon"]] = color
        colors.append(color)
        for y in range(y0, y0 + 8):
            for x in range(x0, x0 + 8):
                expected[y * width + x] = color
    return colors if pixels == expected else None


def observe_tiles(api: Api, route: str, case: str, panel: tuple[int, int],
                  placements: list[dict], periods: dict[str, int], timeout: float,
                  memory_seconds: float = 0, palettes: dict | None = None) -> dict:
    seen = [[] for _ in placements]
    changes_at = [[] for _ in placements]
    samples = 0
    valid_samples = 0
    malformed_after_start = 0
    started = time.monotonic()
    deadline = started + max(timeout, memory_seconds + 1)
    next_memory = started
    memory = []
    while time.monotonic() < deadline:
        screen = api.request("GET", "/api/v1/display/screen")
        colors = tile_colors(screen, panel, placements, palettes)
        samples += 1
        if colors is not None:
            valid_samples += 1
            for index, color in enumerate(colors):
                if not seen[index] or seen[index][-1] != color:
                    seen[index].append(color)
                    changes_at[index].append(time.monotonic())
        elif valid_samples:
            malformed_after_start += 1
        now = time.monotonic()
        if memory_seconds and now >= next_memory:
            memory.append(memory_snapshot(api, "during", now - started))
            next_memory = now + 1
        if all(len(frames) >= 3 for frames in seen) and now - started >= memory_seconds:
            if malformed_after_start:
                raise RuntimeError(f"{route} {case}: {malformed_after_start} incomplete framebuffer "
                                   "samples after initial valid multi-icon frame")
            medians = []
            for index, times in enumerate(changes_at):
                # The first observation can occur partway through a frame. Only measure
                # complete intervals between later changes; allow sampling/render-tick jitter.
                intervals = [1000 * (b - a) for a, b in zip(times[1:], times[2:])]
                median = statistics.median(intervals)
                requested = periods[placements[index]["icon"]]
                if len(intervals) >= 5 and abs(median - requested) > max(120, requested * 0.3):
                    raise RuntimeError(f"{route} {case}: tile {index} median delay {median:.0f} ms "
                                       f"does not match its {requested} ms GIF delay")
                medians.append(round(median, 1))
            return {"route": route, "case": case, "status": "passed", "samples": samples,
                    "valid_samples": valid_samples, "full_frame_verified": True,
                    "duration_seconds": round(now - started, 3),
                    "tiles": [{**placement, "frame_delay_ms": periods[placement["icon"]],
                               "observed_median_delay_ms": medians[index],
                               "frame_changes": len(seen[index]) - 1,
                               "colors_seen": sorted({f"{color:06X}" for color in seen[index]})}
                              for index, placement in enumerate(placements)],
                    "memory_samples": memory, "memory_trend": summarize_memory(memory)}
        time.sleep(0.08)
    raise RuntimeError(f"{route} {case}: expected a complete animation cycle in every tile; "
                       f"observed changes {[max(0, len(frames) - 1) for frames in seen]} "
                       f"over {samples} framebuffer samples")


def run(args) -> int:
    api = Api(args.url)
    namespace = "codex_gif_probe_" + secrets.token_hex(4)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    directory = ROOT / ".pio" / "gif-device-test" / (stamp + "_" + namespace)
    directory.mkdir(parents=True)
    report = {"namespace": namespace, "started_utc": stamp, "status": "running",
              "results": [], "cleanup_errors": []}
    report_path = directory / "report.json"

    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    pending_apps = set()
    pending_icons = set()
    pending_notifications = set()
    original = None
    settings_changed = False

    def remove_notification(name):
        api.request("DELETE", "/api/v1/notifications/" + name, allowed=(200, 404))
        pending_notifications.discard(name)

    def remove_app(name):
        api.request("DELETE", "/api/v1/apps/" + name, allowed=(200, 404))
        pending_apps.discard(name)

    def cleanup_action(label, action):
        try:
            action()
        except Exception as error:
            report["cleanup_errors"].append(f"{label}: {error}")

    try:
        screen = api.request("GET", "/api/v1/display/screen")
        panel = (screen["width"], screen["height"])
        report["panel"] = {"width": panel[0], "height": panel[1]}
        if panel[0] < 32 or panel[1] < 8:
            raise RuntimeError("Probe requires configured panel width >= 32 and height >= 8; "
                               "no geometry settings have been changed")
        settings = api.request("GET", "/api/v1/settings")
        device = api.request("GET", "/api/v1/device")
        display = api.request("GET", "/api/v1/display")
        if not device.get("scriptingRunning", False):
            raise RuntimeError("Berry scripting must already be running")
        if not display.get("power") or display.get("overlay") or display.get("moodlight"):
            raise RuntimeError("Display must already be powered on without global overlay/moodlight")
        apps = api.request("GET", "/api/v1/apps")
        files = api.request("GET", "/api/v1/files?dir=/ICONS")["files"]
        if any(app["name"].startswith("codex_gif_probe") for app in apps) or any(
                entry["name"].rsplit("/", 1)[-1].startswith("codex_gif_probe") for entry in files):
            raise RuntimeError("Existing codex_gif_probe app/icon found; refusing to alter it")
        original = {"currentApp": device.get("currentApp", ""),
                    "settings": {key: settings[key] for key in
                                 ("autoTransition", "transitionDurationMs")}}
        report["original"] = original
        if args.memory_seconds:
            report["memory"] = {
                "before": memory_snapshot(api, "before_all"),
                "note": "Short-run trends only; HTTP, scripts and retained caches affect readings. "
                        "These samples alone neither prove nor exclude a memory leak. "
                        "The API exposes the largest internal free block, not PSRAM's largest block."}
        save()  # Recovery data is on disk before the first mutation.
        settings_changed = True  # Cleanup also covers a timeout after a successful write.
        api.request("PATCH", "/api/v1/settings",
                    {"autoTransition": False, "transitionDurationMs": 0})
        for suffix, gif_size in (("8", (8, 8)), ("w", panel)):
            icon = namespace + "_" + suffix
            gif = animated_gif(*gif_size)
            (directory / (icon + ".gif")).write_bytes(gif)
            pending_icons.add(icon)
            api.upload(icon, gif)
            payload = {"icon": icon, "text": "", "backgroundColor": "#000000",
                       "iconMode": "fixed", "durationMs": 15000}

            pushed = namespace + "_p"
            pending_apps.add(pushed)
            api.request("PUT", "/api/v1/apps/pushed/" + pushed, payload)
            api.request("PUT", "/api/v1/apps/active", {"name": pushed, "fast": True})
            report["results"].append(observe(api, "pushed", panel, gif_size, args.timeout))
            save()
            remove_app(pushed)

            script = namespace + "_s"
            source = ('class GifProbe\n  def draw()\n    clear()\n'
                      f'    icon("{icon}", 0, 0)\n  end\nend\nreturn GifProbe()\n')
            pending_apps.add(script)
            reply = api.request("PUT", "/api/v1/apps/script/" + script, source, "text/plain")
            if reply.get("error"):
                raise RuntimeError("Probe Berry script failed to compile; inspect device script diagnostics")
            api.request("PUT", "/api/v1/apps/active", {"name": script, "fast": True})
            report["results"].append(observe(api, "script", panel, gif_size, args.timeout))
            save()
            remove_app(script)

            notification = namespace + "_n"
            pending_notifications.add(notification)
            api.request("POST", "/api/v1/notifications",
                        {**payload, "name": notification, "hold": True, "stack": True})
            report["results"].append(observe(api, "notification", panel, gif_size, args.timeout))
            save()
            remove_notification(notification)

        periods = {}
        palettes = {}
        for index, delay in enumerate((200, 300, 400, 500)):
            icon = namespace + f"_m{index}"
            periods[icon] = delay
            palettes[icon] = TILE_PALETTES[index]
            gif = animated_gif(8, 8, delay, palettes[icon])
            (directory / (icon + ".gif")).write_bytes(gif)
            pending_icons.add(icon)
            api.upload(icon, gif)
        icon_names = list(periods)
        x_positions = ([0, 8, 16, 24] if panel[0] < 40
                       else [index * 8 for index in range(len(icon_names))])
        cases = (
            ("duplicate_id", [{"icon": icon_names[0], "x": x, "y": 0} for x in (0, 24)]),
            ("four_independent_ids", [{"icon": name, "x": x_positions[index], "y": 0}
                                      for index, name in enumerate(icon_names)]),
        )
        for case, placements in cases:
            memory_seconds = args.memory_seconds if case == "four_independent_ids" else 0
            payload = {"icons": placements, "text": "", "backgroundColor": "#000000",
                       "durationMs": int(max(15, memory_seconds + 10) * 1000)}
            for route in ("pushed", "script", "notification"):
                before = memory_snapshot(api, "before_case") if memory_seconds else None
                app = namespace + ("_s" if route == "script" else "_p")
                notification = namespace + "_n"
                if route == "pushed":
                    pending_apps.add(app)
                    api.request("PUT", "/api/v1/apps/pushed/" + app, payload)
                elif route == "script":
                    commands = "".join(f'    icon("{item["icon"]}", {item["x"]}, {item["y"]})\n'
                                       for item in placements)
                    source = "class GifProbe\n  def draw()\n    clear()\n" + commands
                    source += "  end\nend\nreturn GifProbe()\n"
                    pending_apps.add(app)
                    reply = api.request("PUT", "/api/v1/apps/script/" + app, source, "text/plain")
                    if reply.get("error"):
                        raise RuntimeError("Probe Berry multi-icon script failed to compile")
                else:
                    pending_notifications.add(notification)
                    api.request("POST", "/api/v1/notifications",
                                {**payload, "name": notification, "hold": True, "stack": True})
                if route != "notification":
                    api.request("PUT", "/api/v1/apps/active", {"name": app, "fast": True})
                result = observe_tiles(api, route, case, panel, placements, periods,
                                       args.timeout, memory_seconds, palettes)
                report["results"].append(result)
                save()
                if route == "notification":
                    remove_notification(notification)
                else:
                    remove_app(app)
                if memory_seconds:
                    time.sleep(0.1)  # Allow a render tick to release the removed page's resources.
                    result["memory_before"] = before
                    result["memory_after"] = memory_snapshot(api, "after_case")
                    result["memory_trend_with_boundaries"] = summarize_memory(
                        [before, *result["memory_samples"], result["memory_after"]])
                save()
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report["status"] = "failed"
        report["error"] = str(error) or type(error).__name__
    finally:
        for name in sorted(pending_notifications):
            cleanup_action("delete notification " + name, lambda name=name: remove_notification(name))
        for name in sorted(pending_apps):
            cleanup_action("delete app " + name, lambda name=name: remove_app(name))
        for name in sorted(pending_icons):
            cleanup_action("delete icon " + name, lambda name=name: api.request(
                "DELETE", "/api/v1/files?" + urllib.parse.urlencode({"path": f"/ICONS/{name}.gif"}),
                allowed=(200, 404)))
        if settings_changed and original:
            if original["currentApp"]:
                cleanup_action("restore active app", lambda: api.request(
                    "PUT", "/api/v1/apps/active", {"name": original["currentApp"], "fast": True}))
            cleanup_action("restore settings", lambda: api.request(
                "PATCH", "/api/v1/settings", original["settings"]))
            def verify_settings():
                restored = api.request("GET", "/api/v1/settings")
                if any(restored.get(key) != value for key, value in original["settings"].items()):
                    raise RuntimeError("settings readback differs from saved values")
            cleanup_action("verify restored settings", verify_settings)
            if args.memory_seconds and "memory" in report:
                def final_memory():
                    time.sleep(0.1)
                    report["memory"]["after"] = memory_snapshot(api, "after_cleanup")
                    report["memory"]["before_after"] = summarize_memory(
                        [report["memory"]["before"], report["memory"]["after"]])
                cleanup_action("record final memory", final_memory)
        if report["cleanup_errors"]:
            report["status"] = "failed"
        save()
    print(json.dumps(report, indent=2))
    print(f"Report and recovery data: {report_path}", file=sys.stderr)
    return 0 if report["status"] == "passed" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="Explicit live device base URL")
    parser.add_argument("--timeout", type=float, default=6,
                        help="Maximum seconds per animation probe (default: 6)")
    parser.add_argument("--memory-seconds", type=float, default=0,
                        help="Observe RAM for this many seconds per four-icon route; e.g. 20 (default: off)")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.memory_seconds < 0:
        parser.error("--memory-seconds must not be negative")
    try:
        return run(args)
    except ValueError as error:
        parser.error(str(error))


if __name__ == "__main__":
    sys.exit(main())
