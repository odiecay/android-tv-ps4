#!/usr/bin/env python3
"""Recover hash-pinned runtime additions from the v0.1 package's assets."""
import argparse
import hashlib
import json
import lzma
from pathlib import Path
import runpy
import tarfile

ROOT = Path(__file__).resolve().parents[2] / 'release-1.0'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pins = json.loads((ROOT / 'runtime-inputs.json').read_text())
    for name, key in [('initramfs.cpio.xz', 'initramfs_sha256'),
                      ('mt7668-wifi.tar.gz', 'mt7668_wifi_sha256')]:
        with (args.assets / name).open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != pins[key]:
                raise SystemExit('Wrong release asset: ' + name)
    if args.output.exists() and any(args.output.iterdir()):
        raise SystemExit('Output directory must be empty')
    unpack = runpy.run_path(str(ROOT / 'prepare-runtime.py'))['unpack']
    entries = unpack(lzma.decompress((args.assets / 'initramfs.cpio.xz').read_bytes()))
    files = json.loads((ROOT / 'runtime-additions.json').read_text())['files']
    recovered = []
    with tarfile.open(args.assets / 'mt7668-wifi.tar.gz', 'r:gz') as archive:
        for name, item in files.items():
            path = Path(name)
            if path.is_absolute() or '..' in path.parts:
                raise SystemExit('Unsafe manifest path: ' + name)
            if item.get('location') == 'drive':
                member = archive.getmember(name)
                if not member.isfile():
                    raise SystemExit('Expected a regular file: ' + name)
                with archive.extractfile(member) as stream:
                    data = stream.read()
            else:
                data = entries[name][1]
            if hashlib.sha256(data).hexdigest() != item['sha256']:
                raise SystemExit('Addition hash mismatch: ' + name)
            recovered.append((path, data, int(item.get('mode', '0644'), 8)))
    for path, data, mode in recovered:
        dest = args.output / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        dest.chmod(mode)
    print(f'Recovered and verified {len(recovered)} runtime additions.')


if __name__ == '__main__':
    main()
