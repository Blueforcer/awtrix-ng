#!/usr/bin/env python3
"""Sample real native_sim effects; stdlib-only PNG strips and Markdown/JSON tables.

Example: python tools/sim/effect_survey.py --size 32x8 --size 53x11 --out .pio/survey
Samples are wall-clock observations, not golden images or a complete animation cycle.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import zlib


def png(path, frames, width, height, scale):
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    rows = bytearray()
    for y in range(height):
        row = bytearray(b'\0')
        for frame in frames:
            for pixel in frame[y * width:(y + 1) * width]:
                row.extend(bytes(((pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255)) * scale)
        rows.extend(row * scale)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width * len(frames) * scale,
        height * scale, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def survey(args, width, height):
    out = args.out / f'{width}x{height}'
    out.mkdir(parents=True, exist_ok=True)
    base = f'http://127.0.0.1:{args.port}'

    def http(path, body=None, method=None):
        request = urllib.request.Request(base + '/api/v1/' + path,
            data=None if body is None else json.dumps(body).encode(),
            headers={'Content-Type': 'application/json'}, method=method)
        with urllib.request.urlopen(request, timeout=3) as response:
            return json.load(response)

    report = []
    with tempfile.TemporaryDirectory(prefix='awtrix-effects-') as data:
        Path(data, 'device.json').write_text(json.dumps({'panelWidth': width, 'panelHeight': height,
            'panels': 1, 'mqttEnabled': False, 'scriptingEnabled': False}))
        with (out / 'simulator.log').open('w') as log:
            sim = subprocess.Popen([str(args.program.resolve()), '--no-matrix', '--port', str(args.port),
                                    '--data', data], stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 30
                while True:
                    if sim.poll() is not None:
                        raise RuntimeError(f'simulator exited: see {out}/simulator.log')
                    try:
                        effects = http('capabilities')['effects']
                        break
                    except (urllib.error.URLError, TimeoutError):
                        if time.monotonic() >= deadline:
                            raise RuntimeError('simulator readiness timeout')
                        time.sleep(.1)
                for effect in effects:
                    http('notifications/active', method='DELETE')
                    http('notifications', {'name': 'effect-survey', 'text': '', 'hold': True,
                                    'effect': effect})
                    time.sleep(args.settle)
                    frames = []
                    for _ in range(args.frames):
                        screen = http('display/screen')
                        assert (screen['width'], screen['height']) == (width, height), screen
                        assert len(screen['pixels']) == width * height
                        frames.append(tuple(screen['pixels']))
                        time.sleep(args.interval)
                    lit = [y for y in range(height) if any(any(f[y*width:(y+1)*width]) for f in frames)]
                    changing = [y for y in range(height) if len({f[y*width:(y+1)*width] for f in frames}) > 1]
                    name = re.sub(r'[^A-Za-z0-9_-]', '_', effect)
                    png(out / (name + '.png'), frames, width, height, args.scale)
                    report.append({'effect': effect, 'distinct': len(set(frames)), 'lit': lit, 'changing': changing})
            finally:
                sim.terminate()
                try:
                    sim.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    sim.kill()
                    sim.wait()
    table = '| Effect | Distinct | Lit rows (0-based) | Changing rows |\n|---|---:|---|---|\n'
    for row in report:
        table += f"| {row['effect']} | {row['distinct']} | {row['lit']} | {row['changing']} |\n"
    (out / 'summary.md').write_text(table)
    (out / 'summary.json').write_text(json.dumps({'size': [width, height], 'frames': args.frames,
        'interval': args.interval, 'settle': args.settle, 'effects': report}, indent=2) + '\n')
    print(f'\n{width}x{height}\n{table}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', type=Path, default=Path('.pio/build/native_sim/program'))
    parser.add_argument('--out', type=Path, default=Path('.pio/effect-survey'))
    parser.add_argument('--size', action='append', default=[])
    parser.add_argument('--frames', type=int, default=12)
    parser.add_argument('--interval', type=float, default=.25)
    parser.add_argument('--settle', type=float, default=.5)
    parser.add_argument('--scale', type=int, default=4)
    parser.add_argument('--port', type=int, default=18091)
    args = parser.parse_args()
    if args.frames < 2 or args.scale < 1 or args.interval <= 0 or args.settle < 0:
        parser.error('frames >= 2, scale >= 1, interval > 0 and settle >= 0 required')
    for size in args.size or ['32x8', '53x11']:
        if not re.fullmatch(r'[1-9][0-9]*x[1-9][0-9]*', size):
            parser.error('size must be WIDTHxHEIGHT')
        survey(args, *map(int, size.split('x')))


if __name__ == '__main__':
    main()
