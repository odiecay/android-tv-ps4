# Baikal kernel

Baikal consoles (later Slims and Pros) boot `bzImage-baikal`; every other console keeps the tested kernel.

It is built from [Bobjes/linux-rmux `androidtv/baikal-7.0.8`](https://github.com/Bobjes/linux-rmux/tree/androidtv/baikal-7.0.8): rmux's `baikal/7.0.8-Stable` (Linux 7.0.8 with the Baikal southbridge, AHCI, xHCI, SDHCI, MSI and Gladius display bring-up) plus:

| Commit | Why |
| --- | --- |
| usb: xhci-aeolia: poll Baikal AHCI completions | The internal drive's AHCI shares one edge MSI with xHCI and loses completions; the drive timed out and dropped off. |
| drm/amdgpu: dce_v8_0: add XBGR/ABGR to PS4 primary planes | Android draws in XBGR8888; without it every frame failed `drmModeAddFB2` and the screen stayed black. Same change as `../liverpool-primary-formats.patch`. |
| mt76x8: allow netns moves of MT7668 wiphys | Same change as `../mt7668-netns-ok.patch`. |
| androidtv: config and build script | `androidtv/config` and `androidtv/build.sh`. |

```sh
git clone --depth 1 -b androidtv/baikal-7.0.8 https://github.com/Bobjes/linux-rmux.git
cd linux-rmux && androidtv/build.sh    # androidtv/out/bzImage-baikal
```

Build on a case-sensitive file system with Clang/LLVM 18 or newer (x86-64 or arm64 host). The release is `7.0.8-androidtv-ps4-baikal`.

## Modules

The start script loads modules from `/lib/modules/$(uname -r)/extra`. For this kernel the initramfs carries (see `runtime-additions.json`):

- `psx_mts_net.ko` from `psx_mts_net.c` and `mts_net_core.h` here: linux-psx's Baikal Ethernet driver (GPL-2.0), with a receive-mode handler so Android's macvlan address gets its unicast frames
- `xt_quota2.ko` from AOSP `android13-5.15` with `../../../../patches/quota2-namespace-port.patch`

Build them against the kernel tree with `make -C <tree> M=<dir> LLVM=1 ARCH=x86_64 LOCALVERSION= modules`. The MT7668 Wi-Fi driver is built in.

## Status

Tested on a PS4 Pro CUH-7208B (Baikal, firmware 11.50), started from USB with the build 132 runtime: Android, HDMI picture, internal drive (about 120 MB/s, no errors), MT7668 Wi-Fi and Ethernet. Baikal Slims are untested.
