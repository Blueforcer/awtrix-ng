#!/usr/bin/env python3
"""Render the matrix screenshots that sit under the code examples in docs/guides/.

The pictures are not drawn by hand. Every one of them is the real firmware
rendering the real example: this script boots `native_sim`, replays each code
block against it, and reads the canvas back from `/api/v1/display/screen`. A
change to the renderer therefore shows up in the docs on the next run, and an
example that no longer works fails here instead of lying in a picture.

    pio run -e native_sim
    pip install pillow
    python tools/gen_docshots.py                 # all guides
    python tools/gen_docshots.py --only text     # one page

Each shot is captured as a burst of frames. If they are all identical the result
is a PNG. If the panel moves it becomes a GIF, cut where the animation returns
to its first frame so it loops without a seam; an animation that never comes
back to where it started - a palette drifting across the text, say - is cut
where the jump is smallest instead.

The markdown is rewritten between generated markers:

    <!-- shot:begin id=hello hash=1a2b3c4d -->
    ![orange HELLO](../assets/shots/text/hello.png){ .shot }
    <!-- shot:end -->

Alt text written by hand inside those markers is preserved across runs; only the
path and the hash are updated. The hash covers the code block, so
`tools/check_docshots.py` can tell when an example changed without the picture
being regenerated.

Per-block behaviour is steered by an optional directive on the line right after
the closing fence:

    <!-- shot: skip -->                       never render this block
    <!-- shot: each -->                       one picture per command, not per block
    <!-- shot: id=my-name -->                 fixed file name
    <!-- shot: wait=1500 -->                  settle time in ms before capturing
    <!-- shot: window=12000 -->               how long to look for an animation loop
    <!-- shot: base={"text":"HI"} -->         push this app first, then run the block
    <!-- shot: alt=... -->                    force the alt text
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit("gen_docshots: Pillow is required (pip install pillow)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GUIDES = os.path.join(ROOT, "docs", "guides")
SHOTS = os.path.join(ROOT, "docs", "assets", "shots")
SIM = os.path.join(ROOT, ".pio", "build", "native_sim",
                   "program.exe" if os.name == "nt" else "program")

# Capture timing. The simulator renders at 40 fps, so a canvas stands for 25 ms;
# sampling faster than that catches every one of them. GIF stores delays in
# whole centiseconds, and browsers treat anything under 20 ms as "as fast as
# possible", so frames are merged until each lasts at least 50 ms.
FRAME_MS = 25
SAMPLE_MS = 15
GIF_MIN_MS = 50
CUT_MIN_MS = 1200
CUT_MAX_MS = 3000
# How long the panel must sit unchanged before it counts as a still picture.
# It has to outlast the slowest plateau an animation can have, or a blink with
# a one-second phase and a fade that crawls through its dim end both get filed
# as stills - which is exactly what they look like for the first half-second.
STILL_MS = 2500
DEFAULT_WAIT_MS = 1200
DEFAULT_WINDOW_MS = 20000
CELL = 8          # pixels per LED, including the gap
LED = 7           # lit area of one LED
PAD = 4           # border around the panel
OFF = (14, 14, 14)

# Routes whose effect is visible on the panel. A block that touches none of them
# (a DELETE, a settings-only change, a GET) is set up but never photographed.
VISIBLE = ("/api/v1/notifications", "/api/v1/apps/pushed/", "/api/v1/display",
           "/api/v1/apps/script/")

# Keys that actually put something on the canvas. A payload without one of them
# paints nothing - and, more importantly, this is what separates a request body
# from the API *responses* the docs quote in the very same `json` fences.
CONTENT_KEYS = {"text", "icon", "draw", "barChart", "lineChart", "progress",
                "effect", "overlay", "backgroundColor"}

# ... except that a quoted response can carry a content key too - the app list
# names the icon of every pushed app. These keys never appear in a request body.
RESPONSE_KEYS = {"ok", "error", "origin", "inLoop", "present", "slot", "version",
                 "available", "usedBytes", "totalBytes", "melodies", "stations"}

# The docs reference icon IDs from the LaMetric gallery. Those are nothing but
# file names on the device, and AWTRIX ships an empty /ICONS, so an example
# would otherwise photograph a blank gap where the reader's icon goes. Every ID
# the guides mention is seeded with the same real gallery icon, 3253, so the
# pictures show what an icon does to the layout - the left 9 px reserved, the
# text pushed across - with a genuine 8x8 icon rather than an invention.
ICON_SRC = os.path.join(ROOT, "tools", "docshots", "lametric-3253.gif")
ICON_ID = re.compile(r"^[A-Za-z0-9_-]{1,16}$")

SHOT_APP = "docshot"
SHOT_SCRIPT = "DocShot"
BLANK_APP = "docshot-blank"


class SimError(RuntimeError):
    pass


# --------------------------------------------------------------------------- HTTP

class Sim:
    def __init__(self, port, data_dir, binary=SIM):
        self.base = "http://127.0.0.1:%d" % port
        self.port = port
        self.data_dir = data_dir
        self.binary = binary
        self.proc = None
        self.baseline = None

    def start(self):
        if not os.path.exists(self.binary):
            raise SimError("simulator not built: %s\n  run: pio run -e native_sim"
                           % self.binary)
        if os.path.isdir(self.data_dir):
            shutil.rmtree(self.data_dir)
        os.makedirs(self.data_dir, exist_ok=True)
        self.proc = subprocess.Popen(
            [self.binary, "--port", str(self.port), "--no-matrix",
             "--data", self.data_dir],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                self.get("/api/v1/version")
                break
            except Exception:
                time.sleep(0.1)
        else:
            raise SimError("simulator did not answer on port %d" % self.port)
        # One stable stage for every shot: no rotation, no auto-brightness, so a
        # picture taken now looks the same as one taken tomorrow.
        self.req("PATCH", "/api/v1/settings",
                 {"autoTransition": False, "autoBrightness": False, "brightness": 120})
        self.baseline = json.loads(self.get("/api/v1/settings"))

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None

    def req(self, method, path, body=None, ctype="application/json"):
        data = None
        if body is not None:
            if isinstance(body, (dict, list)):
                data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            elif isinstance(body, bytes):
                data = body
            else:
                data = body.encode("utf-8")
        r = urllib.request.Request(self.base + path, data=data, method=method)
        if data is not None:
            r.add_header("Content-Type", ctype)
        try:
            with urllib.request.urlopen(r, timeout=15) as fh:
                return fh.status, fh.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read().decode("utf-8", "replace")

    def get(self, path):
        status, body = self.req("GET", path)
        if status != 200:
            raise SimError("GET %s -> %d %s" % (path, status, body))
        return body

    # ---------------------------------------------------------------- state

    def reset(self):
        """Clear the panel to a verified blank stage and return that canvas.

        Blanking it deliberately, rather than leaving whatever was on it, is
        what makes a failed shot *visible*: every example is then required to
        change the panel, so one that silently does nothing raises instead of
        quietly photographing the clock.

        Notifications are the trap. One left standing covers the panel, they
        stack, and dismissing is a queued command - so this waits for the panel
        to actually go blank rather than counting dismisses and hoping.
        """
        self.req("PATCH", "/api/v1/settings", self.baseline)
        self.req("PATCH", "/api/v1/display", {"overlay": None, "effect": None})
        for app in json.loads(self.get("/api/v1/apps")):
            if app.get("origin") in ("pushed", "script"):
                self.req("DELETE", "/api/v1/apps/" + app["name"])
        self.req("PUT", "/api/v1/apps/pushed/" + BLANK_APP, {"text": " "})
        if not self.pin(BLANK_APP):
            raise SimError("could not pin the blank stage")

        # Dismissing the active notification lets the next queued one take the
        # panel, and there is a gap between the two where it looks clear. Insist
        # on it *staying* blank, or the stage is handed over still occupied.
        deadline = time.perf_counter() + 8
        clear_since = None
        while time.perf_counter() < deadline:
            _, _, px = self.screen()
            if any(px):
                clear_since = None
                self.req("DELETE", "/api/v1/notifications/active")
            elif clear_since is None:
                clear_since = time.perf_counter()
            elif time.perf_counter() - clear_since >= 0.4:
                return px
            time.sleep(0.05)
        raise SimError("panel would not go blank between shots")

    def pin(self, name):
        """Hold one app on the panel.

        `currentApp` flips at the *start* of a transition, so polling it and
        shooting straight away catches a blend of two apps. `fast` skips the
        transition, and auto-transition is already off, so the app stays put.
        """
        status, body = self.req("PUT", "/api/v1/apps/active", {"name": name, "fast": True})
        if status != 200:
            return False
        for _ in range(40):
            if json.loads(self.get("/api/v1/device")).get("currentApp") == name:
                return True
            time.sleep(0.05)
        return False

    def screen(self):
        s = json.loads(self.get("/api/v1/display/screen"))
        return s["width"], s["height"], tuple(s["pixels"])


# --------------------------------------------------------------------------- capture

def capture(sim, wait_ms, window_ms):
    """Sample the panel and return (width, height, [(frame, duration_ms), ...]).

    Sampling faster than the panel renders and then collapsing repeats turns the
    poll into the *sequence of distinct canvases* with how long each one stood.
    That matters twice over: a one-second hold at the end of a scroll becomes a
    single frame instead of forty identical ones, and the sequence is genuinely
    periodic, so the loop can be cut where it truly repeats. Comparing raw
    samples cannot find that period at all - polling and rendering drift against
    each other and no two samples ever land on the same phase twice.
    """
    time.sleep(wait_ms / 1000.0)
    width, height, first = sim.screen()
    runs = [[first, 0.0]]
    last = time.perf_counter()
    deadline = last + window_ms / 1000.0
    cand = 0            # candidate period: the panel came back to its first canvas
    confirmed = 0       # how much of a second cycle has replayed since

    while True:
        now = time.perf_counter()
        runs[-1][1] += (now - last) * 1000.0
        last = now
        if len(runs) == 1 and runs[0][1] >= STILL_MS:
            return width, height, [(first, 0)]
        if now >= deadline:
            break
        time.sleep(SAMPLE_MS / 1000.0)
        _, _, px = sim.screen()
        if px == runs[-1][0]:
            continue
        runs.append([px, 0.0])
        i = len(runs) - 1
        if cand:
            # A blink or a fade passes through its first canvas twice a cycle,
            # so one sighting proves nothing. Replay a whole second cycle
            # against the first before believing the period.
            if runs[i][0] == runs[i - cand][0]:
                confirmed += 1
                if confirmed >= cand:
                    return width, height, quantize(runs[1:cand + 1])
            else:
                cand, confirmed = 0, 0
        if not cand and px == first and i >= 2:
            cand, confirmed = i, 0

    if len(runs) == 1:
        return width, height, [(first, 0)]
    return width, height, quantize(best_cut(runs))


def distance(a, b):
    """How far apart two canvases are, summed over every channel."""
    total = 0
    for x, y in zip(a, b):
        total += (abs((x >> 16 & 0xFF) - (y >> 16 & 0xFF))
                  + abs((x >> 8 & 0xFF) - (y >> 8 & 0xFF))
                  + abs((x & 0xFF) - (y & 0xFF)))
    return total


def best_cut(runs):
    """The best loop available when nothing repeats exactly.

    Some animations never come back to where they started - a palette drifting
    across the text is the usual one. Cutting where the panel looks most like
    its first frame gives the smallest jump a loop can have, and the length is
    bounded so one restless example cannot produce a megabyte of GIF.
    """
    seq = runs[1:-1]                 # the first and last runs have partial durations
    if len(seq) < 3:
        return runs[:-1] or runs
    first = seq[0][0]
    elapsed = 0.0
    best, best_score = len(seq), None
    for j in range(1, len(seq)):
        elapsed += seq[j - 1][1]
        if elapsed < CUT_MIN_MS:
            continue
        if elapsed > CUT_MAX_MS:
            best = best if best_score is not None else j
            break
        score = distance(seq[j][0], first)
        if best_score is None or score < best_score:
            best, best_score = j, score
    return seq[:best]


def quantize(runs):
    """Round each run to a whole number of rendered frames."""
    out = []
    for pixels, ms in runs:
        frames = max(1, int(round(ms / FRAME_MS)))
        out.append((pixels, int(frames * FRAME_MS)))
    return out


# --------------------------------------------------------------------------- drawing

def render(width, height, pixels):
    """One frame as an image, drawn the way the web UI draws its live preview."""
    img = Image.new("RGB", (width * CELL + 2 * PAD, height * CELL + 2 * PAD), (0, 0, 0))
    d = ImageDraw.Draw(img)
    for y in range(height):
        for x in range(width):
            v = pixels[y * width + x] & 0xFFFFFF
            color = ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF) if v else OFF
            x0 = PAD + x * CELL
            y0 = PAD + y * CELL
            d.rounded_rectangle([x0, y0, x0 + LED - 1, y0 + LED - 1], radius=2, fill=color)
    return img


def merge_short(frames):
    """Drop frames until every one of them lasts at least GIF_MIN_MS.

    Scrolling advances one pixel every 25 ms, which no GIF can show; showing
    every other position for twice as long keeps the text moving at the speed
    the panel moves it. A hold is already long enough and survives untouched.
    """
    out = []
    pending = None
    total = 0
    for pixels, ms in frames:
        if pending is None:
            pending, total = pixels, ms
        else:
            total += ms
        if total >= GIF_MIN_MS:
            out.append((pending, total))
            pending, total = None, 0
    if pending is not None:
        if out:
            out[-1] = (out[-1][0], out[-1][1] + total)
        else:
            out.append((pending, max(total, GIF_MIN_MS)))
    return out


def rotate_to_brightest(frames):
    """Start the loop on its fullest frame.

    A loop can begin anywhere, and where it begins is what a reader sees before
    the GIF plays and in any preview that does not animate. A blink captured
    from its dark half would otherwise be published as an empty panel.
    """
    if len(frames) < 2:
        return frames
    best = max(range(len(frames)),
               key=lambda i: (sum(1 for p in frames[i][0] if p), -i))
    return frames[best:] + frames[:best]


def shared_palette(frames):
    """One palette for the whole animation, built from the colours it uses.

    Left alone, the encoder picks a fresh palette for every frame and dithers
    into it. Both are ruinous here: a plasma effect ends up with a different
    palette per frame and dither noise where the panel has flat colour, and the
    file grows several times over for a picture that looks no better. Quantising
    the *canvas* colours is enough - a frame is only ever blocks of those.
    """
    colors = sorted({p & 0xFFFFFF for f, _ in frames for p in f})
    colors.append((OFF[0] << 16) | (OFF[1] << 8) | OFF[2])
    strip = Image.new("RGB", (len(colors), 1))
    strip.putdata([((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF) for c in colors])
    return strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT)


def write_image(path_noext, width, height, frames):
    """Write a PNG for a still panel, a looping GIF for a moving one."""
    merged = merge_short(rotate_to_brightest(frames)) if len(frames) > 1 else frames
    if len(merged) == 1:
        path = path_noext + ".png"
        render(width, height, merged[0][0]).save(path, optimize=True)
    else:
        path = path_noext + ".gif"
        palette = shared_palette(merged)
        images = [render(width, height, f).quantize(palette=palette,
                                                    dither=Image.Dither.NONE)
                  for f, _ in merged]
        images[0].save(path, save_all=True, append_images=images[1:],
                       duration=[ms for _, ms in merged], loop=0,
                       disposal=1, optimize=True)
    for other in (".png", ".gif"):
        stale = path_noext + other
        if stale != path and os.path.exists(stale):
            os.remove(stale)
    return path


# --------------------------------------------------------------------------- markdown

FENCE = re.compile(r"^```([a-zA-Z0-9]*)[ \t]*\n(.*?)^```[ \t]*$", re.S | re.M)
DIRECTIVE = re.compile(r"^<!--\s*shot:\s*(?!begin|end)(.*?)-->[ \t]*$", re.M)
BEGIN = re.compile(r"^<!--\s*shot:begin\b([^>]*)-->[ \t]*\n(.*?)^<!--\s*shot:end\s*-->[ \t]*$",
                   re.S | re.M)
ALT = re.compile(r"^!\[(.*?)\]\(", re.M)
WHAT_YOU_SEE = re.compile(r"^\*\*What you see:\*\*.*?(?=\n\n|\Z)", re.S | re.M)


def parse_directive(text):
    """`id=x wait=500 skip base={...}` -> dict.

    Hand-scanned rather than shlex'd because the useful values are JSON objects
    full of quotes, and a shell-style split would strip exactly the quotes that
    make them parse.
    """
    out = {}
    i, n = 0, len(text)
    while i < n:
        while i < n and text[i].isspace():
            i += 1
        m = re.compile(r"[A-Za-z_][\w-]*").match(text, i)
        if not m:
            break
        key = m.group(0)
        i = m.end()
        if i >= n or text[i] != "=":
            out[key] = True
            continue
        i += 1
        if i < n and text[i] in "\"'":
            quote = text[i]
            end = text.find(quote, i + 1)
            end = n if end < 0 else end
            out[key] = text[i + 1:end]
            i = end + 1
        elif i < n and text[i] in "{[":
            depth, j = 0, i
            while j < n:
                if text[j] in "{[":
                    depth += 1
                elif text[j] in "}]":
                    depth -= 1
                    if depth == 0:
                        j += 1
                        break
                j += 1
            out[key] = text[i:j]
            i = j
        else:
            j = i
            while j < n and not text[j].isspace():
                j += 1
            out[key] = text[i:j]
            i = j
    return out


def slugify(text, fallback):
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s[:40].strip("-") or fallback


def blocks_of(text):
    """Every fenced block, with the directive and generated region that follow it."""
    out = []
    for m in FENCE.finditer(text):
        pos = m.end()
        if pos < len(text) and text[pos] == "\n":
            pos += 1
        directive, existing, alt = {}, None, []
        dm = DIRECTIVE.match(text, pos)
        if dm:
            directive = parse_directive(dm.group(1))
            pos = dm.end() + 1
        bm = BEGIN.match(text, pos)
        if bm:
            existing = (bm.start(), bm.end())
            # One per image: a block split into several shots keeps a separate
            # hand-written alt for each, and folding them all onto the first
            # would quietly overwrite the other descriptions on the next run.
            alt = ALT.findall(bm.group(2))
        out.append({
            "lang": m.group(1), "body": m.group(2),
            "fence_start": m.start(), "after": m.end(),
            "directive": directive, "existing": existing, "alts": alt or [],
            "insert_at": bm.start() if bm else pos,
        })
    return out


# --------------------------------------------------------------------------- commands

CURL_URL = re.compile(r"^https?://[^/]+")


def logical_lines(body):
    """Undo backslash line continuations."""
    out, cur = [], ""
    for line in body.split("\n"):
        s = line.rstrip()
        if s.endswith("\\"):
            cur += s[:-1] + " "
        else:
            cur += s
            out.append(cur)
            cur = ""
    if cur:
        out.append(cur)
    return out


def parse_curl(cmd):
    """A curl invocation as (method, path, body, content-type), or None."""
    try:
        toks = shlex.split(cmd, comments=True)
    except ValueError:
        return None
    method, url, data, ctype = None, None, None, "application/json"
    i = 1
    while i < len(toks):
        t = toks[i]
        if t in ("-X", "--request") and i + 1 < len(toks):
            method = toks[i + 1].upper()
            i += 2
        elif t in ("-H", "--header") and i + 1 < len(toks):
            h = toks[i + 1]
            if h.lower().startswith("content-type:"):
                ctype = h.split(":", 1)[1].strip()
            i += 2
        elif t in ("-d", "--data", "--data-raw", "--data-binary") and i + 1 < len(toks):
            data = toks[i + 1]
            i += 2
        elif t in ("-F", "--form", "-T", "--upload-file"):
            return None                      # file uploads have no payload to replay
        elif t.startswith("-"):
            i += 1
        else:
            if t.startswith("http"):
                url = t
            i += 1
    if not url or (data and data.startswith("@")):
        return None
    path = CURL_URL.sub("", url)
    if not path.startswith("/"):
        return None
    if method is None:
        method = "POST" if data else "GET"
    return method, path, data, ctype


def commands_of(block):
    """The replayable requests in a block, in order."""
    lang, body = block["lang"], block["body"]
    if lang == "bash":
        out = []
        for line in logical_lines(body):
            line = line.strip()
            if line.startswith("curl"):
                parsed = parse_curl(line)
                if parsed:
                    out.append(parsed)
        return out
    if lang == "json":
        try:
            payload = json.loads(body)
        except ValueError:
            return []
        if not has_content(payload):
            return []
        return [("PUT", "/api/v1/apps/pushed/" + SHOT_APP, json.dumps(payload),
                 "application/json")]
    if lang == "berry":
        if not re.search(r"^\s*return\s+\w+\(", body, re.M) or not re.search(r"^class ", body, re.M):
            return []
        if not re.search(r"\bdef\s+draw\s*\(", body):
            return []                        # headless: nothing to photograph
        return [("PUT", "/api/v1/apps/script/" + SHOT_SCRIPT, body, "text/plain")]
    return []


def has_content(payload):
    """True if this body paints something. A frame list counts if any frame does."""
    if isinstance(payload, dict):
        if RESPONSE_KEYS & set(payload):
            return False
        return bool(CONTENT_KEYS & set(payload))
    if isinstance(payload, list):
        return any(has_content(f) for f in payload)
    return False


def icon_ids(payload):
    """Every icon name a payload asks for, ignoring inline base64 bitmaps."""
    out = set()
    if isinstance(payload, list):
        for frame in payload:
            out |= icon_ids(frame)
    elif isinstance(payload, dict):
        icon = payload.get("icon")
        if isinstance(icon, str) and ICON_ID.match(icon):
            out.add(icon)
    return out


def seed_icons(icons_dir, ids):
    """Give every referenced ID a real icon file, so nothing renders as a gap."""
    if not ids:
        return
    os.makedirs(icons_dir, exist_ok=True)
    for name in ids:
        target = os.path.join(icons_dir, name + ".gif")
        if not os.path.exists(target):
            shutil.copyfile(ICON_SRC, target)


def is_visible(cmd):
    method, path, data, _ = cmd
    if method in ("GET", "DELETE"):
        return False
    if not any(path.startswith(p) for p in VISIBLE):
        return False
    if path.startswith("/api/v1/apps/script/"):
        return True
    try:
        payload = json.loads(data or "{}")
    except ValueError:
        return False
    if path == "/api/v1/display":
        return any(payload.get(k) for k in ("overlay", "effect"))
    return has_content(payload)


def target_app(cmds):
    """Which app the last visible command puts on the panel."""
    for method, path, _, _ in reversed(cmds):
        if path.startswith("/api/v1/apps/pushed/"):
            return path.rsplit("/", 1)[-1]
        if path.startswith("/api/v1/apps/script/"):
            return path.rsplit("/", 1)[-1]
    return None


# --------------------------------------------------------------------------- alt text

COLOR_NAMES = [
    ((255, 0, 0), "red"), ((0, 255, 0), "green"), ((0, 0, 255), "blue"),
    ((255, 255, 0), "yellow"), ((255, 136, 0), "orange"), ((255, 0, 255), "magenta"),
    ((0, 255, 255), "cyan"), ((255, 255, 255), "white"), ((128, 128, 128), "grey"),
    ((0, 0, 0), "black"), ((255, 105, 180), "pink"), ((128, 0, 128), "purple"),
]


def color_name(value):
    rgb = None
    if isinstance(value, str) and re.fullmatch(r"#?[0-9a-fA-F]{6}", value):
        v = int(value.lstrip("#"), 16)
        rgb = ((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)
    elif isinstance(value, list) and len(value) == 3 and all(isinstance(c, int) for c in value):
        rgb = tuple(value)
    if rgb is None:
        return None
    best = min(COLOR_NAMES, key=lambda c: sum((a - b) ** 2 for a, b in zip(c[0], rgb)))
    return best[1]


BERRY_NAME = re.compile(r"^#\s*@name\s+(.+?)\s*$", re.M)
BERRY_CLASS = re.compile(r"^class\s+(\w+)", re.M)
BERRY_DRAWCALL = re.compile(r"\b(scroll_text|text|ramp_text)\s*\(([^()]*)\)")


def berry_strings(source):
    """The literal strings a script draws, with their literal colours."""
    out = []
    for _, args in BERRY_DRAWCALL.findall(source):
        lit = re.search(r'"([^"]*)"', args)
        if not lit or not lit.group(1).strip():
            continue
        col = re.search(r"0x([0-9a-fA-F]{6})", args)
        name = None
        if col:
            v = int(col.group(1), 16)
            name = color_name("#%06X" % v)
        out.append((lit.group(1), name))
    return out


def describe_berry(source):
    drawn = berry_strings(source)
    if drawn:
        parts = ['"%s"%s' % (t, " in " + c if c else "") for t, c in drawn]
        return "The panel showing " + " and ".join(parts)
    name = BERRY_NAME.search(source) or BERRY_CLASS.search(source)
    if name:
        return "The panel showing the %s script" % name.group(1)
    return "The panel showing this script"


def describe(cmds):
    """A plain-language alt text, from whatever the block actually sends."""
    payload = {}
    for method, path, data, _ in cmds:
        if not is_visible((method, path, data, None)):
            continue
        if path.startswith("/api/v1/apps/script/"):
            return describe_berry(data or "")
        try:
            body = json.loads(data or "{}")
        except ValueError:
            continue
        if isinstance(body, list):
            frames = [f for f in body if isinstance(f, dict)]
            labels = [str(f["text"]) for f in frames if isinstance(f.get("text"), str)]
            if labels:
                return ("The panel cycling through %d frames: %s"
                        % (len(frames), ", ".join('"%s"' % t for t in labels)))
            payload = frames[0] if frames else {}
        elif isinstance(body, dict):
            payload = body
    if not payload:
        return "the AWTRIX panel showing this example"

    bits = []
    text = payload.get("text")
    if isinstance(text, list):
        parts = []
        for frag in text:
            if isinstance(frag, dict):
                name = color_name(frag.get("color")) or "white"
                parts.append('"%s" in %s' % (str(frag.get("text", "")).strip(), name))
        if parts:
            bits.append(" then ".join(parts))
    elif isinstance(text, str) and text:
        name = color_name(payload.get("textColor"))
        bits.append('"%s"%s' % (text, " in " + name if name else ""))

    if payload.get("icon"):
        bits.append("an icon on the left")
    if isinstance(payload.get("progress"), int) and payload["progress"] >= 0:
        bits.append("a %d%% progress bar along the bottom row" % payload["progress"])
    if payload.get("barChart"):
        bits.append("a bar chart of %d columns" % len(payload["barChart"]))
    if payload.get("lineChart"):
        bits.append("a line chart")
    if payload.get("draw"):
        bits.append("drawn shapes")
    if payload.get("effect"):
        bits.append("the %s effect behind it" % payload["effect"])
    if payload.get("overlay"):
        bits.append("a %s overlay" % payload["overlay"])
    if payload.get("backgroundColor"):
        name = color_name(payload["backgroundColor"])
        if name and name != "black":
            bits.append("a %s background" % name)

    if not bits:
        return "the AWTRIX panel showing this example"
    return "The panel showing " + ", ".join(bits)


# --------------------------------------------------------------------------- one page

def run_block(sim, cmds, directive):
    """Replay a block and hold the resulting app on the panel."""
    sim.reset()
    wanted = set()
    for _, _, data, _ in cmds:
        try:
            wanted |= icon_ids(json.loads(data or "{}"))
        except ValueError:
            pass
    seed_icons(os.path.join(sim.data_dir, "ICONS"), wanted)
    base = directive.get("base")
    if isinstance(base, str):
        sim.req("PUT", "/api/v1/apps/pushed/" + SHOT_APP, base)
        sim.pin(SHOT_APP)
    for method, path, data, ctype in cmds:
        status, body = sim.req(method, path, data, ctype)
        if status >= 400 and is_visible((method, path, data, ctype)):
            raise SimError("%s %s -> %d %s" % (method, path, status, body.strip()))
    name = target_app(cmds)
    if name and not any(c[1].startswith("/api/v1/notifications") for c in cmds):
        if not sim.pin(name):
            raise SimError("could not pin app %r" % name)
    elif base and not name:
        sim.pin(SHOT_APP)

    # The stage was blanked on purpose, so anything at all on the panel is this
    # example's doing. Nothing means the example did not run - a notification
    # that never surfaced, a script that failed to enter the loop - and that is
    # worth an error, not a picture of an empty panel.
    deadline = time.perf_counter() + 4
    while time.perf_counter() < deadline:
        if any(sim.screen()[2]):
            return
        time.sleep(0.05)
    raise SimError("nothing appeared on the panel")


def process(path, sim, dry_run=False, report=None):
    rel = os.path.relpath(path, ROOT).replace("\\", "/")
    page = os.path.splitext(os.path.basename(path))[0]
    with open(path, encoding="utf-8") as fh:
        text = fh.read()

    out_dir = os.path.join(SHOTS, page)
    edits = []
    used = set()
    made = 0

    for index, block in enumerate(blocks_of(text)):
        directive = block["directive"]
        if directive.get("skip"):
            continue
        cmds = commands_of(block)
        if not any(is_visible(c) for c in cmds):
            continue

        groups = [cmds]
        # Notifications queue rather than replace, so a block that posts two of
        # them shows the *first* on the panel and leaves the second waiting.
        # Photographing each on its own stage is the only honest reading, and
        # identical results are folded back into one picture below.
        notifies = [c for c in cmds if is_visible(c) and c[1].startswith("/api/v1/notifications")]
        if directive.get("each") or len(notifies) > 1:
            groups = [[c] for c in cmds if is_visible(c)]

        pieces = []
        captured = []
        for gi, group in enumerate(groups):
            base_id = directive.get("id") or slugify(alt_seed(group), "shot-%d" % index)
            shot_id = base_id if len(groups) == 1 else "%s-%d" % (base_id, gi + 1)
            n = 2
            while shot_id in used:
                shot_id = "%s-%d" % (base_id, n)
                n += 1
            used.add(shot_id)

            kept = block["alts"][gi] if gi < len(block["alts"]) else None
            alt = directive.get("alt") or kept or describe(group)
            if dry_run:
                print("  %-28s %s" % (shot_id, alt))
                pieces.append((shot_id, alt, ".png"))
                made += 1
                continue

            try:
                run_block(sim, group, directive)
                width, height, frames = capture(
                    sim,
                    int(directive.get("wait", DEFAULT_WAIT_MS)),
                    int(directive.get("window", DEFAULT_WINDOW_MS)))
            except SimError as exc:
                print("  !! %s block %d: %s" % (rel, index, exc))
                if report is not None:
                    report.append((rel, index, str(exc)))
                continue

            pixels = [f for f, _ in frames]
            if pixels in captured:
                used.discard(shot_id)
                continue                 # the docs show the same result twice
            captured.append(pixels)

            os.makedirs(out_dir, exist_ok=True)
            image = write_image(os.path.join(out_dir, shot_id), width, height, frames)
            kind = "gif" if image.endswith(".gif") else "png"
            print("  %-28s %-3s %2d frame(s)  %5d B" % (shot_id, kind, len(frames),
                                                        os.path.getsize(image)))
            pieces.append((shot_id, alt, os.path.splitext(image)[1]))
            made += 1

        if not pieces:
            continue

        # Splitting a block and then folding the duplicates back can leave a
        # single picture wearing a "-1" it no longer needs.
        if len(pieces) == 1 and len(groups) > 1 and not dry_run:
            shot_id, alt, ext = pieces[0]
            base = directive.get("id") or slugify(alt_seed(groups[0]), "shot-%d" % index)
            if base != shot_id and base not in used:
                os.replace(os.path.join(out_dir, shot_id + ext),
                           os.path.join(out_dir, base + ext))
                used.discard(shot_id)
                used.add(base)
                pieces = [(base, alt, ext)]

        digest = hashlib.sha256(block["body"].encode("utf-8")).hexdigest()[:8]
        lines = ["<!-- shot:begin id=%s hash=%s -->" % (pieces[0][0], digest)]
        for shot_id, alt, ext in pieces:
            lines.append("![%s](../assets/shots/%s/%s%s){ .shot }"
                         % (alt.replace("]", ")"), page, shot_id, ext))
        lines.append("<!-- shot:end -->")
        edits.append((block["existing"], block["insert_at"], "\n".join(lines)))

    if not edits:
        return 0

    for existing, insert_at, replacement in reversed(edits):
        if existing:
            text = text[:existing[0]] + replacement + text[existing[1]:]
        else:
            text = text[:insert_at] + replacement + "\n\n" + text[insert_at:]

    if not dry_run:
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    return made


def alt_seed(cmds):
    """A short name for the file, taken from the payload rather than the position."""
    for method, path, data, _ in reversed(cmds):
        if not is_visible((method, path, data, None)):
            continue
        if path.startswith("/api/v1/apps/script/"):
            name = BERRY_NAME.search(data or "") or BERRY_CLASS.search(data or "")
            return name.group(1) if name else "script"
        try:
            body = json.loads(data or "{}")
        except ValueError:
            continue
        if isinstance(body, list):
            frames = [f for f in body if isinstance(f, dict)]
            body = frames[0] if frames else {}
        if not isinstance(body, dict):
            continue
        text = body.get("text")
        if isinstance(text, list) and text:
            text = "".join(str(f.get("text", "")) for f in text if isinstance(f, dict))
        parts = []
        if isinstance(text, str) and text.strip():
            parts.append(text.strip())
        for key in ("effect", "overlay"):
            if body.get(key):
                parts.append(str(body[key]))
        # An icon is a short name on disk, but it can also be a whole base64
        # bitmap - which makes a terrible file name.
        icon = body.get("icon")
        if isinstance(icon, str) and 0 < len(icon) <= 16:
            parts.append(icon)
        elif icon:
            parts.append("icon")
        for key in ("progress", "barChart", "lineChart", "draw"):
            if key in body:
                parts.append(key)
        if parts:
            return "-".join(parts)
    return ""


# --------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--only", action="append", default=[],
                    help="page name without .md, repeatable")
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--data", default=os.path.join(ROOT, ".pio", "docshots-data"))
    ap.add_argument("--binary", default=SIM)
    ap.add_argument("--dry-run", action="store_true",
                    help="list the shots that would be taken, touch nothing")
    args = ap.parse_args()

    pages = sorted(f for f in os.listdir(GUIDES) if f.endswith(".md"))
    if args.only:
        wanted = {n if n.endswith(".md") else n + ".md" for n in args.only}
        pages = [p for p in pages if p in wanted]
        if not pages:
            sys.exit("gen_docshots: no such page(s): %s" % ", ".join(sorted(args.only)))

    sim = None
    report = []
    total = 0
    try:
        if not args.dry_run:
            sim = Sim(args.port, args.data, args.binary)
            sim.start()
        for name in pages:
            print(name)
            total += process(os.path.join(GUIDES, name), sim,
                             dry_run=args.dry_run, report=report)
    finally:
        if sim:
            sim.stop()

    print("\n%d shot(s)" % total)
    if report:
        print("%d block(s) failed:" % len(report))
        for rel, index, err in report:
            print("  %s block %d: %s" % (rel, index, err))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
