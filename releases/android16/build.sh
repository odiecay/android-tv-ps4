#!/bin/sh
# Portable entry point for the v0.1 launcher/package builder.
# See BUILD.md for the pinned dependencies and required arguments.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$here/../release-1.0/build-release.py" "$@"
