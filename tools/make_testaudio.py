"""Test signal for the AWTRIX audio visualizer: latency clicks, a frequency staircase,
a log sweep and a drum pattern. Writes testaudio.wav and testaudio.mp3 (via ffmpeg) into
the current directory; upload with `curl -X POST http://<awtrix-ip>/api/v1/audio/mp3 -F
"file=@testaudio.mp3"` and play with `{"mp3":"testaudio"}`."""

import os
import subprocess
import sys

import numpy as np

SR = 44100
OUT = os.getcwd()


def seconds(n):
    return int(n * SR)


def fade(x, ms=10):
    n = min(len(x), seconds(ms / 1000))
    if n == 0:
        return x
    ramp = np.linspace(0, 1, n)
    x[:n] *= ramp
    x[-n:] *= ramp[::-1]
    return x


def sine(freq, dur, amp):
    t = np.arange(seconds(dur)) / SR
    return fade(amp * np.sin(2 * np.pi * freq * t))


def silence(dur):
    return np.zeros(seconds(dur))


def kick(dur=0.25, amp=0.9):
    t = np.arange(seconds(dur)) / SR
    f = 55 + 90 * np.exp(-t * 35)
    phase = 2 * np.pi * np.cumsum(f) / SR
    return amp * np.exp(-t * 14) * np.sin(phase)


def click(dur=0.006, amp=0.8):
    t = np.arange(seconds(dur)) / SR
    return amp * np.exp(-t * 600) * np.sin(2 * np.pi * 3500 * t)


def noise_burst(dur, amp, decay):
    rng = np.random.default_rng(7)
    t = np.arange(seconds(dur)) / SR
    return amp * np.exp(-t * decay) * rng.uniform(-1, 1, len(t))


def place(buf, start, x):
    s = seconds(start)
    e = min(len(buf), s + len(x))
    buf[s:e] += x[: e - s]


def part_clicks():
    """20 s at 120 BPM: a bass thump plus a sharp click every 500 ms, silence in between.
    The panel's beat flash against the audible click is the latency check."""
    buf = silence(20)
    for i in range(40):
        place(buf, i * 0.5, kick(0.12, 0.9))
        place(buf, i * 0.5, click())
    return buf


STEPS = [50, 80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 12500]


def part_steps():
    """Third-octave tones, 1.5 s each with 0.5 s gaps: which band lights for which frequency."""
    out = []
    for f in STEPS:
        out.append(sine(f, 1.5, 0.25))
        out.append(silence(0.5))
    return np.concatenate(out)


def part_sweep():
    """10 s log sweep 40 Hz -> 16 kHz: the bar should walk from left to right."""
    n = seconds(10)
    t = np.arange(n) / SR
    f0, f1 = 40.0, 16000.0
    k = np.log(f1 / f0) / 10.0
    phase = 2 * np.pi * f0 * (np.exp(k * t) - 1) / k
    return fade(0.25 * np.sin(phase), 50)


def part_drums():
    """18 s at 128 BPM: kick on 1 and 3, snare on 2 and 4, hats on the eighths, a bass line."""
    beat = 60 / 128
    bars = 9
    buf = silence(bars * 4 * beat + 0.5)
    for bar in range(bars):
        for b in range(4):
            t0 = (bar * 4 + b) * beat
            if b in (0, 2):
                place(buf, t0, kick())
            else:
                place(buf, t0, noise_burst(0.18, 0.5, 25))
            place(buf, t0, noise_burst(0.05, 0.18, 90))
            place(buf, t0 + beat / 2, noise_burst(0.04, 0.12, 110))
        for step in range(8):
            f = [55, 55, 82.4, 55, 73.4, 55, 65.4, 55][step]
            place(buf, bar * 4 * beat + step * beat / 2, sine(f, beat / 2 * 0.9, 0.3))
    return buf


def main():
    parts = [
        silence(1.0),
        part_clicks(),
        silence(1.0),
        part_steps(),
        silence(1.0),
        part_sweep(),
        silence(1.0),
        part_drums(),
        silence(1.0),
    ]
    mono = np.concatenate(parts)
    peak = np.max(np.abs(mono))
    if peak > 0.95:
        mono = mono * (0.95 / peak)
    stereo = np.stack([mono, mono], axis=1)
    pcm = (stereo * 32767).astype(np.int16)

    wav = os.path.join(OUT, "testaudio.wav")
    mp3 = os.path.join(OUT, "testaudio.mp3")
    import wave

    with wave.open(wav, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())

    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", wav, "-codec:a", "libmp3lame",
         "-b:a", "128k", "-ar", str(SR), mp3],
        check=True,
    )
    total = len(mono) / SR
    print("wrote %s (%.1f s, %d KB)" % (mp3, total, os.path.getsize(mp3) // 1024))
    t = 1.0
    print("  %5.1f s  clicks at 120 BPM (latency)" % t)
    t += 21.0
    print("  %5.1f s  tone steps: %s Hz" % (t, ", ".join(str(f) for f in STEPS)))
    t += len(STEPS) * 2.0 + 1.0
    print("  %5.1f s  sweep 40 Hz -> 16 kHz" % t)
    t += 11.0
    print("  %5.1f s  drums at 128 BPM" % t)


if __name__ == "__main__":
    sys.exit(main())
