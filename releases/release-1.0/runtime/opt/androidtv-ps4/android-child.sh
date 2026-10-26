#!/bin/sh
set -eu
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /android/proc
mount -t sysfs sysfs /android/sys
mkdir -p /android/sys/fs/selinux
mount -t selinuxfs selinuxfs /android/sys/fs/selinux
mount -t tmpfs -o mode=0755 tmpfs /android/dev
mkdir -p /android/dev/pts /android/dev/socket /android/dev/binderfs
mount -t devpts -o newinstance,ptmxmode=0666 devpts /android/dev/pts
for entry in 'null 1 3' 'zero 1 5' 'random 1 8' 'urandom 1 9' 'tty 5 0' 'console 5 1' 'kmsg 1 11'; do
    set -- $entry
    mknod -m 666 /android/dev/$1 c $2 $3
done
ln -s pts/ptmx /android/dev/ptmx
# Android init joins this namespace for the Bluetooth HAL before dropping UID.
touch /android/dev/atv-host-netns
mount --bind /run/androidtv-ps4/host-netns /android/dev/atv-host-netns
mount -t binder binder /android/dev/binderfs
/run/androidtv-ps4/binder-devices /android/dev/binderfs/binder-control
for name in binder hwbinder vndbinder; do
    ln -s binderfs/$name /android/dev/$name
    chmod 666 /android/dev/binderfs/$name
done
if grep -q ' /mnt/android-persistent ' /proc/mounts; then
    mkdir -p /mnt/android-persistent/data
    chmod 0771 /mnt/android-persistent/data
    chown 1000:1000 /mnt/android-persistent/data
    mount --bind /mnt/android-persistent/data /android/data
elif [ -f /run/android-data-preserved/system/packages.xml ]; then
    mount --bind /run/android-data-preserved /android/data
else
    mount -t tmpfs -o size=1536m,mode=0771,uid=1000,gid=1000 tmpfs /android/data
fi
mount -t tmpfs tmpfs /android/apex
mkdir -p /android/linkerconfig /android/metadata /android/mnt /android/debug_ramdisk
mount -t tmpfs tmpfs /android/linkerconfig
# Android remounts its root read-only; these runtime trees must remain writable.
if grep -q ' /mnt/android-persistent ' /proc/mounts; then
    mkdir -p /mnt/android-persistent/metadata
    mount --bind /mnt/android-persistent/metadata /android/metadata
elif [ -d /run/android-metadata-preserved/vold ]; then
    mount --bind /run/android-metadata-preserved /android/metadata
else
    mount -t tmpfs -o mode=0755 tmpfs /android/metadata
fi
for runtime in mnt debug_ramdisk cache; do
    mount -t tmpfs -o mode=0755 tmpfs /android/$runtime
done
mount --bind /run/androidtv-ps4/android-cmdline /android/proc/cmdline
ip link set lo up
/usr/sbin/ip link add dummy0 type dummy
/usr/sbin/ip link set dummy0 up
export ANDROID_ROOT=/system ANDROID_DATA=/data
echo 'Starting Android init in its own PID, mount, IPC, UTS and network namespaces'
# Apps select their own home directory; do not inherit the Linux shell root.
unset HOME
# Android 16's init (selinux_setup) remounts /second_stage_resources, a tmpfs
# that first-stage init normally creates. This runtime starts at the second
# stage, so mount it here; without it init aborts with EINVAL and reboots.
mkdir -p /android/second_stage_resources
mount -t tmpfs -o mode=0755 tmpfs /android/second_stage_resources
# Make Android's root the real root of its mount namespace, as first-stage init
# does with switch_root. Android 16's init switches back into this namespace
# (setns) and then uses absolute paths such as /bootstrap-apex; under a plain
# chroot that lands in the Linux initramfs instead of Android's root.
# A mount under a shared parent cannot be moved; this is Android's own namespace.
mount --make-rprivate /
cd /android
mount --move /android /
exec chroot . /system/bin/init selinux_setup
