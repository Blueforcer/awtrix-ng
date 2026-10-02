#!/usr/bin/env python3
"""Verify both device entrypoints use the host-tested common built-in catalog."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--revision', help='Check an older git revision (regression demonstration)')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
for path, call in [('src/platform/rp2040/main_rp2040.cpp', 'builtins.addTo(apps, effects, overlays)'),
                   ('src/main.cpp', 'g_builtins.addTo(g_apps, g_effects, g_overlays)')]:
    text = (subprocess.check_output(['git', 'show', f'{args.revision}:{path}'], cwd=root, text=True)
            if args.revision else (root / path).read_text())
    assert 'capabilitiesJson({}, {}, {}' not in text, f'{path}: empty capabilities lists'
    assert call in text, f'{path}: missing shared built-in registration'
print('PASS: ESP32/Pico entrypoints use identical host-tested built-in catalog')
