# Build Android TV for PS4 16 v0.1

Use this guide for the `v0.1` source tag. The older `docs/BUILD.md` documents
Android 13 / 1.1. Start with the released package's verified assets; it contains
the exact Android image, initramfs, firmware and third-party binaries used here.
No console or connection to the maintainer's computer is needed to build.

## 1. Host and checkout

Use Linux x86-64, Python 3.11+, Pillow, Clang/LLD 18, GCC, Make, binutils,
BusyBox, static libc development files, SquashFS tools and xz. The release
launcher was compiled with Clang 18.1.8. Install the
[OpenOrbis toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases),
including its SDK headers/libraries, package tools and sample runtime modules.
Kernel work also needs flex, bison, bc, libelf and OpenSSL development headers.

```sh
git clone --branch v0.1 https://github.com/odiecay/android-tv-ps4.git
cd android-tv-ps4
python3 releases/android16/scripts/verify-release.py
mkdir -p build/deps build/pkgtool-runtime
```

Set `ATV_SDK` to your OpenOrbis toolchain directory for the commands below.
Set `ATV_PKG` to the downloaded `Android-TV-for-PS4-16-v0.1.pkg`.

```sh
python3 releases/android16/scripts/verify-release.py --pkg "$ATV_PKG"
"$ATV_SDK/bin/linux/PkgTool.Core" pkg_extract "$ATV_PKG" build/extracted
python3 releases/android16/scripts/verify-release.py --assets build/extracted/uroot/assets
```

If your PkgTool version chooses another extraction root, use the directory
containing `bzImage`, `initramfs.cpio.xz` and `system.sfs`. The verifier checks
all 11 asset names and hashes. PkgTool may need compatible .NET/runtime
libraries: the package builder's `--runtime` argument supplies their library
directory. Use the empty `build/pkgtool-runtime` when your installed runtime
already resolves them. Extraction may need the same `LD_LIBRARY_PATH` and
`DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1` settings on such hosts.

## 2. Build helper objects from source

```sh
git clone https://github.com/sleirsgoevy/ps4-libjbc.git build/deps/libjbc
git -C build/deps/libjbc checkout --detach 4dd18714fa7af7e15813d6a11537b60bb8c6f79c
git clone https://github.com/Scene-Collective/ps4-payload-sdk.git build/deps/payload-sdk
git -C build/deps/payload-sdk checkout --detach b7326416c23ce14639e9180a24093bdc5eedb579

python3 releases/release-1.0/prepare-build.py \
  --sdk "$ATV_SDK" --libjbc build/deps/libjbc \
  --payload-sdk build/deps/payload-sdk --output build/prepared
```

The helper template initially uses the historical title; `build-release.py`
sets the actual Android 16 identity: `ATVX16001`, application `0.10`, content ID
`IV0000-ATVX16001_00-PS4ANDROIDTV1600`.

## 3. Build the launcher and PKG

```sh
sh releases/android16/build.sh \
  --sdk "$ATV_SDK" \
  --template build/prepared/template \
  --objects build/prepared/objects \
  --runtime build/pkgtool-runtime \
  --assets build/extracted/uroot/assets \
  --probe-build build/prepared/payload \
  --probe-linker releases/release-1.0/helpers/linker.x \
  --build build/package --output build/dist
```

Outputs: `Android-TV-for-PS4.pkg`, `release.json`, `SHA256SUMS`, `validation.txt`.
The generic output filename is retained by the builder. The released download
was named `Android-TV-for-PS4-16-v0.1.pkg`.

`provenance/release-build.json` is the original build report, including its
historical `release: 1.0` and pre-test boolean fields. Its SHA256, source/input
hashes and content ID identify the artifact; those old labels are not the
Android version or final hardware-test verdict. See `STATUS.md` for that.
Different compilers, library versions or timestamps can change rebuilt hashes.
Do not claim a new package has passed hardware testing just because it builds.

## 4. Change the runtime scripts

Recover the exact additions (modules, firmware, overlays and helper binaries)
from the verified package. This does not require an unpublished base initramfs.

```sh
python3 releases/android16/scripts/extract-additions.py \
  --assets build/extracted/uroot/assets --output build/additions
```

Edit `releases/release-1.0/runtime/`. The two copies of the start script
(`usr/sbin/androidtv-ps4-start` and `opt/androidtv-ps4/autoboot.sh`) must agree.
Then repack and refresh the snapshot:

```sh
python3 releases/release-1.0/repack-runtime.py \
  --assets build/extracted/uroot/assets \
  --additions build/additions --output build/edited-assets
python3 releases/release-1.0/export-runtime-source.py \
  --initramfs build/edited-assets/initramfs.cpio.xz
```

Build with `--assets build/edited-assets` and fresh build/output directories.
The repacker updates `runtime-inputs.json`; commit the edited text and generated
manifests together. `verify-release.py` is for checking the untouched v0.1
baseline and will intentionally report differences after your edits.

## 5. Build the exact kernel source

Download `linux-7.2.11-androidtv-ps4-v0.1.tar.xz` and `SHA256SUMS` from the
[v0.1 release](https://github.com/odiecay/android-tv-ps4/releases/tag/v0.1).
Check the archive's hash against `kernel/source.json`, then extract it under
`build/`. It is the full tracked kernel source at
`7aa4e549a1e18bdf34534fd3c2fada10b2d9bb81`, including all PS4 drivers and Android
changes. Copy the released configuration into it:

```sh
tar -xJf linux-7.2.11-androidtv-ps4-v0.1.tar.xz -C build
cp releases/android16/kernel/config build/linux-7.2.11-androidtv-ps4-v0.1/.config
make -C build/linux-7.2.11-androidtv-ps4-v0.1 LOCALVERSION=+ olddefconfig
make -C build/linux-7.2.11-androidtv-ps4-v0.1 LOCALVERSION=+ -j"$(nproc)" bzImage modules
make -C build/linux-7.2.11-androidtv-ps4-v0.1 LOCALVERSION=+ \
  M="$(pwd)/releases/android16/kernel/modules/quota2" modules
make -C build/linux-7.2.11-androidtv-ps4-v0.1 LOCALVERSION=+ \
  M="$(pwd)/releases/android16/kernel/modules/android-usb-class" modules
```

Alternatively, clone `https://github.com/gregkh/linux.git`, check out the
`upstream_commit` in `kernel/source.json`, and apply all
`kernel/patches/*.patch` in filename order with `git am`. The 31-patch series was
verified to reconstruct the exact release Git tree; its authorship/history is
preserved. The integrated PS4 stack came from
[meerzulee/linux-psx](https://github.com/meerzulee/linux-psx), with subsequent
Android and hardware adaptations recorded by the patches.

When replacing the kernel, rebuild **all** associated modules using that kernel
and config, including `psx_mts_net` and `wlan_mt76x8_sdio` from the kernel tree.
Update the matching files under `build/additions` and their hashes in
`runtime-additions.json`, repack, and put the rebuilt `arch/x86/boot/bzImage` in
the new asset directory. A matching release name/vermagic alone does not prove
module ABI compatibility. The release build had previously crashed with an old
`xt_quota2.ko` after a config change.

## 6. Android image, overlays and helpers

The original Android input is
`lineage-23.2-20261025-UNOFFICIAL-x86_64_tv.iso`; its full hash is in
`provenance/android-inputs.json`. It supplies an EROFS `system.efs`, which contains
the ext4 `system.img`. This is an external prebuilt Lineage distribution, not a
platform build produced by this repository. The source publication includes all
of this port's scripts that change it.

The image preparation sequence is:

1. Extract the ISO, then extract `system.img` from `system.efs` with EROFS tools.
2. Clone `https://github.com/Waydroid-ATV/mindthegapps_tv` and check out
   `38dd398368f90919610667edacb0c6922310fee3` (baklava).
3. Run `scripts/add-gapps.sh system.img /path/to/mindthegapps system-gapps.img`.
4. Supply the APKs listed in `provenance/image-inputs.json` under `image-inputs/`.
   The `etc/` text inputs are included. APKs are signed third-party inputs, not
   source code. Their package paths and hashes are recorded rather than guessed.
5. Run `python3 scripts/ps4-image-edits.py system-gapps.img system-ps4.img` from
   this directory. It invokes the hash-checked Mesa/minigbm and APK-resource
   patch scripts, applies codec limits, TV identity, launcher and Bluetooth
   changes. The final Pro Mesa patch is applied by the runtime on the Pro GPU.
6. Place the result as `system.img` in a clean directory and package it with
   `mksquashfs` using Zstandard (`-comp zstd -b 1M -noappend`). Update
   `SYSTEM_BYTES` in `../release-1.0/image-metadata.h` to the resulting
   `system.sfs` byte length before making a PKG.

For normal port development, retain the verified PKG's system image. The
external ISO/APKs are needed only when reconstructing or changing Android
userspace. The historical build invocation is preserved as
`provenance/build-original.sh`; it contains the original machine paths and
must be read as provenance, not used as the portable entry point.

`tools/ps-button.c` is a Linux helper and can be built with
`cc -static -O2 -Wall -Wextra tools/ps-button.c -o ps-button`.
`../release-1.0/runtime-source/overlay_remounter.c` is an Android helper; build
it statically with the Android NDK x86_64 compiler (the release used NDK r26d).
The power supervisor is compiled by `repack-runtime.py`.

The shared `../release-1.0/name-overlay/build.py` accepts `--source` and `--name`
for all three overlays, including `setupwizard-overlay`. Supply Android SDK
build tools, `android.jar`, and the matching public AOSP test key/certificate
for these static overlays. The setup wizard overlay used the platform test key.
No personal signing keys are required or published. Controller and connectivity
APK source and their build scripts are under `../release-1.0/`.

## Tests and contribution results

Host tests live in `../release-1.0/tests/` and do not operate a console. In the
source-publication check, seven passed. Three inherited fixtures stopped before
running: `test_boot_process.py` and `test_installer.py` lack the generated
`boot-inputs.h`; `test_sender_stage.py` lacks the `installer_ui_boot_error` stub.
The full production build succeeded and passed PKG validation; the rebuilt
launcher and boot payload binaries match the original build byte for byte.
The complete PKG hash differs. See [the verification record](provenance/source-validation.json).
No new console test or full Android/kernel rebuild was performed during source
publication. Always attach the new PKG hash and exact console/firmware when
reporting hardware results.
