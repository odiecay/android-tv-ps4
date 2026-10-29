#!/usr/bin/env python3
"""Build v0.1 from source, including its current power supervisor and runtime."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
RELEASE = HERE.parents[1] / 'release-1.0'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    fields = ['sdk', 'template', 'objects', 'runtime', 'assets', 'build',
              'output', 'probe-build', 'probe-linker']
    for name in fields:
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--prepared-assets', action='store_true',
                        help='Use assets you already rebuilt with repack-runtime.py')
    args = parser.parse_args()
    paths = {name: getattr(args, name.replace('-', '_')).resolve() for name in fields}
    build = paths['build']
    if build.exists() and any(build.iterdir()):
        raise SystemExit('Build directory must be empty; choose a new one.')
    if paths['output'].exists() and any(paths['output'].iterdir()):
        raise SystemExit('Output directory must be empty; choose a new one.')
    root = RELEASE
    if not args.prepared_assets:
        subprocess.run([sys.executable, str(HERE / 'verify-release.py'),
                        '--inputs-only', '--assets', str(paths['assets'])], check=True)
        build.mkdir(parents=True, exist_ok=True)
        # The repacker updates manifests. Do that in an isolated source copy,
        # keeping the checkout and original package inputs unchanged.
        root = build / 'build-source'
        root.mkdir()
        for name in ['source', 'helpers', 'loader-source', 'power', 'runtime',
                     'runtime-source', 'artwork']:
            shutil.copytree(RELEASE / name, root / name,
                            ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
        for path in RELEASE.iterdir():
            if path.is_file() and path.suffix in ('.py', '.json', '.h') and path.name != 'HANDOFF.json':
                shutil.copy2(path, root / path.name)
        subprocess.run([sys.executable, str(HERE / 'extract-additions.py'),
                        '--assets', str(paths['assets']), '--output', str(build / 'additions')], check=True)
        subprocess.run([sys.executable, str(root / 'repack-runtime.py'),
                        '--assets', str(paths['assets']), '--output', str(build / 'assets'),
                        '--additions', str(build / 'additions')], check=True)
        paths['assets'] = build / 'assets'
        subprocess.run([sys.executable, str(root / 'export-runtime-source.py'),
                        '--initramfs', str(paths['assets'] / 'initramfs.cpio.xz')], check=True)
    command = [sys.executable, str(root / 'build-release.py')]
    for name in fields:
        command.extend(['--' + name, str(paths[name])])
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
