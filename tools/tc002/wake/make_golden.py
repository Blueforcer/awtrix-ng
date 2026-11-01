#!/usr/bin/env python3
"""Golden fixtures for the TC002 wake-word engine (tests/tc002/runtime/test_wake.cpp).

The references are the ones the microWakeWord trainer uses: pymicro_features for the features and
LiteRT's reference kernels (OpResolverType.BUILTIN_REF) for the model. The audio is synthetic and
integer-only, so the test regenerates it bit for bit instead of storing it:

  signal.features.bin  uint16 LE [frames][40], pymicro_features over synthetic()
  <name>.signal.bin    the output bytes of each invoke, the model over the signal's features quantised
                       the trainer's way
  <name>.random.bin    the same over random_frames() int8 frames
  zoo.tflite           with --zoo, the op coverage model of zoo_model.py, checked like the others
  clip<k>.pcm, clip<k>.features.bin, <name>.clip<k>.bin  the same for each --clip, not for the repo

Each stream starts on a fresh interpreter. Run in a scratch venv:
  pip install numpy pymicro-features ai-edge-litert flatbuffers soundfile
  python3 make_golden.py --out tests/tc002/runtime/wake --zoo --model smoke=tests/tc002/runtime/wake/smoke.tflite
"""

import argparse
import sys
from pathlib import Path

import numpy as np

RATE = 16000
CHANNELS = 40
STEP = 160
SIGNAL_SECONDS = 4
RANDOM_FRAMES = 800


class XorShift:
    def __init__(self, seed):
        self.state = seed

    def next(self):
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x


def synthetic(seconds=SIGNAL_SECONDS):
    rng = XorShift(0x12345678)
    count = seconds * RATE
    out = np.zeros(count, dtype=np.int16)
    phase = 0
    chirp_start, chirp_samples = 3 * RATE // 2, 3 * RATE // 2
    inc0 = 100 * (1 << 32) // RATE
    slope = (7000 * (1 << 32) // RATE - inc0) // chirp_samples
    for n in range(count):
        noise = (rng.next() >> 16) - 32768
        i = n % (SIGNAL_SECONDS * RATE)
        if i < RATE // 2:
            value = 0
        elif i < chirp_start:
            value = (noise * ((i - RATE // 2) * 2048 // RATE)) >> 12
        elif i < chirp_start + chirp_samples:
            phase = (phase + inc0 + (i - chirp_start) * slope) & 0xFFFFFFFF
            p = phase >> 16
            half = p & 32767
            y = (half * (32768 - half)) >> 13
            tone = (y * 12000) >> 15
            value = (tone if p < 32768 else -tone) + (noise >> 6)
        elif i < 3 * RATE + 2 * RATE // 5:
            value = 0
        else:
            value = (32767 if (i // 40) % 2 else -32768) if (i // 1600) % 2 == 0 else noise >> 4
        out[n] = value
    return out


def random_frames(count=RANDOM_FRAMES):
    rng = XorShift(0x9E3779B9)
    data = [((rng.next() >> 24) ^ 0x80) - 128 for _ in range(count * CHANNELS)]
    return np.asarray(data, dtype=np.int8).reshape(count, CHANNELS)


def frontend_features(pcm):
    from pymicro_features import MicroFrontend

    data = pcm.astype("<i2").tobytes()
    frontend = MicroFrontend()
    frames, idx = [], 0
    while idx + STEP * 2 <= len(data):
        out = frontend.process_samples(data[idx:idx + STEP * 2])
        idx += out.samples_read * 2 or STEP * 2
        if out.features:
            values = [int(round(f / 0.0390625)) for f in out.features]
            assert all(v * 0.0390625 == f for v, f in zip(values, out.features))
            frames.append(values)
    return np.asarray(frames, dtype=np.uint16).reshape(-1, CHANNELS)


def trainer_input(values, scale, zero):
    feats = np.clip(np.round(values.astype(np.float32) * np.float32(0.0390625)), -128, 127).astype(np.int8)
    feats = feats.astype(np.float32)
    q = np.round(feats / scale) + zero
    return np.clip(q, -128, 127).astype(np.int8)


def check_trainer_rounding(scale, zero):
    every = np.arange(65536, dtype=np.uint16)
    rounded = np.clip(np.round(every.astype(np.float32) * np.float32(0.0390625)), -128, 127).astype(np.int64)
    scaled = every.astype(np.int64) * 5
    whole = scaled >> 7
    rest = scaled & 127
    mine = np.minimum(whole + ((rest > 64) | ((rest == 64) & ((whole & 1) == 1))), 127)
    assert np.array_equal(rounded, mine), "trainerFeature() differs from the trainer"
    k = np.arange(128, dtype=np.float32)
    single = np.round(k / np.float32(scale)) + zero
    assert np.array_equal(single, np.round(k / scale) + zero), "numpy no longer divides in float32"
    double = np.round(k.astype(np.float64) / scale) + zero
    ties = np.nonzero(single != double)[0]
    print(f"  input scale {scale}: float32 rounds feature values {list(ties)} up where float64 would not")


def run_model(path, frames):
    from ai_edge_litert.interpreter import Interpreter, OpResolverType

    results = {}
    for name, kind in (("ref", OpResolverType.BUILTIN_REF),
                       ("builtin", OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES),
                       ("auto", OpResolverType.AUTO)):
        interpreter = Interpreter(model_path=str(path), experimental_op_resolver_type=kind)
        interpreter.allocate_tensors()
        inp = interpreter.get_input_details()[0]
        out = interpreter.get_output_details()[0]
        stride = int(inp["shape"][1])
        values = []
        for start in range(0, len(frames) - stride + 1, stride):
            interpreter.set_tensor(inp["index"], frames[start:start + stride][None])
            interpreter.invoke()
            values.append(interpreter.get_tensor(out["index"]).reshape(-1).view(np.uint8).copy())
        results[name] = np.asarray(values, dtype=np.uint8)
    for name in ("builtin", "auto"):
        diff = int((results[name] != results["ref"]).any(axis=1).sum())
        print(f"  {path.name}: {name} differs from ref in {diff}/{len(results['ref'])} invokes")
    unique = len(np.unique(results["ref"]))
    print(f"  {path.name}: {len(results['ref'])} invokes, {unique} distinct output values")
    return results["ref"]


def input_quantization(path):
    from ai_edge_litert.interpreter import Interpreter, OpResolverType

    interpreter = Interpreter(model_path=str(path), experimental_op_resolver_type=OpResolverType.BUILTIN_REF)
    scale, zero = interpreter.get_input_details()[0]["quantization"]
    return scale, zero


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--model", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--seconds", type=int, default=SIGNAL_SECONDS)
    parser.add_argument("--zoo", action="store_true", help="also write and check zoo.tflite")
    parser.add_argument("--clip", type=Path, action="append", default=[],
                        help="a 16 kHz mono WAV, for checks outside the repo")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    values = frontend_features(synthetic(args.seconds))
    values.astype("<u2").tofile(args.out / "signal.features.bin")
    print(f"signal: {len(values)} frames")

    clips = []
    for k, path in enumerate(args.clip):
        import soundfile

        wav, rate = soundfile.read(str(path), dtype="float32", always_2d=False)
        if rate != RATE or wav.ndim != 1:
            sys.exit("the clip must be 16 kHz mono")
        pcm = np.clip(wav * 32768.0, -32768.0, 32767.0).astype(np.int16)
        pcm.astype("<i2").tofile(args.out / f"clip{k}.pcm")
        clips.append(frontend_features(pcm))
        clips[-1].astype("<u2").tofile(args.out / f"clip{k}.features.bin")
        print(f"clip{k}: {len(clips[-1])} frames")

    if args.zoo:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from zoo_model import zoo_model

        (args.out / "zoo.tflite").write_bytes(zoo_model())
        args.model.append(f"zoo={args.out / 'zoo.tflite'}")
    for spec in args.model:
        name, _, path = spec.partition("=")
        path = Path(path)
        scale, zero = input_quantization(path)
        check_trainer_rounding(scale, zero)
        run_model(path, trainer_input(values, scale, zero)).tofile(args.out / f"{name}.signal.bin")
        run_model(path, random_frames()).tofile(args.out / f"{name}.random.bin")
        for k, clip in enumerate(clips):
            outputs = run_model(path, trainer_input(clip, scale, zero))
            outputs.tofile(args.out / f"{name}.clip{k}.bin")
            print(f"  {name}.clip{k}.bin: peak {int(outputs.max())}")


if __name__ == "__main__":
    main()
