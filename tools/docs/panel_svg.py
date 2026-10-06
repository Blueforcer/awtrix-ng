"""SVG pictures of the display: style "led" (round LEDs) and "diagram" (grid with rulers).

A picture is self-contained, with a dark bezel and fixed colours, so it reads the same in the
light and the dark docs theme. Repeated frames are merged; several frames become a CSS
animation, and a reader who asks for reduced motion sees the first frame. Pure Python.
"""

import hashlib

CELL = 10
BEZEL = "#0b0b0b"
UNLIT_LED = "#1c1c1c"
UNLIT_CELL = "#191919"
GRID = "#0b0b0b"
RULER = "#8a8a8a"
MARK = "#378ADD"
COLS = "#1D9E75"
BOXES = ("#EF9F27", "#7F77DD", "#5DCAA5", "#ED93B1")


def frame_hash(pixels):
    return hashlib.sha256(",".join(map(str, pixels)).encode("ascii")).hexdigest()[:16]


def collapse(frames, frame_ms):
    out = []
    for pixels in frames:
        if out and out[-1][0] == pixels:
            out[-1][1] += frame_ms
        else:
            out.append([pixels, frame_ms])
    return [(pixels, ms) for pixels, ms in out]


def render(frames, width, height, style="led", frame_ms=70, mark=None, cols=None, boxes=()):
    if not frames:
        raise ValueError("no frames to draw")
    diagram = style == "diagram"
    left, top, pad = (16, 12, 4) if diagram else (6, 6, 6)
    w, h = left + width * CELL + pad, top + height * CELL + pad
    parts = ['<rect width="%d" height="%d" rx="6" fill="%s"/>' % (w, h, BEZEL)]
    if diagram:
        parts.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s"/>'
                     % (left, top, width * CELL, height * CELL, UNLIT_CELL))
        parts.append(_bands(width, height, left, top, mark, cols))

        def draw(pixels):
            return _cells(pixels, width, height, left, top)
    else:
        parts.append(_dots([(x, y) for y in range(height) for x in range(width)],
                           left, top, UNLIT_LED))

        def draw(pixels):
            return _leds(pixels, width, height, left, top)
    shown = collapse(frames, frame_ms)
    parts.append(draw(shown[0][0]) if len(shown) == 1 else _animation(shown, draw))
    if diagram:
        parts.append(_grid(width, height, left, top))
        parts.append(_rulers(width, height, left, top, mark, cols))
        parts.append(_boxes(boxes, left, top))
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d"'
            ' data-frame0="%s">%s</svg>\n' % (w, h, w, h, frame_hash(frames[0]), "".join(parts)))


def _runs(pixels, width, height):
    runs = {}
    for y in range(height):
        x = 0
        while x < width:
            color = pixels[y * width + x]
            start = x
            while x + 1 < width and pixels[y * width + x + 1] == color:
                x += 1
            if color:
                runs.setdefault(color, []).append((start, y, x - start + 1))
            x += 1
    return runs


def _cells(pixels, width, height, left, top):
    out = []
    for color, runs in sorted(_runs(pixels, width, height).items()):
        d = "".join("M%d %dh%dv%dh-%dz" % (left + x * CELL, top + y * CELL, n * CELL, CELL, n * CELL)
                    for x, y, n in runs)
        out.append('<path d="%s" fill="#%06X"/>' % (d, color))
    return "".join(out)


def _leds(pixels, width, height, left, top):
    out = []
    for color, runs in sorted(_runs(pixels, width, height).items()):
        points = [(x, y) for x0, y, n in runs for x in range(x0, x0 + n)]
        out.append(_dots(points, left, top, "#%06X" % color))
    return "".join(out)


def _dots(points, left, top, color):
    d = "".join("M%g %gh0" % (left + x * CELL + CELL / 2, top + y * CELL + CELL / 2)
                for x, y in points)
    return ('<path d="%s" stroke="%s" stroke-width="%g" stroke-linecap="round" fill="none"/>'
            % (d, color, CELL * 0.72))


def _grid(width, height, left, top):
    d = "".join("M%d %dv%d" % (left + x * CELL, top, height * CELL) for x in range(1, width))
    d += "".join("M%d %dh%d" % (left, top + y * CELL, width * CELL) for y in range(1, height))
    return '<path d="%s" stroke="%s" stroke-width="1" fill="none"/>' % (d, GRID)


def _bands(width, height, left, top, mark, cols):
    out = []
    if cols:
        first, last = cols
        out.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s" opacity="0.4"/>'
                   % (left + first * CELL, top, (last - first + 1) * CELL, height * CELL, COLS))
    if mark:
        axis, n = mark
        if axis == "row":
            box = (left, top + n * CELL, width * CELL, CELL)
        else:
            box = (left + n * CELL, top, CELL, height * CELL)
        out.append('<rect x="%d" y="%d" width="%d" height="%d" fill="%s" opacity="0.5"/>'
                   % (box + (MARK,)))
    return "".join(out)


def _label(x, y, text, anchor):
    return ('<text x="%g" y="%g" font-size="6" font-family="ui-monospace,Menlo,Consolas,monospace"'
            ' fill="%s" text-anchor="%s">%s</text>' % (x, y, RULER, anchor, text))


def _rulers(width, height, left, top, mark, cols):
    columns = {0, width - 1} | set(range(8, width - 1, 8))
    if cols:
        columns |= set(cols)
    if mark and mark[0] == "col":
        columns.add(mark[1])
    out = [_label(left + x * CELL + CELL / 2, top - 3, x, "middle")
           for x in sorted(c for c in columns if 0 <= c < width)]
    out += [_label(left - 3, top + y * CELL + CELL / 2 + 2, y, "end") for y in range(height)]
    return "".join(out)


def _boxes(boxes, left, top):
    return "".join(
        '<rect x="%g" y="%g" width="%g" height="%g" fill="none" stroke="%s" stroke-width="1.5"'
        ' stroke-dasharray="4 2"/>' % (left + x * CELL + 0.75, top + y * CELL + 0.75,
                                      w * CELL - 1.5, h * CELL - 1.5, BOXES[i % len(BOXES)])
        for i, (x, y, w, h) in enumerate(boxes))


def _animation(shown, draw):
    total = sum(ms for _, ms in shown)
    css = [".f{visibility:hidden}"]
    names = {}
    groups = []
    start = 0
    for i, (pixels, ms) in enumerate(shown):
        share = "%.3f" % (100.0 * ms / total)
        if share not in names:
            names[share] = "k%d" % len(names)
            css.append("@keyframes %s{0%%{visibility:visible}%s%%{visibility:hidden}}"
                       % (names[share], share))
        css.append(".f%d{animation:%s %.3fs %.3fs infinite}"
                   % (i, names[share], total / 1000.0, start / 1000.0))
        groups.append('<g class="f f%d">%s</g>' % (i, draw(pixels)))
        start += ms
    css.append("@media (prefers-reduced-motion:reduce){.f{animation:none}.f0{visibility:visible}}")
    return "<style>%s</style>%s" % ("".join(css), "".join(groups))
