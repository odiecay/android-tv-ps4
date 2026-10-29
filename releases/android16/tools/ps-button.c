// PS button handler for Android TV for PS4.
//
// Short press of the PS button: Home. Hold it: Android's power menu (Restart /
// Power off). The power key is always held for a full second, because a short
// power press puts Android to sleep, and sleep freezes the PS4.
//
// Pressing PS also wakes the display if Android has turned it off.
//
// Circle: Back, sent as one complete press when the button is released.
// Android 16 apps act on Back at different points: TV Settings closes on
// key down, and the launcher then takes the key up as a Back of its own and
// closes too, so one press went back twice. The layout leaves Circle
// unmapped (controller-home.sh) so only this Back reaches Android.
//
// Reads Sony controllers (vendor 054c) from /dev/input without grabbing them,
// so the rest of the controller keeps working, and sends the keys through its
// own uinput keyboard. The controller layout must leave BUTTON_MODE unmapped
// (not HOME), or Android would act on the PS button as well.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define MAX_PADS 8
#define HOLD_MS 700
#define POWER_HOLD_MS 1000

static int pads[MAX_PADS];
static char pad_paths[MAX_PADS][300];

static long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void emit(int ui, int type, int code, int value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(ui, &ev, sizeof(ev)) < 0)
		perror("uinput write");
}

static void key(int ui, int code, int value)
{
	emit(ui, EV_KEY, code, value);
	emit(ui, EV_SYN, SYN_REPORT, 0);
}

static int make_keyboard(void)
{
	int ui = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (ui < 0) {
		perror("/dev/uinput");
		return -1;
	}
	ioctl(ui, UI_SET_EVBIT, EV_KEY);
	ioctl(ui, UI_SET_KEYBIT, KEY_HOMEPAGE);
	ioctl(ui, UI_SET_KEYBIT, KEY_POWER);
	ioctl(ui, UI_SET_KEYBIT, KEY_BACK);
	ioctl(ui, UI_SET_KEYBIT, KEY_WAKEUP);
	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_VIRTUAL;
	snprintf(setup.name, sizeof(setup.name), "PS4 PS button");
	if (ioctl(ui, UI_DEV_SETUP, &setup) || ioctl(ui, UI_DEV_CREATE)) {
		perror("uinput create");
		close(ui);
		return -1;
	}
	return ui;
}

// Open any Sony controller event device not already open.
static void scan_pads(void)
{
	DIR *dir = opendir("/dev/input");
	struct dirent *de;
	if (!dir)
		return;
	while ((de = readdir(dir))) {
		char path[300];
		if (strncmp(de->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		int known = 0, slot = -1;
		for (int i = 0; i < MAX_PADS; i++) {
			if (pads[i] >= 0 && !strcmp(pad_paths[i], path))
				known = 1;
			if (pads[i] < 0 && slot < 0)
				slot = i;
		}
		if (known || slot < 0)
			continue;
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		struct input_id id;
		unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1];
		memset(keys, 0, sizeof(keys));
		if (ioctl(fd, EVIOCGID, &id) || id.vendor != 0x054c ||
		    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0 ||
		    !(keys[BTN_MODE / (8 * sizeof(long))] & (1UL << (BTN_MODE % (8 * sizeof(long)))))) {
			close(fd);
			continue;
		}
		pads[slot] = fd;
		snprintf(pad_paths[slot], sizeof(pad_paths[slot]), "%s", path);
		fprintf(stderr, "ps-button: watching %s\n", path);
	}
	closedir(dir);
}

int main(int argc, char **argv)
{
	/* --hold-only: leave short presses to Android (its layout still maps PS to HOME). */
	int send_home = !(argc > 1 && !strcmp(argv[1], "--hold-only"));
	for (int i = 0; i < MAX_PADS; i++)
		pads[i] = -1;
	int ui = make_keyboard();
	if (ui < 0)
		return 1;
	long pressed_at = 0, last_scan = 0;
	int down = 0, power_sent = 0;

	for (;;) {
		long t = now_ms();
		if (t - last_scan > 2000) {
			scan_pads();
			last_scan = t;
		}
		struct pollfd pfd[MAX_PADS];
		int map[MAX_PADS], n = 0;
		for (int i = 0; i < MAX_PADS; i++)
			if (pads[i] >= 0) {
				pfd[n].fd = pads[i];
				pfd[n].events = POLLIN;
				map[n++] = i;
			}
		int timeout = 2000;
		if (down && !power_sent)
			timeout = HOLD_MS - (int)(t - pressed_at);
		if (timeout < 0)
			timeout = 0;
		poll(pfd, n, timeout);

		// Held long enough: a long power press, always a full second.
		if (down && !power_sent && now_ms() - pressed_at >= HOLD_MS) {
			key(ui, KEY_POWER, 1);
			usleep(POWER_HOLD_MS * 1000);
			key(ui, KEY_POWER, 0);
			power_sent = 1;
		}

		for (int j = 0; j < n; j++) {
			if (pfd[j].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				close(pads[map[j]]);
				pads[map[j]] = -1;
				continue;
			}
			if (!(pfd[j].revents & POLLIN))
				continue;
			struct input_event ev;
			errno = 0;
			while (read(pads[map[j]], &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_KEY && ev.code == BTN_EAST) {
					/* Circle is not in the layout, so it gets no WAKE
					 * flag there: wake from here on press */
					if (ev.value == 1) {
						key(ui, KEY_WAKEUP, 1);
						key(ui, KEY_WAKEUP, 0);
					}
					if (ev.value == 0) {
						key(ui, KEY_BACK, 1);
						key(ui, KEY_BACK, 0);
					}
					continue;
				}
				if (ev.type != EV_KEY || ev.code != BTN_MODE)
					continue;
				if (ev.value == 1 && !down) {
					/* Wake the display first: Home is not a wake key, so
					 * with the display off nothing else could bring it
					 * back. Wakeup does nothing when already awake. */
					key(ui, KEY_WAKEUP, 1);
					key(ui, KEY_WAKEUP, 0);
					down = 1;
					power_sent = 0;
					pressed_at = now_ms();
				} else if (ev.value == 0 && down) {
					down = 0;
					if (!power_sent && send_home) {
						key(ui, KEY_HOMEPAGE, 1);
						key(ui, KEY_HOMEPAGE, 0);
					}
				}
			}
			if (errno != EAGAIN && errno != 0) {
				close(pads[map[j]]);
				pads[map[j]] = -1;
			}
		}
	}
}
