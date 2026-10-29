# Developing Android 16 v0.1

## Execution path

1. The native launcher installs the system and boot assets under
   `/user/app/ps4-android16`, preserving the separate Android data image.
2. The app delivers its bundled payload to GoldHEN BinLoader on the console's
   loopback address. `boot-payload.c` selects firmware offsets and prepares the
   kernel and initramfs in pieces to avoid needing one large free memory block.
3. The app releases its display allocation and acknowledges the prepared
   handoff. The payload arms the loader and requests the reboot into Linux.
4. `runtime/usr/sbin/androidtv-ps4-start` opens the encrypted PS4 drive, mounts
   the system image with a RAM overlay and the persistent data image, and starts
   Android in its namespaces. `android-child.sh` handles the init transition.
5. Android defaults, controller handling and the power supervisor complete the
   TV experience. USB log copying stops after Android has booted.

## Why the Android 16 changes exist

| Change | Reason and source |
| --- | --- |
| Android init transition | Android 16 uses `second_stage_resources` and bootstrap APEX differently. The runtime creates the tmpfs, makes mounts private and moves the root before starting init. See `android-child.sh`. |
| memfd/ashmem shim | Composer's compatibility path expects ashmem ioctls on memfd. See kernel patch 0003 and `mm/memfd-ashmem-shim*` in the kernel. |
| BPF settings per network namespace | Android netd needs the BPF JIT sysctls inside its namespace. See kernel patch 0003 and `net/core/sysctl_net_core.c`. |
| Belize/Baikal drive unlock | The partition index participates in the encryption tweak. See the start script's southbridge-specific mapping. |
| SquashFS direct/per-CPU decompression | The earlier single decompressor caused severe I/O stalls during APEX startup. The released config enables direct reads and per-CPU decompression. |
| sky2 interrupt handling | An Aeolia PHY interrupt storm consumed CPU. The kernel series masks the problematic interrupts and checks link state through the watchdog. |
| Mesa traps/raster setup | The supplied Mesa binaries contain addrlib assertion traps and need PS4 raster configuration. The patch scripts verify input hashes and exact patch locations. Pro-specific raster setup is applied at boot on its GPU only. |
| minigbm YUV mapping | Video buffers needed 512-byte luma stride and a CPU mapping covering the whole allocation. See `patch-minigbm-yuv.py`. |
| CPU video limits | Advertising higher resolutions made apps choose streams that the CPU could not decode in real time. See `ps4-image-edits.py`. |
| Gboard as a system app | Installing it later left setup without the expected keyboard. The runtime mounts the base APK and matching density split before package scanning. |
| Restart/power-off cleanup | The Gboard host bind survives Android namespace exit. The supervisor now unmounts it before the Android overlay and backing storage. `build.sh` compiles this into the runtime automatically; no Magisk layer is needed. |
| Controller/wake handling | Circle key-down/key-up handling produced duplicate Back actions; `ps-button.c` sends one Back on release and a wake event. |
| Colors screensaver | Backdrop failed to keep the display awake in testing. Colors does not require an account or network. |
| Logging stops after boot | Avoids continuous disk/USB writes and recording normal Android usage. |

These explain implementation decisions, not universal hardware guarantees.
See [STATUS.md](STATUS.md) for accepted failures.

## Baikal boot investigation

Start by finding the last **confirmed** stage. A full payload byte count only
proves delivery. Stage 7 proves preparation; missing Linux logs does not prove
which of handoff, early kernel, southbridge, storage or display failed.

Useful code locations:

- `../release-1.0/source/boot-payload.c`: firmware selection, Baikal-only command
  line and the prepare/release/arm sequence.
- `../release-1.0/loader-source/linux/ps4-kexec-common/`: staging and final jump.
- Kernel `drivers/ps4/` and `drivers/ata/`: ICC, southbridge and drive startup.
- Kernel `drivers/gpu/drm/amd/amdgpu/ps4_bridge.c`, `dce_v8_0.c`, `cik.c`:
  MN864729 sequencing, display clock fallback and Pro GPU identification.
- Kernel `drivers/net/ethernet/psx_mts_net.c`: Baikal Ethernet.
- `kernel/patches/0020-*` through `0028-*`: Baikal changes brought across from
  the upstream native Baikal work, including the 50 ms SATA settle, IOMMU path,
  bridge stages and delays, and fixed 1080p60 path.

Use the actual southbridge ID from logs, not just "Slim" or "Pro". Keep firmware,
kernel config and build inputs fixed while testing one hypothesis. Compare the
full report to a baseline and distinguish a proposed explanation from a measured
result. The source archive includes the upstream driver stack, so contributors
can inspect the entire path rather than only our final patch.

Never send an empty probe to GoldHEN BinLoader: an empty connection can disrupt
the receiver. Only deliver a complete intended payload. A final fresh-user test
must launch the installed app and its bundled payload. A live edit or manually
swapped kernel is useful for exploration, but does not validate a release PKG.
