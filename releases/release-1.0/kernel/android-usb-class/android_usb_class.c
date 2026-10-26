// SPDX-License-Identifier: GPL-2.0
/*
 * Registers an empty "android_usb" device class. Android's UsbService only
 * creates its USB device manager when /sys/class/android_usb exists, and
 * Settings' Developer options fails without one. The PS4 has no USB device
 * (gadget) controller, so the class stays empty and Android reports no
 * USB functions.
 */
#include <linux/device/class.h>
#include <linux/err.h>
#include <linux/module.h>

static const struct class android_usb_class = {
	.name = "android_usb",
};

static int __init android_usb_class_init(void)
{
	return class_register(&android_usb_class);
}

static void __exit android_usb_class_exit(void)
{
	class_unregister(&android_usb_class);
}

module_init(android_usb_class_init);
module_exit(android_usb_class_exit);
MODULE_DESCRIPTION("Empty android_usb class for Android's USB service");
MODULE_LICENSE("GPL");
