// Android TV for PS4: stands in for Android 16's /system/xbin/overlay_remounter.
//
// Android's root here is an overlayfs the PS4 runtime builds (read-only system
// image plus a RAM layer for its own settings). init execs overlay_remounter
// whenever an overlay is mounted; the real one unmounts every overlay and
// mounts it again read-only, which fails on Android's own root. Skip that and
// go straight to second-stage init, as init does when there are no overlays.
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char *const args[] = { "/system/bin/init", "second_stage", NULL };

	execv(args[0], args);
	perror("overlay_remounter: execv /system/bin/init");
	return 1;
}
