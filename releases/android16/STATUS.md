# v0.1 status

This describes the released PKG with SHA256
`7a930b70d4eef5c9c3e48db7e18db5e803a43d190b4176bb41120da7d7a1ad2f`.
Android 16 / LineageOS 23.2, Linux `7.2.11-androidtv-ps4-a16+`, title `ATVX16001`,
application version `0.10`, persistent Android data size 32 GiB.

## Source update included in v0.1

The published v0.1 source adds the missing Gboard bind-mount cleanup to the
power supervisor. Both restart and power-off must release that host mount before
unmounting Android. The normal build compiles and packages the correction.
Host tests exercise unmount order, an absent Gboard mount, and refusal to power
off or restart when storage cleanup fails. No new console test has been made.
The original PKG above is unchanged; hardware failures below remain relevant.
This change is not a claim that all restart or display failures are resolved.

## Hardware evidence

The maintainer's CUH-1102A (Aeolia), firmware 13.52, was used for release testing.
The final package follows the preceding candidate's setup, playback, controller,
Bluetooth and logging tests, with additional screensaver/wake fixes. Restart
hangs were observed on the final package. The package validation report is in
`provenance/package-validation.txt`; structural package validation does not prove
that a console boots.

Belize and Baikal implementation paths are included in the single kernel. They
were not tested by the maintainer on physical Slim/Pro hardware before release.
Community reports after release still show Baikal reaching the prepared loader
handoff and then black-screening without Linux logs. **Baikal is not confirmed
working in v0.1.** A compatible loader path or upstream success is not proof that
this integrated package boots on that hardware.

## Known problems

- HDMI unplug/replug or switching the TV off can lose the picture permanently
  for that session. The hotplug worker exists, but reconnect recovery failed in
  testing. Do not describe it as fixed.
- Restart can hang on console text or at "waiting for a display frame" on the
  next boot. It can require a full power-off.
- Some Slim/Pro/Baikal combinations still black-screen after handoff. Stage 7
  means the loader prepared memory; it does not establish Linux or Android boot.
- Android 16's translation supports arm64 apps, not 32-bit-only ARM apps.
- Video decoding is on the CPU. The image limits advertised software decoder
  performance to 1080p60 (H.264/HEVC/VP9) and 720p30 (AV1); this is not a guarantee
  for every stream or application. Output modes are capped at 1080p.
- Google sign-in and application compatibility vary in community reports.
- Timezone may need to be selected manually.
- Boot logging stops after Android starts. Post-boot display faults therefore
  need separately collected diagnostics; the boot log may not contain them.

## Useful reports

Use a FAT32/exFAT USB stick connected before launch. Boot logs are copied to
`AndroidTV-PS4-logs` when the relevant stages reach the logging code. No folder
is an observation, not enough by itself to identify the failed component.
The v0.1 FTP log directory is `/user/app/ps4-android16/logs` (Android 13 used
`/user/app/ps4-android/logs`). Do not attach userdata or account information.

[Discord experimental testing](https://discord.com/channels/1564057215105638523/1564825329942401085)
and [GitHub issues](https://github.com/odiecay/android-tv-ps4/issues) are the places
to compare results. State the version explicitly so 1.1/b132 reports are not
mistaken for Android 16 v0.1 results.
