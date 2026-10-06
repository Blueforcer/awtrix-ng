#!/usr/bin/env python3
"""Writes the voice fixture of the voice tests: a small ATTS v1 voice with seeded random weights
and what the model's reference runtime makes of a few utterances.

    python tools/speech/make_voice_fixture.py --model DIR [--out tests/tc002/runtime/speech]

DIR is the model's source tree with tts/atts.py (the format) and tts/runtime.py (the executable
specification of the arithmetic). The voice has a reduced configuration and no training data in it.
Every utterance is split into chunks as the firmware splits it; each chunk starts with BOS. The
reference runs one chunk at a time with its durations and pitch predicted, and the DC blocker
runs over the whole utterance.

voice.expect, little-endian: u32 utterances, then per utterance u32 chunks, per chunk u32 tokens,
u16 codes[tokens], u8 durations[tokens], f32 pitch[tokens], u32 frames, f32 mel[frames][n_mels],
and per utterance u32 samples, i16 pcm[samples].
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np

# Token codes (contract v1, BOS first) of each chunk, as the firmware frontend reads the text.
UTTERANCES = (
    ("Hello, world.", ((3584, 33, 10, 37, 335, 512, 39, 75, 37, 276, 1024),)),
    ("It is twelve o'clock.", ((3584, 68, 275, 68, 286, 19, 39, 67, 37, 282, 10, 21, 37, 66, 277, 1024),)),
    ("Did you feed the cat?", ((3584, 20, 68, 276, 40, 328, 25, 69, 276, 28, 266, 21, 65, 275, 1536),)),
    ("The timer is done. Your tea is ready!", ((3584, 28, 266, 19, 76, 34, 267, 68, 286, 20, 73, 291, 1024),
                                               (3584, 40, 70, 294, 19, 325, 68, 286, 38, 67, 20, 261, 2048))),
)
CONFIG = dict(d_enc=32, enc_kernel=5, enc_blocks=3, enc_ratio=2, d_pred=16, d_dec=32, dec_kernel=7, dec_blocks=2,
              dec_ratio=2, voc_dim=32, voc_in_kernel=3, voc_kernel=7, voc_blocks=2, voc_ratio=2, max_duration=255)
DILATIONS = (1, 2, 4)
SEED = 20261002


def random_voice(path, atts, keys):
    rng = np.random.default_rng(SEED)
    c = CONFIG

    def normal(*shape, scale=1.0):
        return (rng.standard_normal(shape) * scale).astype(np.float32)

    def dense(name, rows, cols, bias=0.0, spread=0.1, gain=1.0):
        w.dense(name, normal(rows, cols, scale=gain / np.sqrt(cols)), bias + normal(rows, scale=spread))

    def norm(name, width):
        w.f32(name + ".g", rng.uniform(0.5, 1.5, width).astype(np.float32))
        w.f32(name + ".b", rng.uniform(-0.2, 0.2, width).astype(np.float32))

    def block(name, width, kernel, ratio):
        w.f32(name + ".dw.w", normal(width, kernel, scale=1 / np.sqrt(kernel)))
        w.f32(name + ".dw.b", normal(width, scale=0.1))
        norm(name + ".ln", width)
        dense(name + ".pw1", width * ratio, width)
        dense(name + ".pw2", width, width * ratio)
        w.f32(name + ".gamma", rng.uniform(0.2, 0.8, width).astype(np.float32))

    def predictor(name, width, out_bias, out_gain):
        dense(name + ".c1", c["d_pred"], width * 3)
        norm(name + ".ln1", c["d_pred"])
        dense(name + ".c2", c["d_pred"], c["d_pred"] * 3)
        norm(name + ".ln2", c["d_pred"])
        dense(name + ".out", 1, c["d_pred"], bias=out_bias, spread=0.0, gain=out_gain)

    w = atts.Writer()
    w.u32("config", [c[k] for k in keys] + list(DILATIONS))
    d, e, v = c["d_enc"], c["d_dec"], c["voc_dim"]
    for name, rows in (("phone", 41), ("stress", 3), ("wordend", 2), ("punct", 8)):
        w.f32("am." + name, normal(rows, d, scale=0.5))
    for i in range(c["enc_blocks"]):
        block(f"am.enc.{i}", d, c["enc_kernel"], c["enc_ratio"])
    norm("am.enc_ln", d)
    predictor("am.dur", d, np.log(4.0), 0.4)  # about three frames a phone
    predictor("am.pitch", d, 0.0, 1.0)
    dense("am.pitch_emb", d, 3)
    dense("am.frame_in", e, d + 2)
    for i in range(c["dec_blocks"]):
        block(f"am.dec.{i}", e, c["dec_kernel"], c["dec_ratio"])
    norm("am.dec_ln", e)
    dense("am.mel_out", 100, e)
    dense("voc.in", v, 100 * c["voc_in_kernel"])
    norm("voc.in_ln", v)
    for i in range(c["voc_blocks"]):
        block(f"voc.blk.{i}", v, c["voc_kernel"], c["voc_ratio"])
    norm("voc.out_ln", v)
    bins = 1024 // 2 + 1
    # Log-magnitudes near 1.5 keep the waveform around -18 dBFS; phases spread over a few radians.
    head = np.concatenate([normal(bins, v, scale=0.3 / np.sqrt(v)), normal(bins, v, scale=3 / np.sqrt(v))])
    w.dense("voc.head", head, np.concatenate([np.full(bins, 1.5), np.zeros(bins)]).astype(np.float32))
    return w.save(path)


def to_pcm(y):
    v = np.clip(y.astype(np.float32), -1, 1) * np.float32(32767)
    return (np.sign(v) * np.floor(np.abs(v) + np.float32(0.5))).astype(np.int16)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, type=Path)
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[2] / "tests/tc002/runtime/speech")
    a = ap.parse_args()
    sys.path.insert(0, str(a.model))
    from scipy.signal import lfilter
    from tts import atts, runtime
    from tts.text import decode

    a.out.mkdir(parents=True, exist_ok=True)
    voice = a.out / "voice.atts"
    size = random_voice(voice, atts, runtime.CFG_KEYS)
    rt = runtime.Runtime(voice)
    out = bytearray(struct.pack("<I", len(UTTERANCES)))
    for text, chunks in UTTERANCES:
        out += struct.pack("<I", len(chunks))
        waves = []
        for codes in chunks:
            tokens = decode(codes)
            h = rt.encode(tokens)
            log_durations = rt.predictor("am.dur", h)
            frames = np.exp(log_durations).astype(np.float32) - np.float32(1.0)
            margin = np.abs(frames - np.floor(frames) - 0.5).min()
            if margin < 1e-3:
                sys.exit(f"{text!r}: a duration lies {margin:.1e} from rounding the other way; change SEED")
            mel, durations, pitch = rt.acoustic(tokens)
            spectrum = rt.spectrum(mel)
            waves.append(runtime.istft(spectrum, rt.m.n_fft, rt.m.hop, dc_block=False))
            out += struct.pack("<I", len(codes)) + np.asarray(codes, dtype="<u2").tobytes()
            out += np.asarray(durations, dtype=np.uint8).tobytes() + np.asarray(pitch, dtype="<f4").tobytes()
            out += struct.pack("<I", len(mel)) + np.asarray(mel, dtype="<f4").tobytes()
            print(f"{text!r}: {len(codes)} tokens, {len(mel)} frames, durations {list(durations)}")
        wave = lfilter([1.0, -1.0], [1.0, -0.9973], np.concatenate(waves)).astype(np.float32)
        pcm = to_pcm(wave)
        print(f"  {len(pcm)} samples, peak {np.abs(wave).max():.3f}, rms {np.sqrt(np.mean(wave ** 2)):.3f}")
        out += struct.pack("<I", len(pcm)) + pcm.astype("<i2").tobytes()
    (a.out / "voice.expect").write_bytes(bytes(out))
    print(f"wrote {voice} ({size} bytes) and voice.expect ({len(out)} bytes)")


if __name__ == "__main__":
    main()
