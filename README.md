# Android TV 16 for PS4

![Android TV for PS4](branding/repository-banner.svg)

I’m building Android TV 16 for the PS4 so Fat, Slim and Pro owners can all install **one PKG with one Strawberry kernel**. The package handles the hardware differences at boot. You don’t have to figure out which kernel to download. The kernel is the work of [@7xkq / rmux](https://gitlab.com/rmuxnet/linux).

**Release status · 25 November 2026.** I’m testing the package on a fresh install before I put the download here. Playback has been tested on Aeolia during development. Belize and Baikal still need confirmation on the release build.

## Using Android TV on the PS4

Android runs from the internal drive. You open it from the PS4 home screen and use your DualShock 4 to navigate Google TV Home.

- **Watch video.** Use SmartTube for YouTube, tune into BackWhen TV, or open your Plex library. On the Aeolia test console, BackWhen and Plex Live TV played smoothly in short tests. SmartTube held 30 fps at 1080p after two brief pauses at startup.
- **Get more apps.** Aurora Store is the storefront. I’ve used its anonymous sign-in to download and install apps. microG Services is included for apps that use Google services.
- **Browse your media.** Material Files lets you browse a NAS over SMB. Kodi, VLC and Jellyfin are included for playback.

## Testing and feedback

If you try the build, include your CUH model, firmware and exact PKG filename in your report. For a boot problem, tell me the last thing you saw. For a playback problem, include the app, what you played and whether the audio kept going. [Send a console report](https://github.com/odiecay/android-tv-ps4/issues/new?template=console-report.yml) · [Join the Discord](https://discord.gg/UxC8kx6Eh5)

## Earlier releases

If your installation already works, you can keep using it. Previous installers, checksums and their matching instructions are available for [Android 13](https://github.com/odiecay/android-tv-ps4/releases/tag/v1.1) and the [earlier Android 16 build](https://github.com/odiecay/android-tv-ps4/releases/tag/v0.1).

The original Android 13 port builds on BRTDTV / TD Android TV’s Android TV x86 distribution. Thanks to the PS4 Linux community, the Android and LineageOS contributors, and everyone who tested on their own console. [Project credits](docs/CREDITS.md)
