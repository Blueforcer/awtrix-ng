"""Stage only the pinned project-owned extension, recipe and MIT license."""
import argparse
import hashlib
import json
from pathlib import Path


def stage(release, output, lock):
    pin = json.loads(lock.read_text(encoding='utf-8'))
    allowed = {'manifest.json', 'extension.bin', 'LICENSE.txt'}
    if {p.name for p in release.iterdir()} != allowed:
        raise ValueError('MCU public artifact must contain only manifest.json, extension.bin and LICENSE.txt')
    if output.exists() and (output.is_symlink() or any(p.name not in allowed for p in output.iterdir())):
        raise ValueError('MCU output contains unexpected files; use a clean directory')
    verified = {}
    for name, digest in (('manifest.json', pin['manifest_sha256']), ('extension.bin', pin['sha256']),
                         ('LICENSE.txt', pin['license_sha256'])):
        path = release / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Not a regular MCU artifact: {path}')
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f'MCU release does not match {lock}: {path}')
        verified[path.name] = data
    metadata = json.loads(verified['manifest.json'])
    if (metadata['schema'] != 2 or metadata['recipe'] != 'tc002-pcm-v1' or metadata['target'] != 'tc002' or
            metadata['version'] != pin['version'] or metadata['sha256'] != pin['sha256'] or
            metadata['image_sha256'] != pin['image_sha256'] or metadata['base_sha256'] != pin['base_sha256'] or
            metadata['bytes'] != len(verified['extension.bin']) or not 256 < metadata['bytes'] <= 4096 or
            metadata['file'] != 'extension.bin'):
        raise ValueError('Inconsistent MCU release metadata')
    output.mkdir(parents=True, exist_ok=True)
    for name, data in verified.items():
        destination = output / name
        if destination.is_symlink():
            raise ValueError(f'Symlink destination: {destination}')
        destination.write_bytes(data)
        destination.chmod(0o644)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('release', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--lock', type=Path, default=Path(__file__).with_name('release.json'))
    args = parser.parse_args()
    stage(args.release, args.output, args.lock)
