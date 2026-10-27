/* Written to logs/launcher.log and passed to Linux as androidtv.boot_id, so
 * every report says which build it came from. Raise it for each public build. */
#define ATV_BUILD_ID "1.1-dev-a16"
/* Dev build: boot/bzImage-dev, when present, replaces the packaged kernel. */
#define ATV_DEV_KERNEL_OVERRIDE 1
#define ATV_DEV_KERNEL_MAX (64ULL*1024*1024)
