# Contributing

Thanks for wanting to help. This started as one person tinkering with one PS4, and it gets better with every console, report and fix people bring. You don't need permission to jump in.

## Ways to help

- **Test it on your console** and send a [console report](https://github.com/odiecay/android-tv-ps4/issues/new?template=console-report.yml), even if it fails. Slim, Pro and Baikal reports are the most valuable right now. The form explains how to get the logs.
- **Talk it through** in the [Discord server](https://discord.gg/UxC8kx6Eh5) for quick chat and test builds, or [Discussions](https://github.com/odiecay/android-tv-ps4/discussions) for ideas and longer write-ups.
- **Fix docs or add your console** to [Hardware support](docs/HARDWARE.md#tested-consoles).
- **Work on the code.** The [roadmap](docs/ROADMAP.md) lists known work with what's already figured out, and the sections below get you started.

## Editing a page without Git

Open the file on GitHub, click the **pencil icon** (Edit this file), make your change and choose **Propose changes**. GitHub copies the repository to your account and opens a pull request for you. No Git or command line needed.

## Android 16 v0.1

Use [the v0.1 source guide](releases/android16/README.md) for the released Android 16 package, kernel source, build instructions and hardware status. The mapping below also covers the shared launcher/runtime layout.

## Where things live

All the source is in [`releases/release-1.0/`](releases/release-1.0/) (the folder name predates 1.1). Read [How it works](docs/DEVELOPMENT.md) first, then [Build it](docs/BUILD.md).

| If you want to work on... | Start with |
| --- | --- |
| Installing and launching | `source/install-prebuilt.c`, `source/boot-stage.c`, `source/installer-ui.c` |
| Getting Linux to start | `source/boot-payload.c`, `loader-source/` |
| A new firmware or console model | `build-kexec.py`, `kernel/`, `runtime/usr/sbin/androidtv-ps4-start` |
| Graphics and video | `patches/`, `runtime-source/` |
| Wi-Fi, Bluetooth and Ethernet | `connectivity/`, `kernel/baikal/`, the start script |
| Controller and remote | `runtime/opt/androidtv-ps4/controller-home.sh` |
| Android defaults (name, screensaver) | `name-overlay/`, `screensaver-overlay/`, `runtime/opt/androidtv-ps4/device-name.sh` |
| Bundled apps | `bundled-apps.json`, `prepare-apps.py` |

## How we work

1. Reproduce the problem and note your console model, firmware and the PKG hash.
2. Make the smallest change that fixes it.
3. Build a PKG and test it on a real console: a fresh install, and a launch over an existing install if your change touches either.
4. Open a pull request saying what you changed and what you saw, with the logs.

A fix only counts once it works from a freshly built PKG on a fresh install. Changing a running console by hand is a good way to find a fix, not to prove it.

## A few ground rules

- **Never risk people's data.** If something fails, existing Android data is kept, and nothing is formatted or overwritten that a user would miss.
- **Booting needs no PC or network.** GoldHEN on the console itself is the only requirement.
- **Be honest about what's tested.** Say which console and firmware a result comes from, and don't claim more than that.
- **No personal data in the build.** Accounts and app data come from each user's own setup.
- **Credit your sources.** Keep upstream licence notices and say where copied code, patches and binaries come from. There's no single project licence yet, so don't relabel upstream code.

Leave account names, Wi-Fi names, tokens and IP addresses out of anything you share, and never attach a userdata image.
