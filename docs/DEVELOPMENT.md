> **Android 16 v0.1:** use [the Android 16 source guide](../releases/android16/README.md). The document below describes Android 13 / 1.1.

# Architecture

Android TV for PS4 has three execution environments: the native Orbis installer, the GoldHEN payload receiver, and Linux running Android. The launcher is an installer and boot controller. Linux takes over the hardware; Android runs on that kernel in its own namespaces.

The source links below describe the 1.1 implementation (the source folder is still named `release-1.0`). [`runtime/`](../releases/release-1.0/runtime/) contains text exported from the packaged initramfs and a hash manifest. [`runtime-source/`](../releases/release-1.0/runtime-source/) and `prepare-runtime.py` describe changes applied to the accepted base runtime. [Build](BUILD.md) identifies the external binaries and the boundaries of reproducibility.

## 1. Native installation

[`install-prebuilt.c`](../releases/release-1.0/source/install-prebuilt.c) opens `/app0/assets` while it still has the app's normal filesystem view. It then obtains privileged filesystem access for installation and restores its previous credentials before boot delivery.

The compiled storage edition is 32 GiB. The app checks the installed edition before writes. It copies the compressed system, graphics library, translation archive, read-only app filesystem and boot files. It commits a `ready` marker only after those operations succeed.

The `PS4ANDROID13-SPARSE-V015` string is an on-disk layout marker retained for compatibility. It is not the public application version. `ready` means the Orbis-side file installation completed; `data-ready` means Linux created and checked the userdata image. These are separate states.

On later launches the launcher checks that the installed files still match the package. A full byte-for-byte comparison reads about 1.5 GB, which took up to two minutes after a restart and left memory fragmented just before Linux needed large contiguous blocks. A file that matches is now recorded in a `.verified` file holding its package ID (a SHA256 prefix generated into `boot-inputs.h` by `build-release.py`) and its installed size and modification time. While those match, the comparison is skipped. Files the launcher has just copied are recorded the same way. The time taken is logged as "Installed files checked in N s".

When startup stops, the launcher's status area switches to an error state: the activity bar stops, the headline says what to do, and the detail is wrapped by pixel width (`installer-ui.c`).

[`image-copy.c`](../releases/release-1.0/source/image-copy.c) verifies an existing file prefix before appending. A complete or partial mismatched copy is preserved and rejected. Completed copies are synchronized and renamed. Boot files and the graphics replacement use staged names and retain old copies. The launcher refuses unknown editions or conflicting recovery state instead of formatting user data.

The `stat` ABI shim in [`ps4-stat-abi.h`](../releases/release-1.0/source/ps4-stat-abi.h) supplies the target layout and compile-time offset checks. It must precede SDK headers: a host/SDK layout mismatch previously made file-size checks read the wrong field.

## 2. Local payload delivery and handoff

[`boot-stage.c`](../releases/release-1.0/source/boot-stage.c) sends the bundled flat payload to **127.0.0.1:9090**. GoldHEN executes it in its receiver context. The launcher does not execute the kernel loader in its own process, and no external host supplies the payload.

The receiver is [`boot-payload.c`](../releases/release-1.0/source/boot-payload.c). It reads the console firmware with the loader's own `get_firmware`, maps point releases to the base version that shares kernel offsets, and selects the matching kexec build from a table embedded in the payload ([`build-kexec.py`](../releases/release-1.0/build-kexec.py) builds one per firmware defined in the loader's `magic.h`). An unknown firmware returns `-120` before anything is loaded. It records the firmware and southbridge in the status record, reads the kernel and XZ initramfs from `/user/app/ps4-android/boot`, requests a 2048 MiB graphics reservation, and prepares Linux using the modified kexec loader.

Preparation and handoff are separate operations:

1. The launcher creates a control record with a fresh nonce and sends the payload.
2. The receiver prepares Linux memory and publishes progress tied to that nonce.
3. A QUERY verifies the prepared state and the kernel/initramfs CRC32 values.
4. At stage 7, the launcher closes VideoOut and frees its display allocation.
5. Only after successful display release does it acknowledge `released` in the control record.
6. The receiver validates that acknowledgement, issues ARM, queries the armed state, and triggers the Orbis shutdown-to-Linux handoff.

**Staging in pieces.** The loader no longer asks Orbis for one contiguous block per image. `kexec.c` works out the final layout first (protected-mode kernel at `0x6000000`, initramfs on the next 2 MiB boundary after the kernel's `init_size` workspace), then copies the kernel and initramfs into pieces of 2 MiB down to 64 KiB taken from free memory above or below that layout, never inside it. Each piece becomes a `{src, dst, len}` entry in a list kept with the boot parameters in low memory. After the CR3 switch, `jmp_to_linux` in `linux_thunk.S` walks the list with `rep movsb` and jumps to `startup_64`; because no piece overlaps any destination, the order of the copy doesn't matter. The CRC32 values still cover every byte, and every piece is range-checked before ARM. Before staging, the loader also tries the pre-26 Oct single-block request once and releases it, so `logs/launcher.log` can report piece counts and whether the old method would have worked.

A timer never authorizes boot. Timeouts stop preparation. A cancelled record, stale nonce, incomplete status, checksum mismatch or failed display close cannot authorize ARM. A successful socket write proves delivery only; it does not prove Linux booted.

The slideshow is stored as separate raw pixel assets rather than a large embedded C array. The native app is about 851 KB and uses its existing VideoOut buffers. Artwork descriptors are closed before Linux preparation; the app stays visible until the verified handoff condition. This preserves feedback without retaining the previous large graphics allocation strategy.

Source: [`launch-protocol.h`](../releases/release-1.0/source/launch-protocol.h), [`display-release.h`](../releases/release-1.0/source/display-release.h), and [`loader-source/`](../releases/release-1.0/loader-source/).

## 3. Linux opens the internal installation

The initramfs [`init`](../releases/release-1.0/runtime/init) creates the basic Linux mounts and starts [`androidtv-ps4-start`](../releases/release-1.0/runtime/usr/sbin/androidtv-ps4-start).

The script logs the southbridge it finds (Aeolia, Belize or Baikal) but doesn't require a particular one, and continues without Ethernet if no interface exists. It finds the internal drive as the disk that has partition 27; the PS4's internal drive sits behind a USB bridge, so it can't be told apart by its bus. It uses the console-specific key supplied in the boot environment to open a dm-crypt mapping, then mounts the existing UFS2 filesystem. That key is not a release-package asset.

The installation directory appears as `/mnt/ps4-user/app/ps4-android` in Linux. A fresh start creates `data.img.part` with `truncate`, formats that file as ext4 with lazy initialization, checks it, and commits `data.img` plus `data-ready`. An existing uncommitted data image stops for recovery; it is not silently reformatted.

The data image uses ext4 project quotas, because Android 13's vold tags each app's `Android/data` folder with a project ID and refuses the folder when that fails. Both kernels are built with `CONFIG_QUOTA` and `CONFIG_QFMT_V2`. New images are formatted with `-O quota,project`; older images are upgraded once before mounting (details in [Storage](STORAGE.md)). The script reads `/proc/config.gz` first and skips both steps on a kernel without quota support, so an image is never left in a format the running kernel can't mount. Case-insensitive folders (`external_storage.casefold.enabled`) stay off because the kernel doesn't have `CONFIG_UNICODE`.

The script also removes the `virtualmachine` group mapping for `MANAGE_VIRTUAL_MACHINE` from `platform.xml` in the RAM overlay. This build's C library doesn't know that group, so it resolved to an invalid ID (-1) and any app holding the permission, such as Shell, failed to start and was retried every 10 seconds.

Each start writes `hardware.txt`: the southbridge, which kernel image booted, every PCI, SDIO and USB device with its bound driver, network interfaces, loaded modules and the driver decisions the script made. If the start fails, the reports are copied to the first FAT or exFAT USB stick that appears, never to a partition of the internal drive.

Mounts:

| Path | Backing storage / purpose |
| --- | --- |
| `/mnt/ps4-user` | Decrypted existing PS4 UFS2 user filesystem. |
| `/mnt/android-sfs` | Read-only SquashFS `system.sfs`. |
| `/mnt/android-lower` | Read-only ext4 `system.img` inside the SquashFS. |
| `/android` | OverlayFS combining that system with a RAM upper/work directory. |
| `/mnt/android-persistent` | Writable ext4 `data.img`. |
| `/mnt/android-bundled-apps` | Read-only `bundled-apps.sfs`. |

The boot script installs the PS4 adaptations into the RAM overlay on each start. Persistent `/data` and `/metadata` are bind-mounted from directories inside `data.img`. The compressed system and bundled applications stay on disk. [Storage](STORAGE.md) covers capacity and duplication in the installed PKG.

## 4. Android namespaces and hardware access

[`android-child.sh`](../releases/release-1.0/runtime/opt/androidtv-ps4/android-child.sh) prepares Android's proc/sys/dev mounts, binderfs devices, persistent directories and runtime tmpfs mounts. The supervisor creates PID, mount, IPC, UTS and network namespaces; Android init becomes PID 1 inside them and runs through `chroot`.

This is Android userspace on the PS4 Linux kernel. Namespace isolation does not emulate the CPU or GPU. Native x86 and x86-64 apps use the console CPU directly; selected ARM apps use the separately supplied native bridge.

The Android command line is bound into its `/proc/cmdline`. A per-install random identifier is generated locally and stored as `device-id`; it is not the console's factory serial number. No developer serial is copied into other installations.

Two static overlays set PS4 defaults so Android TV setup starts with them. `name-overlay/` targets the Settings provider: device name "PlayStation 4", `def_sleep_timeout` -1 (never sleep) and `def_screen_off_timeout` 15 minutes. `screensaver-overlay/` targets the framework: the default screensaver is the `dreamx` Backdrop this build ships (the framework default names `com.google.android.backdrop`, which isn't included), and it starts instead of sleep. Android's sleep freezes PS4 Linux, so it must never be entered. `device-name.sh` re-applies the sleep settings at each boot as a backstop for installs from 1.0, retrying each write and logging under the `androidtv-defaults` tag.

The default device name is **PlayStation 4**. Setting `Settings.Global.device_name` from a boot script didn't survive Android TV setup, which writes its own default from the Settings provider. A static resource overlay (`name-overlay/`) for `com.android.providers.settings` changes that default (`def_device_name` and `def_device_name_simple`), so setup itself uses PlayStation 4. A script (`device-name.sh`, guarded by `persist.vendor.androidtv.named`) also sets the name once per installation, so a 1.0 install updated to 1.1 is renamed once, including one the user had renamed. Any name chosen after that is kept. Spotify Connect and casting read this setting.

The base app-facing model and fingerprint are compatibility inputs inherited from BRTDTV. The friendly device name and build label identify the PS4 port. Android is **13 / API 33** even though the base compatibility fingerprint mentions a newer Google device. Changing `ro.product.model` to `PlayStation 4` caused the YouTube service to reject the device; restoring the base model resolved that rejection in an A/B/A test. This does not establish certification or resolve DRM requirements.

## 5. Graphics, video and the boot animation

The display path comprises the PS4 amdgpu kernel driver, Mesa/radeonsi, GBM/gralloc and the drmfb Android hardware composer. SurfaceFlinger renders Android through that stack.

The relevant adaptations address different failures:

- **Primary-plane formats:** [`liverpool-primary-formats.patch`](../patches/liverpool-primary-formats.patch) advertises the Liverpool-supported XRGB/ARGB and XBGR/ABGR formats so Android buffers pass DRM validation.
- **Framebuffer identity:** the composer allocation-key change keys cached framebuffers by allocation identity instead of a recyclable imported-handle pointer. This addresses stale surfaces being presented as new buffers.
- **Format transitions:** a failed page flip clears the composer's modeset state so a framebuffer-format change can be retried through a modeset.
- **YV12 layout:** the supplied gralloc adaptation corrects video-plane stride/layout. Its binary is hash-pinned; complete reproducible graphics builds remain an external-input boundary.
- **Raster configuration:** [`patch-mesa-raster.py`](../releases/release-1.0/runtime-source/patch-mesa-raster.py) changes the Liverpool raster configuration in the pinned Mesa driver from `0x00000002` to `0x2a00161a`. The 64-bit library is patched at build time, and the 32-bit library (used by x86 apps) is patched at boot. In a 40-case depth/stencil probe, 27 cases failed before the correction and none after. Browser icons and Crossy Road rendering were then visually checked. This is an explicit binary adaptation, not a source-built Mesa claim.
- **Mode filtering:** [`display-modes.c`](../releases/release-1.0/source/display-modes.c) filters interlaced, doublescan and over-60-Hz connector modes inside the composer before Android starts presenting frames.

[`display-ready.c`](../releases/release-1.0/source/display-ready.c) observes scanout and immediately drops any implicitly acquired DRM master. When Android replaces the boot framebuffer, it stops console logging to the display. It does not inject frames or take display ownership. The boot animation gives feedback while Android prepares its applications.

GPU rendering, video decoding and protected-media output are separate capabilities. Working OpenGL or a successful Kayo stream does not prove every codec, app or HDCP requirement works. The media-extractor seccomp extension permits `memfd_create` and `ftruncate` for the Android 13 buffer path on this kernel; it does not disable the media sandbox.

## 6. Ethernet, Wi-Fi and Bluetooth

The Linux host brings up the physical Ethernet interface and creates an `atv0` macvlan with a locally administered address derived from the physical MAC. It moves that interface into Android's network namespace and renames it `eth0`. Android owns DHCP and connection policy. The host does not require a management IP.

The real wireless PHY is moved into Android's namespace as `wlan0`. The small `AtvConnectivity` system app retains a Wi-Fi-specific network request so Ethernet's higher score does not withdraw all Wi-Fi demand. It does not contain a password, bind other applications to Wi-Fi or replace Android's default route policy.

Bluetooth uses a different namespace arrangement because HCI sockets are available in the initial network namespace. The vendor Bluetooth HAL joins a retained handle to that namespace before dropping to its Bluetooth identity. Wi-Fi and Ethernet stay in Android's namespace.

Two device-specific fixes accompany that arrangement: a bounded foreground real-time scheduling budget and an SD8797-only adapter that masks unsupported LE event bits in HCI command `0x2001`. The adapter is loaded only when the Marvell SDIO identifier matches. It does not fake successful HCI replies. See [`bluetooth.rc`](../releases/release-1.0/source/bluetooth.rc) and [`bluetooth-sd8797.c`](../releases/release-1.0/source/bluetooth-sd8797.c).

The controller keylayout changes Sony `BUTTON_MODE` mappings to Android `HOME`. A paired Bluetooth controller still uses Android's input stack.

## 7. Applications and power control

`prepare-apps.py` verifies the selected signed APKs, preserves splits and extracts the appropriate native libraries into the normal system-app layout. Android does not perform the same library extraction for immutable system apps as it does for a user APK install. The resulting read-only app filesystem is mounted into `/system/app/atv-*`; account data is created independently in each console's `/data`.

The Linux supervisor remains outside Android's PID namespace and waits for Android init. A namespace restart reports SIGHUP; namespace power-off reports SIGINT. The supervisor maps those outcomes to a physical console operation, synchronizes and unmounts storage, and refuses power operations if cleanup fails. An ordinary crash is not treated as a power request.

The supervisor removes app and service binds first, then the system overlay and app filesystem before internal storage. Host tests check that busy mounts block physical power. Changes to the mount layout must preserve this order and include a console power test.

## Diagnostics and development boundaries

All logs are in `logs/` in the install folder, the start before in `logs/previous/`, and a USB stick gets the same files in `AndroidTV-PS4-logs`. Native delivery/progress is in `launcher.log`. Linux milestones are in `progress.log`, `setup.log` and `kernel.log`; Android service transitions are in `android-stages.log`; `running.log` and `kernel.log` are rewritten every minute while Android runs. Android also has bounded rotating logcat output under `/data/misc/logd`.

The runtime currently uses SELinux permissive mode and authenticated TCP ADB. No trusted host keys or user accounts are packaged. These are development choices, not claims of retail-device security or certification.

Changes must be built into a new PKG and tied to its hash. Editing a running Android overlay is useful for diagnosis but disappears at reboot unless encoded into the release inputs. [Contributing](../CONTRIBUTING.md) describes useful test reports and where to make changes.

## Observing a boot without disturbing it

Do not port-check GoldHEN BinLoader with an empty TCP connection, `nc -z`, a scanner or an HTTP request. A connection to the payload receiver is an attempted payload delivery, not a harmless health check. An empty probe produced a receiver error before the launcher attempted delivery. Use FTP for reachability and the kernel-log service for observation; the launcher alone delivers the intended payload during an end-to-end test.

A complete socket write does not establish that GoldHEN executed the payload. The launch status with the matching nonce confirms entry, subsequent stages identify Linux preparation, and stage 7 authorizes display release only after preparation succeeds. Capture the last completed stage and receiver log before retrying.
