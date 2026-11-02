#!/usr/bin/env python3
import argparse
import ctypes
import math
import os
import sys

import numpy as np

DESCRIPTION = """Writes src/platform/tc002/wake/WakeFrontendTables.h: the microfrontend tables the original computes
in float at start-up (Hann window, mel filterbank, PCAN gain).

Two of them hang on the last bit of a libm result (the top channel ends exactly on 7500 Hz), so they
are data here, taken from the libm the trainer's pymicro_features ran on: glibc before 2.41 on x86-64,
whose cosf, log1pf and powf this script calls through ctypes while numpy float32 does the rest step for
step like the C code (tensorflow/lite/experimental/microfrontend/lib/*_util.cc).
"""

f32 = np.float32
libm = ctypes.CDLL("libm.so.6")
for name, args in (("cosf", 1), ("log1pf", 1), ("powf", 2)):
    getattr(libm, name).restype = ctypes.c_float
    getattr(libm, name).argtypes = [ctypes.c_float] * args


def cosf(x):
    return f32(libm.cosf(float(x)))


def log1pf(x):
    return f32(libm.log1pf(float(x)))


def powf(x, y):
    return f32(libm.powf(float(x), float(y)))


def window(size=480, bits=12):
    arg = f32(f32(math.pi) * f32(2.0)) / f32(size)
    out = []
    for i in range(size):
        value = f32(0.5) - f32(0.5) * cosf(arg * (f32(i) + f32(0.5)))
        out.append(int(np.floor(value * f32(1 << bits) + f32(0.5))))
    return out


def filterbank(channels=40, rate=16000, spectrum=257, lower=125.0, upper=7500.0, bits=12):
    def mel(freq):
        return f32(1127.0) * log1pf(freq / f32(700.0))

    count = channels + 1
    mel_low = mel(f32(lower))
    spacing = (mel(f32(upper)) - mel_low) / f32(count)
    centers = [mel_low + spacing * f32(i + 1) for i in range(count)]
    hz = f32(0.5) * f32(rate) / (f32(spectrum) - f32(1))
    start_index = int(f32(1.5) + f32(lower) / hz)
    starts, weight_starts, widths, actual = [0] * count, [0] * count, [0] * count, []
    freq_start, weight_start, needs_zeros = start_index, 0, False
    for chan in range(count):
        freq = freq_start
        while mel(f32(freq) * hz) <= centers[chan]:
            freq += 1
        width = freq - freq_start
        actual.append((freq_start, width))
        if width == 0:
            starts[chan], weight_starts[chan], widths[chan] = 0, 0, 4
            if not needs_zeros:
                needs_zeros = True
                for j in range(chan):
                    weight_starts[j] += 4
                weight_start += 4
        else:
            aligned = (freq_start // 2) * 2
            padded = ((freq_start - aligned + width - 1) // 4 + 1) * 4
            starts[chan], weight_starts[chan], widths[chan] = aligned, weight_start, padded
            weight_start += padded
        freq_start = freq
    weights, unweights = [0] * weight_start, [0] * weight_start
    end_index = 0
    for chan in range(count):
        frequency, width = actual[chan]
        offset = frequency - starts[chan]
        denominator = mel_low if chan == 0 else centers[chan - 1]
        for j in range(width):
            weight = (centers[chan] - mel(f32(frequency) * hz)) / (centers[chan] - denominator)
            index = weight_starts[chan] + offset + j
            weights[index] = int(np.floor(weight * f32(1 << bits) + f32(0.5)))
            unweights[index] = int(np.floor((f32(1.0) - weight) * f32(1 << bits) + f32(0.5)))
            frequency += 1
        end_index = max(end_index, frequency)
    return start_index, end_index, starts, weight_starts, widths, weights, unweights


def pcan(input_bits=7, gain_bits=21, offset=80.0, strength=0.95):
    def gain(x):
        value = f32(x) / f32(1 << input_bits)
        g = f32(1 << gain_bits) * powf(value + f32(offset), -f32(strength))
        return 0x7FFF if g > 0x7FFF else int(g + f32(0.5))

    lut = [0] * 125
    lut[0], lut[1] = gain(0), gain(1)
    for interval in range(2, 33):
        x0 = 1 << (interval - 1)
        x1 = x0 + (x0 >> 1)
        x2 = x0 + (x0 - 1) if interval == 32 else 2 * x0
        y0, y1, y2 = gain(x0), gain(x1), gain(x2)
        a1 = 4 * (y1 - y0) - (y2 - y0)
        a2 = (y2 - y0) - a1
        lut[4 * interval - 6] = y0
        lut[4 * interval - 5] = (a1 + 0x8000) % 0x10000 - 0x8000
        lut[4 * interval - 4] = (a2 + 0x8000) % 0x10000 - 0x8000
    return lut


def array(name, kind, values, per_line=16):
    lines = [f"constexpr {kind} {name}[] = {{"]
    for i in range(0, len(values), per_line):
        lines.append("  " + ", ".join(str(v) for v in values[i:i + per_line]) + ",")
    lines.append("};")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=DESCRIPTION, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output", help="the header to write")
    output = parser.parse_args().output
    libc = os.confstr("CS_GNU_LIBC_VERSION") or ""
    major, minor = (int(p) for p in libc.split()[-1].split(".")[:2]) if libc.startswith("glibc") else (0, 0)
    if (major, minor) >= (2, 41) or (major, minor) == (0, 0):
        sys.exit(f"needs glibc before 2.41 as the trainer had, not '{libc}'")
    start, end, starts, weight_starts, widths, weights, unweights = filterbank()
    text = "\n\n".join([
        "#pragma once\n\n// Generated by tools/tc002/wake/frontend_tables.py from " + libc + "; do not edit.\n\n"
        "#include <cstdint>\n\nnamespace awtrix::tc002::wake::tables {",
        array("kWindow", "int16_t", window()),
        f"constexpr int kStartIndex = {start};\nconstexpr int kEndIndex = {end};",
        array("kChannelStarts", "int16_t", starts),
        array("kWeightStarts", "int16_t", weight_starts),
        array("kWidths", "int16_t", widths),
        array("kWeights", "int16_t", weights),
        array("kUnweights", "int16_t", unweights),
        array("kGainLut", "int16_t", pcan()),
        "}\n",
    ])
    with open(output, "w", encoding="utf-8", newline="\n") as out:
        out.write(text)


if __name__ == "__main__":
    main()
