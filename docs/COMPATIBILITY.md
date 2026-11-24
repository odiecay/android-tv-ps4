# Compatibility

## Supported hardware

| Component | Version 1.1 |
| --- | --- |
| Console | Any PS4 model. Tested on a CUH-1102A (fat, Aeolia southbridge). |
| Firmware | Any GoldHEN firmware listed below. Tested on 13.52. |
| Android | Android TV 13 (API 33), based on BRTDTV Android TV x86 |
| Kernel | PS4 Linux 6.15.4 |
| Graphics memory | 2048 MiB reserved for the GPU |
| Android storage | 32 GiB, allocated as used |
| Boot | GoldHEN BinLoader on port 9090 (local only) |

## Firmware

The boot payload reads the console's firmware and picks the matching build of the [ps4-linux-loader](https://github.com/ps4-linux/ps4-linux-loader) kexec (v25.1, with 14.00/14.02 offsets added). The package contains one for each firmware the loader has kernel offsets for:

**5.05, 6.72, 7.00–7.55, 8.00–8.52, 9.00–9.60, 10.00–10.71, 11.00, 11.02, 11.50–11.52, 12.00–12.52, 13.00–13.52 and 14.00–14.02**

Point releases that share kernel offsets use the same build (for example 9.51 uses the 9.60 one). On a firmware with no offsets, the launcher shows "Firmware X isn't supported yet" and stops before anything is loaded. Only 13.52 has been tested on a real console so far.

## Console models

The full per-model breakdown, including Wi-Fi chips and the Baikal port, is in [Hardware support](HARDWARE.md).

Nothing is blocked by model. The launcher records the southbridge (Aeolia on fat models, Belize or Baikal on later fat, Slim and Pro models) in `logs/launcher.log` and boots anyway. The kernel includes drivers for all three. Belize Slims and Pros reach Android since the 26 Oct update (on MediaTek MT7668 consoles Wi-Fi doesn't turn on yet; Bluetooth works). Baikal consoles get no picture after the switch to Linux; that's being worked on. Holding the power button returns the console to normal. [Hardware support](HARDWARE.md) has the details by model.

Reports from other consoles are welcome, even when they fail ([how to report](#reporting-a-result)).

## Release package

`Android-TV-for-PS4-1.1-b132.pkg`: **3,123,380,224 bytes** (3.12 GB)

```text
SHA256  092b97255e12e6a6fbe4049b170c3a32acf0c9ecf2b6574c46deb48d8ae67286
```

This exact package was installed on the test console (CUH-1102A, 13.52) and booted to Android TV, with Wi-Fi handed to Android and the logs written to the console and a USB stick. Allow 5–10 minutes for the first boot and about 2 minutes after that.

## Tested and working

On the test console:

- Fresh installation, Android setup, Google sign-in and relaunching from the internal drive
- HDMI video and audio
- YouTube 7.27.302, signed in, at 1080p and 1440p
- Spotify and Kodi playback
- Kayo sign-in and streaming
- Wi-Fi and Ethernet
- Bluetooth pairing and navigation with a DualShock 4 and a Google TV Streamer 4K remote
- PS button as Android Home
- Restart to PS4 mode and power-off from Android (last tested on the console with a slightly earlier boot runtime)
- Correct rendering of browser icons and Crossy Road graphics

Results may differ on other hardware, accessories, or newer versions of apps.

## Known issues

| Issue | Details |
| --- | --- |
| Protected video / HDCP | There is no HDCP on this path. 7plus plays audio with no video and reports an HDCP error. Other services with strict DRM may behave the same way. |
| First setup restarts | The first Android setup can jump back to the start, usually once. 1.1 removed one cause (apps being refused their storage folders), which used to make it happen two or three times; the remaining cause isn't known yet. Keep going and it completes. It only happens during first setup. |
| YouTube guest mode | Guest playback can request a secure decoder that isn't available. Sign in to watch. |
| BinLoader connection (error 61) | The launcher couldn't reach BinLoader. Close the app, toggle BinLoader off and on, and reopen it ([steps](INSTALL.md#troubleshooting)). |
| Startup memory (code 12) | Should be rare since the 26 Oct update, which loads Linux in pieces of 2 MB or less instead of two unbroken 10–12 MB blocks. The first 1.1 build often needed a restart after installing, and some consoles failed even after restarting. If it still happens, restart, load GoldHEN, open the app, and send `logs/launcher.log`. Nothing is changed. ([details](INSTALL.md#troubleshooting)) |
| Payload received but not run | GoldHEN may show the payload as received but then report "failed to launch payload". The launcher keeps waiting. Restart the console and try again. |
| Not certified | This isn't a Google-certified Android TV device. Some apps and services may refuse to run or limit playback. |
| App updates | Play Store updates can change how an app behaves. The bundled versions are listed in [Apps](APPS.md). |
| Security | SELinux runs in permissive mode, and network ADB is on with per-computer authorization. |

## Reporting a result

Include your console model, firmware, the PKG SHA256, and whether it was a first install or a later launch. Attach the logs: if a start fails after Linux has started, plug in a FAT32 or exFAT USB stick and the logs are saved to it (see [Logs and reporting problems](INSTALL.md#logs-and-reporting-problems)); otherwise copy the `logs` folder from `/user/app/ps4-android` over FTP. `hardware.txt` lists the console's southbridge, chips and drivers, so it's the most useful single file. For startup problems, add the last thing on screen and any error message. For video problems, say whether the app refused to play, played audio only, or showed broken video. Keep account names, codes and private logs out of public reports.

The quickest way is the [console report form](https://github.com/odiecay/android-tv-ps4/issues/new?template=console-report.yml). The [testing checklist](RELEASE-CHECKLIST.md) covers everything worth checking.
