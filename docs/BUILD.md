> **Android 16 v0.1:** use [the Android 16 source guide](../releases/android16/README.md). The document below describes Android 13 / 1.1.

# Building Android TV for PS4

The active code is in `releases/release-1.0`. The native launcher, local payload runner, modified kexec loader, helper objects and package metadata can be built from this checkout plus the pinned dependencies below.

The Android system, base initramfs, graphics binaries and third-party APKs are **external inputs**. The kernel can be rebuilt from the pinned upstream source, published configuration and patch below. Rebuilding the launcher/PKG with those inputs is supported by the build scripts; this repository is not a complete AOSP/BRTDTV distribution build tree. Input hashes are recorded so a build cannot silently pick up a different runtime.

## Build environment

Use Linux x86-64 with:

- Python 3.11 or newer and Pillow.
- Clang 18, LLD 18, GCC, GNU binutils and Make.
- [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases), with its LLVM 18-compatible sysroot, `create-fself`, `create-gp4`, `PkgTool.Core` and sample runtime modules.
- SquashFS tools with Zstandard support and BusyBox when preparing runtime/app images.
- Inkscape and Roboto when regenerating artwork.
- Static libc development files when building the Linux power supervisor.
- Android platform 33, Java and Android build tools when rebuilding the connectivity APK.

The checked build uses Clang 18.1.8. `PkgTool.Core` is a .NET application and may require compatible runtime libraries on a newer host. `--runtime` below is the directory containing those libraries; use an existing empty directory if your host resolves them normally. The builder supplies `DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` and that library search path for its package-tool subprocesses.

## Checkout and pinned source dependencies

```sh
git clone https://github.com/odiecay/android-tv-ps4.git
cd android-tv-ps4
mkdir -p build/deps

git clone https://github.com/sleirsgoevy/ps4-libjbc.git build/deps/libjbc
git -C build/deps/libjbc checkout --detach 4dd18714fa7af7e15813d6a11537b60bb8c6f79c

git clone https://github.com/Scene-Collective/ps4-payload-sdk.git build/deps/payload-sdk
git -C build/deps/payload-sdk checkout --detach b7326416c23ce14639e9180a24093bdc5eedb579
```

Compile the helpers and generate a clean package template:

```sh
python3 releases/release-1.0/prepare-build.py \
  --sdk /path/to/OpenOrbis/PS4Toolchain \
  --libjbc build/deps/libjbc \
  --payload-sdk build/deps/payload-sdk \
  --output build/prepared
```

This produces:

- `build/prepared/objects`: libjbc and the privilege wrapper compiled for Orbis.
- `build/prepared/payload`: source-built payload helper objects and headers.
- `build/prepared/template`: product SFO and the OpenOrbis sample runtime modules.
- `build/prepared/helper-build.json`: source revisions and object/template hashes.

All nine helper object hashes have been reproduced against the recorded release inputs on the checked compiler environment. There is no requirement to copy opaque helper objects from the developer's machine.

## Obtain the runtime assets

For a launcher-only change, extract the corresponding published PKG with the OpenOrbis package tool:

```sh
/path/to/OpenOrbis/PS4Toolchain/bin/linux/PkgTool.Core pkg_extract \
  Android-TV-for-PS4.pkg build/extracted
```

The checked PkgTool extracts assets under `build/extracted/uroot/assets`. If your version uses a different PFS root, select the directory containing these exact files:

```text
assets/
  data-gib.txt
  bzImage
  initramfs.cpio.xz
  bootargs.txt
  vram.txt
  linux.elf
  system.sfs
  libgallium-baseline.so
  ndk-translation.tar.gz
  bundled-apps.sfs
```

Verify the source PKG against its published SHA256 before extraction. When building without an extracted PKG, supply those same inputs independently. The source checkout does not download the Android distribution or APKs for you.

The selected runtime must match `runtime-inputs.json`, `bundled-apps.json` and `bundled-apps-image.json`. Do not bypass a hash failure to combine unrelated development builds. The builder also rejects extra files in the asset directory.

## Compile and package

```sh
python3 releases/release-1.0/build-release.py \
  --sdk /path/to/OpenOrbis/PS4Toolchain \
  --template build/prepared/template \
  --objects build/prepared/objects \
  --runtime /path/to/pkgtool-runtime-libraries \
  --assets build/extracted/uroot/assets \
  --probe-build build/prepared/payload \
  --probe-linker releases/release-1.0/helpers/linker.x \
  --build build/package \
  --output build/dist
```

The builder compiles a kexec blob for every firmware in the loader's `magic.h` (20 in total; the 13.52 build must match the console-confirmed hash), the local payload runner, embeds the bundled payload, builds the native launcher, packages four raw slideshow images, generates the PKG and validates its structure and signatures. It checks SFO/content-ID agreement and verifies the final copied output.

Outputs are `Android-TV-for-PS4.pkg`, `SHA256SUMS`, `release.json` and `validation.txt`. The manifest records runtime, source, artwork, helper and payload hashes. Package validation is not a console boot test.

The title is **Android TV for PS4**, title ID **ATVX13001**, application version **1.10**, and content ID **IV0000-ATVX13001_00-PS4ANDROIDTV0000**. Rebuilds keep this product identity.

## Host regression tests

These tests compile temporary fixtures and do not deliver a payload or control a console:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess, sys
for test in sorted(Path('releases/release-1.0/tests').glob('test_*.py')):
    subprocess.run([sys.executable, str(test)], check=True)
PY
```

They cover copy/resume validation, edition guards, installation flow, UI resource handling, loopback delivery, handoff state/nonce checks, display release, mode filtering, Bluetooth packet handling and power cleanup. Hardware-specific behaviour still requires the exact built package on a console.

## Building the PS4 kernel

The kernel input was traced to `sony-jaguar-devs/ps4-linux` commit
`be8199c3c1617239c2b798663e86bc0dd22d2bdb`. A comparison of all tracked upstream
files found one source change, published in `kernel/liverpool-primary-formats.patch`.
`kernel/config` is the configuration from the tree whose `bzImage` matches the
package. `kernel/UPSTREAM.json` records those hashes.

Install the normal Linux kernel build dependencies (GCC, Make, binutils, flex,
bison, OpenSSL development headers, libelf development headers and bc), then:

```sh
git clone https://github.com/sony-jaguar-devs/ps4-linux.git build/deps/ps4-linux
git -C build/deps/ps4-linux checkout --detach be8199c3c1617239c2b798663e86bc0dd22d2bdb
git -C build/deps/ps4-linux apply ../../../releases/release-1.0/kernel/liverpool-primary-formats.patch
mkdir -p build/kernel
cp releases/release-1.0/kernel/config build/kernel/.config
make -C build/deps/ps4-linux O="$(pwd)/build/kernel" olddefconfig
make -C build/deps/ps4-linux O="$(pwd)/build/kernel" -j"$(nproc)" bzImage
```

The output is `build/kernel/arch/x86/boot/bzImage`. This configuration enables
PS4/Aeolia support, amdgpu, UFS2, device mapper, ext4, SquashFS, Binder, namespaces,
Android cgroups, Marvell Wi-Fi/Bluetooth and the network features used by Android.
The local version is `-androidtv-ps4-aeolia`.

The `xt_quota2` module in the initramfs must match the kernel you build. Its
namespace adaptation is documented in `patches/quota2-namespace-port.patch`;
changing the kernel requires rebuilding associated modules, checking firmware
requirements, and testing boot. Compiler version, build timestamp and identity
can affect the resulting kernel hash; the recorded hash identifies the tested
input, not a promise of byte-identical output from arbitrary toolchains.

## Editing the packaged runtime

For changes to the published boot scripts or power supervisor, use the current
package's verified assets and the matching `runtime/` snapshot:

```sh
python3 releases/release-1.0/repack-runtime.py \
  --assets build/extracted/uroot/assets \
  --output build/runtime/assets
python3 releases/release-1.0/export-runtime-source.py \
  --initramfs build/runtime/assets/initramfs.cpio.xz
```

`repack-runtime.py` consumes the snapshot text, syntax-checks shell scripts,
compiles `power/android-supervisor.c`, repacks the initramfs and updates the
runtime input manifest. Hardware binaries, Android images and apps stay pinned.
Use `--assets build/runtime/assets` in the package-build command afterward.
The output directory must be empty, and the source archive and snapshot must
match before rebuilding. Commit the changed source, regenerated snapshot and
manifest together.

## Preparing app and runtime changes

`bundled-apps.json` lists each signed APK, split, ABI, byte size and SHA256. Supply those clean files under their manifest `asset` names, then run:

```sh
python3 releases/release-1.0/prepare-apps.py \
  --inputs /path/to/clean-apk-inputs \
  --build build/apps \
  --output build/app-assets/bundled-apps.sfs
```

The script preserves signed APKs and prepares native libraries for Android's immutable system-app layout. Multi-architecture Play services includes both x86 and x86-64 libraries. The runtime mounts it over the original privileged product directory before Android scans packages, hiding the old ARM service; shutdown releases that mount before closing the app filesystem. It generates `bundled-apps-image.json`. It never consumes `/data/data`, accounts, saves or pairings.

`prepare-runtime.py` applies the port's current adaptations to the pinned accepted **base** initramfs and base graphics library. That base is identified by `BASE_SHA` in the script; it is not interchangeable with an already-patched final initramfs.

```sh
python3 releases/release-1.0/prepare-runtime.py \
  --base /path/to/accepted-base-assets \
  --output build/runtime/assets \
  --apps-dir build/app-assets
```

Check `--help` against the checked-out revision before preparing a different runtime. Changes to the base Android distribution, kernel, composer or gralloc require those external inputs to be rebuilt and their provenance recorded. The present pipeline adapts pinned binaries; it does not claim to compile the entire graphics stack or Android framework.

`runtime/` is the inspectable packaged text. `repack-runtime.py` consumes it for runtime edits; `build-release.py` consumes the resulting archive. To refresh it from the manifest-matching archive:

```sh
python3 releases/release-1.0/export-runtime-source.py \
  --initramfs build/runtime/assets/initramfs.cpio.xz
```

The connectivity helper has its own build script under `connectivity/`; provide an Android SDK and your own signing key. No signing key is stored in Git. The Linux supervisor source is under `power/`.

## Artwork and release checks

Run `python3 releases/release-1.0/artwork/build-slides.py` to regenerate the four previews. The package builder converts them to raw BGRA assets outside the launcher ELF. Changing a PNG or source file does not alter an existing PKG.

Before publishing, tie fresh-install, existing-install, playback, connectivity and power tests to the output hash. Freeze that tested artifact. Keep account data, local diagnostic captures, private keys and machine-specific deployment scripts outside the source and release assets.
