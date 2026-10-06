#!/usr/bin/env python3
"""Write a mono s16le 44.1 kHz sine tone for /dev/awtrix_pcm."""

import argparse
import hashlib
import math
import struct

RATE = 44100
MIU_WORD_SAMPLES = 8


def tone(hz, ms, dbfs, fade_ms):
    frames = round(RATE * ms / 1000)
    padded = -(-frames // MIU_WORD_SAMPLES) * MIU_WORD_SAMPLES
    amplitude = 32767 * 10 ** (dbfs / 20)
    fade = max(1, round(RATE * fade_ms / 1000))
    samples = []
    for i in range(padded):
        if i >= frames:
            samples.append(0)
            continue
        envelope = min(1.0, i / fade, (frames - 1 - i) / fade)
        samples.append(round(amplitude * envelope * math.sin(2 * math.pi * hz * i / RATE)))
    return struct.pack("<%dh" % len(samples), *samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("--hz", type=float, default=440.0)
    parser.add_argument("--ms", type=int, default=300)
    parser.add_argument("--dbfs", type=float, default=-12.0)
    parser.add_argument("--fade-ms", type=int, default=10)
    args = parser.parse_args()
    pcm = tone(args.hz, args.ms, args.dbfs, args.fade_ms)
    with open(args.output, "wb") as out:
        out.write(pcm)
    print("%s %d bytes sha256 %s" % (args.output, len(pcm), hashlib.sha256(pcm).hexdigest()))


if __name__ == "__main__":
    main()
