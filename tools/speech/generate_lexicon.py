"""Writes the English pronunciation table SpeechLexiconData.h from the frozen selection.

--check verifies the table, the TSV and the pins without writing; --reselect also
rebuilds the selection from the pinned upstream inputs, downloaded into .pio.
"""
from __future__ import annotations
import argparse
import collections
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import sqlite3
import subprocess
import sys
import zipfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = json.loads((ROOT / 'tools/speech/sources.json').read_text(encoding='utf-8'))
DATA = ROOT / 'tools/speech' / MANIFEST['data_file']
OUTPUT = ROOT / 'src/platform/tc002/speech/SpeechLexiconData.h'
STRIDE = 32
PHONES = 'AA AE AH AO AW AY B CH D DH EH ER EY F G HH IH IY JH K L M N NG OW OY P R S SH T TH UH UW V W Y Z ZH'.split()
VOWELS = set('AA AE AH AO AW AY EH ER EY IH IY OW OY UH UW'.split())
PINS = {name: (pin['url'], pin['sha256']) for name, pin in MANIFEST['sources'].items()}

def bits_for_pron(pron: tuple[str, ...], ids: dict, vowels: set[str]) -> bytes:
    value, used = 0, 0
    for phone in pron:
        base = phone[:-1] if phone[-1].isdigit() else phone
        value |= ids[base] << used
        used += 6
        if base in vowels:
            value |= int(phone[-1]) << used
            used += 2
    return value.to_bytes(math.ceil(used / 8), "little")

def encode(records: list[tuple[str, list]], phones: list[str], vowels: set[str]) -> bytes:
    ids = {phone: i for i, phone in enumerate(phones)}
    payload, offsets = bytearray(), []
    previous = ""
    for index, (word, variants) in enumerate(records):
        shared = 0
        if index % STRIDE == 0:
            offsets.append(len(payload))
        else:
            while shared < min(len(word), len(previous)) and word[shared] == previous[shared]:
                shared += 1
        suffix = word[shared:].encode("ascii")
        assert shared <= 255 and len(suffix) <= 255 and len(variants) <= 255
        # Each nibble holds 0..14; 15 means an extra exact-length byte follows.
        payload.append((min(shared, 15) << 4) | min(len(suffix), 15))
        if shared >= 15:
            payload.append(shared)
        if len(suffix) >= 15:
            payload.append(len(suffix))
        payload.extend(suffix)
        payload.append(len(variants))
        for _, pron in variants:
            assert len(pron) <= 255
            payload.append(len(pron))
            payload.extend(bits_for_pron(pron, ids, vowels))
        previous = word
    header = struct.pack("<4sHBBII", b"LXR1", STRIDE, len(phones), 0, len(records), len(offsets))
    return header + struct.pack("<" + "I" * len(offsets), *offsets) + payload

def decode(blob: bytes, phones: list[str], vowels: set[str]) -> list[tuple[str, list[tuple]]]:
    magic, stride, phone_count, flags, word_count, block_count = struct.unpack_from("<4sHBBII", blob)
    assert (magic, stride, phone_count, flags) == (b"LXR1", STRIDE, len(phones), 0)
    offsets = struct.unpack_from("<" + "I" * block_count, blob, 16)
    start = 16 + 4 * block_count
    position, previous, decoded = start, "", []
    for index in range(word_count):
        if index % stride == 0:
            assert position == start + offsets[index // stride]
            previous = ""
        pair = blob[position]
        position += 1
        shared, length = pair >> 4, pair & 15
        if shared == 15:
            shared, position = blob[position], position + 1
        if length == 15:
            length, position = blob[position], position + 1
        assert shared <= len(previous)
        word = previous[:shared] + blob[position:position + length].decode("ascii")
        position += length
        count, position = blob[position], position + 1
        variants = []
        for _ in range(count):
            n, position = blob[position], position + 1
            # Read each packed token from a two-byte window.
            used, pron = 0, []
            for _ in range(n):
                byte_position, bit_position = divmod(used, 8)
                window = int.from_bytes(blob[position + byte_position:position + byte_position + 2], "little")
                phone_id = (window >> bit_position) & 63
                assert phone_id < len(phones)
                phone = phones[phone_id]
                used += 6
                if phone in vowels:
                    byte_position, bit_position = divmod(used, 8)
                    window = int.from_bytes(blob[position + byte_position:position + byte_position + 2], "little")
                    stress = (window >> bit_position) & 3
                    assert stress <= 2
                    phone += str(stress)
                    used += 2
                pron.append(phone)
            position += math.ceil(used / 8)
            variants.append(tuple(pron))
        decoded.append((word, variants))
        previous = word
    assert position == len(blob)
    return decoded

def records():
    rows = csv.DictReader((line for line in DATA.read_text(encoding='utf-8').splitlines() if not line.startswith('#')), delimiter='\t')
    entries = collections.defaultdict(list)
    for row in rows:
        word, pron = row['word'], tuple(row['arpabet'].split())
        if not re.fullmatch(r"[a-z]+(?:'[a-z]+)*", word) or len(word) >= 64 or not 0 < len(pron) <= 64:
            raise ValueError('dictionary exceeds bounded lookup: ' + word)
        for phone in pron:
            base = phone[:-1] if phone[-1].isdigit() else phone
            if base not in PHONES or ((base in VOWELS) != phone[-1].isdigit()):
                raise ValueError('unsupported ARPAbet: ' + phone)
        entries[word].append((int(row['cmu_variant']), pron))
    return sorted((w, sorted(v)) for w, v in entries.items())

def reselect(expected):
    cache = ROOT / '.pio/speech-lexicon-inputs'
    cache.mkdir(parents=True, exist_ok=True)
    inputs = {}
    for name, (url, digest) in PINS.items():
        path = cache / name
        if not path.exists():
            raw = urllib.request.urlopen(url, timeout=30).read()
            if hashlib.sha256(raw).hexdigest() != digest:
                raise ValueError('upstream input changed: ' + name)
            path.write_bytes(raw)
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != digest:
            raise ValueError('cached input changed: ' + name)
        inputs[name] = raw
    seeds = set((ROOT / 'tools/speech/application-words.txt').read_text(encoding='utf-8').splitlines())
    source = cache / 'esdb-source'
    # Pinned upstream tooling runs only with explicit --reselect, in disposable
    # generation scratch. Firmware builds and normal --check use frozen TSV.
    with zipfile.ZipFile(io.BytesIO(inputs['esdb-rel-2026.02.25.zip'])) as archive:
        for member in archive.infolist():
            target = (source / member.filename).resolve()
            if not target.is_relative_to(source.resolve()):
                raise ValueError('invalid archive member')
            # PostgreSQL convenience links are unused by the SQLite generator.
            # Never create archive symlinks in the local generation workspace.
            if (member.external_attr >> 16) & 0o170000 == 0o120000: continue
            if member.is_dir(): continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(member))
    upstream = source / 'wordlist-rel-2026.02.25'
    database = upstream / 'scowl.db'
    db_pin = cache / 'esdb-db.json'
    valid_cache = database.exists() and db_pin.exists() and json.loads(db_pin.read_text(encoding='utf-8')) == {
        'source_sha256': PINS['esdb-rel-2026.02.25.zip'][1],
        'database_sha256': hashlib.sha256(database.read_bytes()).hexdigest(),
    }
    if not valid_cache:
        database.unlink(missing_ok=True)
        subprocess.run([sys.executable, '-X', 'utf8', 'combine.py', 'create-db', 'scowl.db'], cwd=upstream, check=True)
        db_pin.write_text(json.dumps({
            'source_sha256': PINS['esdb-rel-2026.02.25.zip'][1],
            'database_sha256': hashlib.sha256(database.read_bytes()).hexdigest(),
        }), encoding='utf-8', newline='\n')
    words = set(seeds)
    with sqlite3.connect(database) as conn:
        rows = conn.execute('select distinct word,base_pos from scowl_ where ' + MANIFEST['query']['sql_where'],
                            MANIFEST['query']['sql_parameters'])
        for word, base_pos in rows:
            if word == 'I' and base_pos == 'pn': word = 'i'
            if re.fullmatch(r"[a-z]+(?:'[a-z]+)*", word): words.add(word)
    cmu = collections.defaultdict(list)
    for line in inputs['cmudict.dict'].decode('utf-8').splitlines():
        fields = line.split('#', 1)[0].split()
        if not fields: continue
        match = re.fullmatch(r'(.+?)(?:\((\d+)\))?', fields[0])
        if match and match[1] in words:
            cmu[match[1]].append((int(match[2] or 0), tuple(fields[1:])))
    selected = sorted((w, sorted(v)) for w, v in cmu.items())
    if selected != expected: raise ValueError('source selection differs from the reviewed TSV')
    print('PASS: pinned ESDB35 and CMU inputs reproduce all selected words/variants')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--reselect', action='store_true')
    args = parser.parse_args()
    source = records()
    if args.reselect: reselect(source)
    blob = encode(source, PHONES, VOWELS)
    expected = [(w, [pron for _, pron in v]) for w, v in source]
    if decode(blob, PHONES, VOWELS) != expected: raise ValueError('roundtrip mismatch')
    if len(blob) != MANIFEST['bytes'] or hashlib.sha256(blob).hexdigest() != MANIFEST['packed_sha256']:
        raise ValueError('dictionary footprint or contents differ from reviewed manifest')
    if hashlib.sha256(DATA.read_bytes()).hexdigest() != MANIFEST['tsv_sha256']:
        raise ValueError('source TSV differs from reviewed manifest')
    text = '// Generated by tools/speech/generate_lexicon.py from CMUdict and SCOWL; notices in\n'
    text += '// LICENSES/CMUdict-SCOWL.txt.\n#pragma once\n#include <cstdint>\n'
    text += 'namespace awtrix::speech::detail {\ninline constexpr uint8_t kEnglishLexicon[] = {\n'
    text += ''.join('  '+', '.join(str(x) for x in blob[i:i+24])+',\n' for i in range(0,len(blob),24))
    text += '};\n}\n'
    if args.check:
        if OUTPUT.read_text(encoding='utf-8') != text: raise ValueError('generated lexicon is stale')
    else: OUTPUT.write_text(text, encoding='utf-8', newline='\n')
    print(f'PASS: {len(source)} words / {sum(len(v) for _,v in source)} variants, {len(blob)} bytes; full roundtrip')

if __name__ == '__main__': main()
