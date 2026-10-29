# Android TV for PS4 16 v0.1 source

This is the source for the experimental Android 16 package released on 28 October
2026: `Android-TV-for-PS4-16-v0.1.pkg`, **3,166,371,840 bytes**, SHA256
`7a930b70d4eef5c9c3e48db7e18db5e803a43d190b4176bb41120da7d7a1ad2f`.

- [Download the released PKG](https://www.mediafire.com/file/iau0yaa395r71lo/Android-TV-for-PS4-16-v0.1.pkg/file)
- [Source release, full kernel source and checksums](https://github.com/odiecay/android-tv-ps4/releases/tag/v0.1)
- [Build instructions](BUILD.md)
- [Hardware status and known problems](STATUS.md)
- [What changed and why](DEVELOPMENT.md)

The launcher, payload, runtime and common build tools remain in
[`../release-1.0/`](../release-1.0/): that historical directory name does **not**
mean Android 13 on this branch. Android 13 / 1.1 remains on the `main` branch and
the `v1.1` tag. Use the `v0.1` tag for this release.

## Start here

```sh
git clone --branch v0.1 https://github.com/odiecay/android-tv-ps4.git
cd android-tv-ps4
python3 releases/android16/scripts/verify-release.py
```

| Area | Source |
| --- | --- |
| Native installer, UI, uninstaller | [`../release-1.0/source/`](../release-1.0/source/) |
| GoldHEN payload and kernel handoff | `../release-1.0/source/boot-payload.c`, [`../release-1.0/loader-source/`](../release-1.0/loader-source/) |
| Exact packaged startup scripts | [`../release-1.0/runtime/`](../release-1.0/runtime/) |
| Linux 7.2.11 PS4/Android changes | [`kernel/`](kernel/), including all 31 patches and the released config |
| Complete Linux source tree | `linux-7.2.11-androidtv-ps4-v0.1.tar.xz` on the source release |
| Out-of-tree kernel modules | [`kernel/modules/`](kernel/modules/) |
| Android 16 system-image changes | [`scripts/`](scripts/) and [`image-inputs/etc/`](image-inputs/etc/) |
| Mesa and minigbm changes | `scripts/patch-mesa26.py`, `patch-mesa26-gladius.py`, `patch-minigbm-yuv.py` |
| PS button, Circle and wake handling | [`tools/ps-button.c`](tools/ps-button.c), `../release-1.0/runtime/opt/androidtv-ps4/controller-home.sh` |
| Setup wizard and settings overlays | `../release-1.0/setupwizard-overlay/`, `name-overlay/`, `screensaver-overlay/` |
| Package provenance and original build script | [`provenance/`](provenance/) |

This publishes the project's port source, kernel source, patches and build
recipes. Android/Lineage, Google apps, translation libraries and other supplied
binaries are external inputs; this is not a complete Android platform source
checkout. You can recover the exact runtime inputs from the verified released
PKG to build launcher/runtime changes without finding an older private build.

The snapshot intentionally contains the runtime shipped in v0.1. The later USB
filesystem-filter change is not part of that package and is not included here.

## Help improve it

Baikal owners can start with [the handoff and kernel map](DEVELOPMENT.md#baikal-boot-investigation).
Report the exact PKG hash, console model, firmware, southbridge ID from the log,
and the last confirmed boot stage. One change per experiment makes reports useful.
Keep a working baseline and preserve your data. A successful compile is not a
successful console boot.

Keep upstream licence notices with their files. The kernel archive includes its
licences; the port incorporates components with different licences and does not
relicense them under one blanket licence. Private logs, account data, signing
credentials and userdata images are not source inputs.
