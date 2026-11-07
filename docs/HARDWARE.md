# Hardware support

Every PS4 model has the same CPU, but the chips around it changed over the years. This page lists what is inside each model, what Android TV for PS4 does for that hardware, and what has actually been tested.

Only one console has been tested so far: a fat **CUH-1102A (Aeolia) on 13.52**. Everything else on this page is built from PS4 Linux community work and source code, and is waiting on real hardware reports. If you have one of these consoles, a [console report](https://github.com/odiecay/android-tv-ps4/issues/new?template=console-report.yml) is the most useful thing you can send, even if it fails.

| Mark | Meaning |
| :---: | --- |
| ✅ | **Tested.** Confirmed working on a real console, with a report behind it ([list below](#tested-consoles)). |
| 🧪 | **Built in, needs testing.** The support is in 1.1, but nobody has reported on this hardware yet. Your console could be the first. |
| ⚠️ | **Partial or uncertain.** Built from experimental community work, or known to work only in part. |
| ❌ | **Not supported yet.** |

## Which model do I have?

The southbridge is the chip that connects the drive, USB, Wi-Fi and Ethernet. Sony used four over the PS4's life, and most Slim and Pro model numbers shipped with more than one, depending on the motherboard. This table follows the board list on [PSDevWiki](https://web.archive.org/web/20260616145704/https://www.psdevwiki.com/ps4/Southbridge).

| Model numbers | Type | Southbridge |
| --- | --- | --- |
| CUH-10xx, CUH-11xx | Original ("fat") | Aeolia |
| CUH-12xx | Late fat | Belize |
| CUH-20xx | Slim | Belize or Baikal |
| CUH-21xx, CUH-22xx | Slim | Belize 2 or Baikal |
| CUH-70xx | Pro | Belize |
| CUH-71xx, CUH-72xx | Pro | Belize 2 or Baikal |

The model number alone can't tell Belize 2 from Baikal. The launcher writes the southbridge to `logs/launcher.log`, and Linux writes it to `logs/progress.log` (`Southbridge: ...`). GoldHEN 2.2.3 and later also shows it.

## Support by model

| | Fat, Aeolia | Fat CUH-12xx, Belize | Slim, Belize or Belize 2 | Slim, Baikal | Pro, Belize or Belize 2 | Pro, Baikal |
| --- | --- | --- | --- | --- | --- | --- |
| Starting Linux | ✅ on 13.52, 🧪 other firmware | ✅ | ✅ | ⚠️ no picture after the switch | ✅ | ✅ 7.0.8 kernel, build 133 |
| GPU and graphics fixes | ✅ Liverpool | ✅ Liverpool | ✅ Liverpool | 🧪 Liverpool | ✅ Gladius starts | ✅ Gladius |
| HDMI picture and sound | ✅ | ✅ | ✅ picture | ⚠️ no signal | ✅ picture | ✅ picture |
| Internal drive | ✅ | ✅ since the 26 Oct update | ✅ since the 26 Oct update | ⚠️ Baikal port | 🧪 fixed in the 26 Oct update | ✅ |
| USB | ✅ | ✅ | ✅ | ⚠️ Baikal port | ✅ | ✅ |
| Wi-Fi and Bluetooth | ✅ Marvell 88W8797 | ✅ Marvell 88W8897 | ⚠️ MT7668: Bluetooth works, Wi-Fi doesn't turn on yet (fix in the next build); 🧪 88W8897 (firmware loads) | ⚠️ Baikal port | 🧪 88W8897 or MT7668 | ✅ MT7668 |
| Ethernet | ✅ | ✅ | 🧪 | ⚠️ Baikal port | 🧪 | ✅ |
| Video playback fix (720p/4K freeze) | ✅ | 🧪 | 🧪 | 🧪 | 🧪 | 🧪 |
| Controller as remote, screensaver | ✅ | 🧪 | 🧪 | 🧪 | 🧪 | 🧪 |

## What is inside, and what we do with it

### GPU

| GPU | Consoles | PCI ID | Notes |
| --- | --- | --- | --- |
| Liverpool | Fat and Slim | `1002:9920`, `9922`, `9923` | Android's graphics driver gets the Liverpool raster configuration. Tested. |
| Gladius | Pro | `1002:9924` | Needs a second raster value because it has twice the shader engines. Applied at boot only when this GPU is present. Not yet tested. |

The GPU firmware is read from the console's own system software by the Linux loader, so nothing GPU-specific needs to be downloaded.

### Wi-Fi and Bluetooth

The Wi-Fi and Bluetooth chip sits on the southbridge's SDIO bus. The boot log's `Hardware:` line lists the SDIO IDs, which identify the chip.

| Chip | SDIO IDs | Seen in | What Android TV for PS4 includes |
| --- | --- | --- | --- |
| Marvell 88W8797 | `02df:9129` Wi-Fi, `02df:912a` Bluetooth | Fat CUH-10xx/11xx | Kernel driver, firmware and a Bluetooth fix. Tested. |
| Marvell 88W8897 | `02df:912d` Wi-Fi, `02df:912e` Bluetooth | Fat CUH-12xx, some Slims (e.g. a Baikal CUH-2002A, a Belize 2 CUH-2216A), some Pros (e.g. a Belize CUH-7115B) | Kernel driver plus `mrvl/sd8897_uapsta.bin` from linux-firmware. Works: Wi-Fi and Bluetooth confirmed on a fat CUH-1202 ([#27](https://github.com/odiecay/android-tv-ps4/issues/27)). |
| MediaTek MT7668 | `037a:7608` Wi-Fi, `037a:7668` Bluetooth | Many later Slims and Pros (e.g. CUH-2208B, CUH-7215B, CUH-7216B, CUH-2215B) | Wi-Fi driver loaded only when this chip is present, its firmware, and the MT7668 Bluetooth firmware. On a Belize 2 CUH-2215B, Bluetooth works but Wi-Fi doesn't turn on: the driver didn't allow its radio to be moved into Android's network namespace. Fixed for the next build. |

Which chip a given Slim or Pro has varies by board, even within one model: CUH-2216A consoles have been seen with both the Marvell 88W8897 and the MediaTek MT7668. The boot log's `Hardware:` line, or the `Wi-Fi chip:` line in `hardware.txt`, is the reliable way to tell. The startup picks drivers by these IDs, never by model number.

### Ethernet

Aeolia and Belize consoles use a Marvell Yukon-compatible controller (`sky2` driver), confirmed on a Belize CUH-2208B. Baikal's Ethernet is a different controller entirely, not a Yukon. The only driver known to work on it, `psx_mts_net` from linux-psx, has been tested on two Baikal Slims and is still experimental.

### Internal drive

Android lives in files on the PS4's internal drive, in its user partition (number 27 on every model). Since 1.1 the drive is found as the disk that has that partition. The internal drive is connected through a USB bridge inside the console, so it can't be picked out by bus type, and a USB stick left plugged in doesn't confuse it.

## The Baikal port

Baikal is the newest southbridge, in later Slims and Pros. It handles the drive, USB, Wi-Fi bus and Ethernet differently enough that it needs its own kernel changes: southbridge interrupt handling, the drive controller's registers and link start-up, the IOMMU, Bluetooth fixes and a new Ethernet driver.

That work exists in the PS4 Linux community, and 1.1 ships it as a **separate Baikal kernel** (`bzImage-baikal`), which the payload picks only on Baikal consoles. Fat, Belize and Pro-Belize consoles keep the tested kernel. `hardware.txt` says which kernel booted.

Up to build 131, Baikal consoles got a black screen or no signal right after the handover: the southbridge driver gave up when its serial port didn't register, and the GPU waits for the southbridge before it turns on the HDMI output. Build 132 adds the fixes from meerzulee's linux-ps4 (`v60-hdmi-working`, the first 6.15.4 build to show a picture on a Baikal Slim) and boots with `pci=nocrs intremap=off`. It hasn't been run on a Baikal here yet. Plug a USB stick in before launching: Linux copies its logs to it every few seconds, so even a start with no picture shows how far the southbridge and GPU got. The details are in [`kernel/baikal`](../releases/release-1.0/kernel/baikal/README.md).

## Firmware versions

Every firmware the Linux loader has kernel offsets for: 5.05, 6.72, 7.00–7.55, 8.00–8.52, 9.00–9.60, 10.00–10.71, 11.00, 11.02, 11.50–11.52, 12.00–12.52 and 13.00–13.52. Only 13.52 has been tested. See [Compatibility](COMPATIBILITY.md#firmware).

## Tested consoles

Every ✅ on this page comes from a console in this list. Failed attempts are listed too, because they're just as useful.

| Model | Firmware | Southbridge | Result | Report |
| --- | --- | --- | --- | --- |
| CUH-1102A | 13.52 | Aeolia | Works: install, boot, video, Wi-Fi, Ethernet, Bluetooth, controller, screensaver | Maintainer's test console |
| CUH-2215B | 13.50 | Belize 2 | Works on the 26 Oct update: Android, picture, internal drive, USB keyboard and mouse, Bluetooth controller (MT7668). Wi-Fi doesn't turn on: the MT7668 radio isn't handed to Android (fix in the next build) | Discord #test-reports |
| CUH-7215B (Pro) | 13.52 | Belize | Linux and the Gladius GPU start, picture on screen; stopped at the internal drive before the 26 Oct update | [#14](https://github.com/odiecay/android-tv-ps4/issues/14) |
| CUH-1202 (fat) | 12.02 | Belize | Works on the 26 Oct update: Android, picture and sound, Wi-Fi (88W8897), Ethernet, Bluetooth controller, video, restart and shut down | [#27](https://github.com/odiecay/android-tv-ps4/issues/27) |
| CUH-1216B (fat) | 9.00 | Belize | Linux starts; stopped at the internal drive before the 26 Oct update | [#20](https://github.com/odiecay/android-tv-ps4/issues/20) |
| CUH-2016A | 10.01 | Belize | Linux starts; stopped at the internal drive before the 26 Oct update | [#13](https://github.com/odiecay/android-tv-ps4/issues/13) |
| CUH-2016B | 12.52 | Belize | Linux starts; stopped at the internal drive before the 26 Oct update | [#11](https://github.com/odiecay/android-tv-ps4/issues/11) |
| CUH-2116A | 13.52 | Belize 2 | Linux starts; stopped at the internal drive before the 26 Oct update | [#12](https://github.com/odiecay/android-tv-ps4/issues/12) |
| CUH-2215B | 12.50 | Belize | Linux starts; stopped at the internal drive before the 26 Oct update | [#21](https://github.com/odiecay/android-tv-ps4/issues/21) |
| CUH-2216A | 12.52 | Belize 2 | Linux starts; stopped at the internal drive before the 26 Oct update | [#8](https://github.com/odiecay/android-tv-ps4/issues/8) |
| CUH-7208B (Pro) | 11.50 | Baikal | Works with the 7.0.8 kernel (build 133, started from USB): Android, picture, internal drive, MT7668 Wi-Fi and Bluetooth, Ethernet. Build 132: black screen, no logs | Pull request |
| CUH-7216B (Pro) | — | Baikal | Black screen after the switch to Linux | [#2](https://github.com/odiecay/android-tv-ps4/issues/2) |
| CUH-7216B (Pro) | 9.00 | Baikal | Freezes with the light white straight after opening the app; nothing on screen or on a USB stick | Discord report |
| Slim | 13.52 | Baikal | Black screen after the switch to Linux | [#16](https://github.com/odiecay/android-tv-ps4/issues/16) |
| Unknown | 10.50 | Baikal | Black screen after the switch to Linux | [#22](https://github.com/odiecay/android-tv-ps4/issues/22) |

## How this page gets updated

A [console report](https://github.com/odiecay/android-tv-ps4/issues/new?template=console-report.yml) with logs is what changes a mark:

- A report that Android started, with `hardware.txt`, turns the matching 🧪 into ✅ for that hardware and adds a row to [Tested consoles](#tested-consoles).
- A report that something didn't work turns the mark into ⚠️ or ❌, with a note on what failed, until it's fixed.
- `hardware.txt` decides which column a console belongs in, because the model number alone doesn't tell Belize 2 from Baikal.

Maintainers update the page from reports. You can also edit this file yourself: add your row to **Tested consoles**, link your report, and change only the marks your report shows. [Editing a page without Git](../CONTRIBUTING.md#editing-a-page-without-git) takes about a minute.

## Reporting your console

1. Install and open Android TV for PS4, even if you expect it to fail.
2. Note how far it got and any message on screen.
3. Get the logs. If Linux started but Android didn't, plug a FAT32 or exFAT USB stick into the PS4 and they're saved to it. Otherwise copy the `logs` folder from `/user/app/ps4-android` over GoldHEN FTP. `hardware.txt` describes your console exactly: southbridge, kernel, Wi-Fi chip and drivers.
4. Open a [console report](https://github.com/odiecay/android-tv-ps4/issues/new?template=console-report.yml), or post in [Discussions](https://github.com/odiecay/android-tv-ps4/discussions).

## Reading a black screen

A black screen after the handover can mean three different things, and `kernel.log` (saved to `/user/app/ps4-android/logs` and to a USB stick) tells them apart. Compare it with [a known-good fat PS4](reference-logs/fat-aeolia-gpu.txt); the first line that's missing shows where it stopped:

- **No `initializing kernel modesetting` line, or no `kernel.log` at all:** Linux stopped before the GPU driver ran.
- **`LIVERPOOL` or `GLADIUS` appears but there's no `Initialized amdgpu`:** the GPU driver failed. The lines just before say which part (firmware, memory or rings).
- **`amdgpudrmfb` appears:** the GPU is up and drawing, so the problem is the HDMI output or the TV.

## Credits for hardware support

The model-specific work builds on the PS4 Linux community: the [ps4-linux-loader](https://github.com/ps4-linux/ps4-linux-loader) developers, the Liverpool and Gladius kernel work, [crashniels](https://github.com/crashniels/linux) for the 6.15 PS4 and Baikal kernels, [feeRnt's ps4-linux-12xx](https://github.com/feeRnt/ps4-linux-12xx) for the 88W8897 and MT7668 work, [noob404's MT7668 firmware collection](https://github.com/noob404yt/mt7668-wifi-bt-firmware), and [linux-psx](https://github.com/meerzulee/linux-psx) for the Baikal storage, SDIO and Ethernet research.
