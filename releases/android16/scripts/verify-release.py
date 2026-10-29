#!/usr/bin/env python3
"""Check this source snapshot and, optionally, the original v0.1 PKG/assets."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT.parent / 'release-1.0'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pkg', type=Path)
    parser.add_argument('--assets', type=Path)
    args = parser.parse_args()
    recorded = json.loads((ROOT / 'provenance/release-build.json').read_text())
    runtime = json.loads((RELEASE / 'runtime/manifest.json').read_text())
    checks = []
    for prefix, hashes in [
        (RELEASE / 'source', recorded['source_sha256']),
        (RELEASE / 'loader-source', recorded['loader_build']['source_sha256']),
        (RELEASE, recorded['metadata_sha256']),
        (RELEASE / 'runtime', runtime['text_files']),
    ]:
        checks.extend((prefix / name, sha) for name, sha in hashes.items())
    if args.pkg:
        if args.pkg.stat().st_size != recorded['bytes']:
            raise SystemExit('PKG size does not match the released v0.1 package')
        checks.append((args.pkg, recorded['sha256']))
    if args.assets:
        names = {p.name for p in args.assets.iterdir()}
        if names != set(recorded['assets']):
            raise SystemExit('Asset list does not match v0.1: ' + str(names ^ set(recorded['assets'])))
        checks.extend((args.assets / name, sha) for name, sha in recorded['assets'].items())
    errors = [str(path) for path, sha in checks if not path.is_file() or digest(path) != sha]
    if errors:
        raise SystemExit('Different from released v0.1:\n' + '\n'.join(errors))
    print(f'PASS: {len(checks)} v0.1 source/input hashes match.')


if __name__ == '__main__':
    main()
