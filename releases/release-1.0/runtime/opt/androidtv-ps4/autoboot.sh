#!/bin/sh
set -eu
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
assets=/opt/androidtv-ps4
stage() {
 message="[Android setup $(cut -d' ' -f1 /proc/uptime)s] $*"
 printf '%s\n' "$message" >> /run/androidtv-ps4/progress.log
 # A new Android framebuffer ends all console writes from this script.
 if [ ! -f /run/androidtv-ps4/display-ready ]; then printf '%s\n' "$message"; fi
}
# Hardware report for people who send logs back: which console, kernel, chips
# and drivers this start saw. Rewritten at each checkpoint so it is never stale.
hardware_report() {
 {
  echo "Android TV for PS4 hardware report (uptime $(cut -d' ' -f1 /proc/uptime)s)"
  echo "Build: $(sed -n 's/.*androidtv.boot_id=\([^ ]*\).*/\1/p' /proc/cmdline)"
  echo "Southbridge: ${southbridge:-unknown}"
  sdio_ids=" $(for d in /sys/bus/sdio/devices/*; do [ -r "$d/vendor" ] && printf '%s:%s ' "$(cat "$d/vendor")" "$(cat "$d/device")"; done 2>/dev/null) "
  case "$sdio_ids" in
   *' 0x037a:0x7608 '*) echo "Wi-Fi chip: MediaTek MT7668";;
   *' 0x02df:0x912d '*) echo "Wi-Fi chip: Marvell 88W8897";;
   *' 0x02df:0x9129 '*) echo "Wi-Fi chip: Marvell 88W8797";;
   *) echo "Wi-Fi chip: not recognised (see SDIO devices below)";;
  esac
  handover=$(grep -E 'handed to Android|could not be handed|No Wi-Fi interface' /run/androidtv-ps4/progress.log 2>/dev/null | tail -1 | sed 's/^\[[^]]*\] //')
  echo "Wi-Fi handed to Android: ${handover:-not yet (Android hadn't started when this was written)}"
  # Only the Baikal kernel registers the baikal_pcie driver.
  if [ -d /sys/bus/pci/drivers/baikal_pcie ]; then echo "Kernel image: bzImage-baikal (Baikal kernel)"
  else echo "Kernel image: bzImage (standard kernel)"; fi
  echo "Kernel: $(cat /proc/version)"
  echo "Command line: $(cat /proc/cmdline)"
  echo "CPU: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2-)"
  grep -E 'MemTotal|MemAvailable' /proc/meminfo
  echo "Storage partition: ${userpart:-not found}"
  echo; echo "PCI devices (vendor:device class driver):"
  for d in /sys/bus/pci/devices/*; do
   drv=$(basename "$(readlink "$d/driver" 2>/dev/null)" 2>/dev/null)
   echo " ${d##*/} $(cat "$d/vendor"):$(cat "$d/device") $(cat "$d/class") ${drv:-none}"
  done
  echo; echo "SDIO devices (Wi-Fi/Bluetooth chips):"
  for d in /sys/bus/sdio/devices/*; do
   [ -r "$d/vendor" ] || continue
   drv=$(basename "$(readlink "$d/driver" 2>/dev/null)" 2>/dev/null)
   echo " ${d##*/} $(cat "$d/vendor"):$(cat "$d/device") ${drv:-none}"
  done
  echo; echo "USB devices:"
  for d in /sys/bus/usb/devices/*; do
   [ -r "$d/idVendor" ] && echo " ${d##*/} $(cat "$d/idVendor"):$(cat "$d/idProduct") $(cat "$d/product" 2>/dev/null)"
  done
  echo; echo "Network interfaces:"
  for n in /sys/class/net/*; do
   drv=$(basename "$(readlink "$n/device/driver" 2>/dev/null)" 2>/dev/null)
   echo " ${n##*/} ${drv:-virtual} $(cat "$n/operstate" 2>/dev/null)"
  done
  echo " (Wi-Fi is moved into Android once it starts, so wlan0 may not be listed here.)"
  for p in /sys/class/ieee80211/*; do [ -e "$p" ] && echo " Wi-Fi radio ${p##*/}: $(basename "$(readlink "$p/device/driver" 2>/dev/null)" 2>/dev/null)"; done
  echo; echo "GPU and display:"
  gpu=$(dmesg | grep -m1 'initializing kernel modesetting' | grep -oE '\((LIVERPOOL|GLADIUS)[^)]*\)')
  echo " GPU found: ${gpu:-no (the amdgpu driver never saw a PS4 GPU)}"
  dmesg | grep -iE 'amdgpu.*(fail|error|timeout|ring .* test)|firmware.*(liverpool|gladius)|active_cu_number|Initialized amdgpu|amdgpudrmfb' | sed 's/^/ /'
  for c in /sys/class/drm/card*-*; do [ -e "$c/status" ] && echo " Output ${c##*/}: $(cat "$c/status")"; done
  if dmesg | grep -q 'amdgpudrmfb'; then echo " Verdict: the GPU driver is up and drawing; a black screen points at the HDMI output or the TV."
  elif dmesg | grep -q 'Initialized amdgpu'; then echo " Verdict: the GPU driver started but no picture was set up."
  elif [ -n "$gpu" ]; then echo " Verdict: the GPU was found but its driver failed; the lines above show where."
  else echo " Verdict: no GPU driver activity."; fi
  echo; echo "Bluetooth: $(ls /sys/class/bluetooth 2>/dev/null | tr '\n' ' ')"
  echo "Loaded modules:"; cut -d' ' -f1 /proc/modules 2>/dev/null | sed 's/^/ /'
  echo; echo "Driver and setup decisions:"; grep -E 'found|driver|GPU|Southbridge|Hardware|Wi-Fi|Ethernet|failed' /run/androidtv-ps4/progress.log 2>/dev/null || true
 } > /run/androidtv-ps4/hardware.txt 2>&1 || true
}
# A start that fails before Android launches explains itself, copies its
# reports to a USB stick if one is plugged in (the internal drive may be what
# failed) and unmounts cleanly, so holding the power button is then safe.
# A running Android is never interrupted.
installation=
# Every log from this start goes in one folder: logs/ in the install folder on
# the console, or AndroidTV-PS4-logs on a USB stick. The files are kept in
# /run/androidtv-ps4 while Linux runs and copied from there.
copy_logs() {
 mkdir -p "$1" || return 1
 # USB_LIGHT=1 (most USB cycles) leaves out the two big full logs, so the
 # stick is only mounted for about a second and is safe to pull.
 for f in /run/androidtv-ps4/*.log /run/androidtv-ps4/*.txt; do
  [ -e "$f" ] || continue
  case "${USB_LIGHT:-0}:${f##*/}" in 1:logcat-full.log|1:logcat-start.log) continue ;; esac
  cp "$f" "$1/" 2>/dev/null || true
 done
 [ ! -d /run/androidtv-ps4/crash ] || cp -r /run/androidtv-ps4/crash "$1/" 2>/dev/null || true
 cp /run/androidtv-ps4/display-ready "$1/display-ready.log" 2>/dev/null || true
 dmesg > "$1/kernel.log" 2>/dev/null || true
 if [ -n "$installation" ] && [ "$1" != "$installation/logs" ]; then
  cp "$installation/logs/launcher.log" "$1/" 2>/dev/null || true
 fi
 sync
}
export_reports() {
 for block in /sys/class/block/sd*; do
  name=${block##*/}
  # A whole stick with partitions: use the partition, not the disk, or the
  # kernel prints exFAT/FAT probe errors on every copy.
  ls -d "$block"/"$name"[0-9]* >/dev/null 2>&1 && continue
  # Never write to the console's own drive (the disk with partition 27); some
  # of its partitions are FAT. This holds even when the drive was never found.
  [ -b "/dev/${name%%[0-9]*}27" ] && continue
  mkdir -p /mnt/usb-report
  mount -t vfat "/dev/$name" /mnt/usb-report 2>/dev/null || mount -t exfat "/dev/$name" /mnt/usb-report 2>/dev/null || continue
  if copy_logs /mnt/usb-report/AndroidTV-PS4-logs; then
   umount /mnt/usb-report
   stage 'Logs saved to the USB stick in AndroidTV-PS4-logs'
   return 0
  fi
  umount /mnt/usb-report 2>/dev/null || true
 done
 return 1
}
# Dev build: mirror the logs folder to a USB stick on every cycle, so a hang
# or black screen still leaves everything on the stick. One writer per stick:
# if Android has mounted it (USB storage), write through Android's own mount;
# otherwise mount it here just for the copy. Never the console's own drive.
usb_mirror() {
 init_pid=$(cat /run/androidtv-ps4/android-init.pid 2>/dev/null)
 for block in /sys/class/block/sd*; do
  name=${block##*/}
  # A whole stick with partitions: use the partition, not the disk, or the
  # kernel prints exFAT/FAT probe errors on every copy.
  ls -d "$block"/"$name"[0-9]* >/dev/null 2>&1 && continue
  [ -b "/dev/${name%%[0-9]*}27" ] && continue
  [ -r "$block/dev" ] || continue
  devno=$(cat "$block/dev")
  android_mount=
  if [ -n "$init_pid" ] && [ -r "/proc/$init_pid/mountinfo" ]; then
   android_mount=$(awk -v d="$devno" '$3 == d { print $5; exit }' "/proc/$init_pid/mountinfo")
  fi
  if [ -n "$android_mount" ]; then
   copy_logs "/proc/$init_pid/root$android_mount/AndroidTV-PS4-logs" && return 0
   continue
  fi
  mkdir -p /mnt/usb-report
  mount -t vfat "/dev/$name" /mnt/usb-report 2>/dev/null || mount -t exfat "/dev/$name" /mnt/usb-report 2>/dev/null || continue
  copy_logs /mnt/usb-report/AndroidTV-PS4-logs
  umount /mnt/usb-report 2>/dev/null
  return 0
 done
 return 1
}
stop_safely() {
 [ ! -f /run/androidtv-ps4/android-launched ] || return 0
 # Stop the background log copy before the drive is unmounted.
 kill "$(cat /run/androidtv-ps4/keep-logs.pid 2>/dev/null)" 2>/dev/null || true
 sync
 for mount in /mnt/android-bundled-apps /mnt/android-persistent /android /mnt/android-lower /mnt/android-sfs /mnt/android-share /mnt/ps4-user; do
  umount "$mount" 2>/dev/null || true
 done
 cryptsetup close ps4-android-user 2>/dev/null || true
 stage 'Android TV could not start. It is now safe to hold the power button to restart the PS4.'
 # Keep watching for a USB stick so the logs can still be saved after the fact.
 if ! export_reports; then
  stage 'To keep the logs, plug in a FAT or exFAT USB stick now'
  # Wait for as long as it takes; the console is idle until it is turned off.
  until export_reports; do sleep 5; done
 fi
}
# Dev build: when Android does not start, stay up so the cause can be read
# live, then restart to the PS4 by itself. If the install folder has a file
# debug-host ("IP PORT"), Linux takes a DHCP address on the wired port and
# connects out to that one computer with a shell for up to 20 minutes, or
# until /run/androidtv-ps4/debug-done exists. Nothing listens on the network.
dev_net_up() {
 dev=eth0; [ -e /sys/class/net/eth0 ] || dev=psx0
 [ -e "/sys/class/net/$dev" ] || return 1
 cat > /run/androidtv-ps4/udhcpc.sh <<'SCRIPT'
#!/bin/sh
case "$1" in bound|renew)
 ip addr flush dev "$interface"
 ip addr add "$ip/${mask:-24}" dev "$interface"
 [ -z "$router" ] || ip route replace default via "${router%% *}" dev "$interface";;
esac
SCRIPT
 chmod 755 /run/androidtv-ps4/udhcpc.sh
 ip link set "$dev" up
 udhcpc -i "$dev" -s /run/androidtv-ps4/udhcpc.sh -q -n -t 10 -T 2 >/dev/null 2>&1
}
dev_recover() {
 [ ! -f /run/androidtv-ps4/dev-recover ] || return 0
 touch /run/androidtv-ps4/dev-recover
 set +e
 host=
 [ -z "$installation" ] || [ ! -r "$installation/debug-host" ] || host=$(cat "$installation/debug-host")
 stage 'Dev build: staying up to save the logs; it restarts to the PS4 by itself'
 # About 30 seconds of copies, so late kernel messages reach the stick too.
 for copy in 1 2 3 4 5 6; do usb_mirror >/dev/null 2>&1; sleep 5; done
 wait_s=0
 if [ -n "$host" ] && dev_net_up; then
  stage "Dev build: debug shell connecting to $host for up to 20 minutes"
  ( while [ ! -f /run/androidtv-ps4/debug-done ]; do nc -w 10 ${host% *} ${host#* } -e /bin/sh; sleep 3; done ) </dev/null >/dev/null 2>&1 &
  wait_s=1200
 fi
 waited=0
 while [ "$waited" -lt "$wait_s" ] && [ ! -f /run/androidtv-ps4/debug-done ]; do
  sleep 5; waited=$((waited + 5))
  [ $((waited % 30)) -ne 0 ] || usb_mirror >/dev/null 2>&1
 done
 kill "$(cat /run/androidtv-ps4/keep-logs.pid 2>/dev/null)" 2>/dev/null
 hardware_report
 [ -z "$installation" ] || copy_logs "$installation/logs"
 usb_mirror >/dev/null 2>&1
 stage 'Dev build: restarting to the PS4'
 sync
 for mount in /mnt/android-bundled-apps /mnt/android-persistent /android /mnt/android-lower /mnt/android-sfs /mnt/android-share /mnt/ps4-user; do
  umount "$mount" 2>/dev/null
 done
 sync
 sleep 2
 reboot -f
}
on_exit() {
 status=$?
 hardware_report
 if [ -n "$installation" ] && [ -d "$installation" ]; then save_logs "$status"; fi
 if [ "$status" -ne 0 ]; then
  stage "SETUP FAILED (status $status, build $(sed -n 's/.*androidtv.boot_id=\([^ ]*\).*/\1/p' /proc/cmdline)); data preserved"
  dev_recover
  stop_safely
 fi
}
trap on_exit EXIT
touch /run/androidtv-ps4/bootstrap-active
# If the GPU never gives a picture, the screen stays black and the power button
# is the only way out. The picture normally appears a few seconds after Linux
# starts, so after a minute without one: save the logs to the console and any
# USB stick, flush the PS4 drive, and restart to the PS4 system. A file called
# test-no-picture in the install folder makes this act as if there were no
# picture, to test it on a console where the picture works.
display_watchdog() {
 set +e
 sleep 60
 test_file=/mnt/ps4-user/app/ps4-android16/test-no-picture
 if [ -e /dev/fb0 ] && [ ! -e "$test_file" ]; then return 0; fi
 rm -f "$test_file"
 stage 'No picture after 60 seconds; saving the logs and restarting to the PS4'
 kill "$(cat /run/androidtv-ps4/keep-logs.pid 2>/dev/null)" 2>/dev/null
 hardware_report
 if grep -q ' /mnt/ps4-user ' /proc/mounts; then copy_logs /mnt/ps4-user/app/ps4-android16/logs; fi
 export_reports
 sync
 sleep 2
 reboot -f
}
display_watchdog </dev/null >/dev/null 2>&1 &
# The internal drive is the disk with Sony's partition 27; no USB stick has
# one. A stick left in from installing the PKG can take the sda name first.
internal_disk() {
 for disk in /sys/block/sd*; do
  [ -b "/dev/${disk##*/}27" ] || continue
  echo "/dev/${disk##*/}27"; return 0
 done
 return 1
}
stage 'Waiting for internal PS4 disk'
userpart=
for attempt in $(seq 1 60); do
 userpart=$(internal_disk || true)
 [ -z "$userpart" ] || break
 if [ "$((attempt % 5))" = 0 ]; then stage "Still waiting for internal disk ($attempt seconds)"; fi
 sleep 1
done
[ -n "$userpart" ] || { stage 'Internal disk did not appear within 60 seconds'; exit 1; }
stage 'Opening internal PS4 storage (disk encryption and filesystem)'
[ -r /key/eap_hdd_key.bin ]
# Name the southbridge from its PCI ID. Driver names say "aeolia" on every
# model, so dmesg can't tell them apart.
southbridge=unknown
for device in /sys/bus/pci/devices/*; do
 [ "$(cat "$device/vendor" 2>/dev/null)" = 0x104d ] || continue
 case "$(cat "$device/device" 2>/dev/null)" in
  0x908f) southbridge=aeolia;; 0x90c8) southbridge=belize;; 0x90d7) southbridge=baikal;;
 esac
done
stage "Southbridge: $southbridge"
# One line describing this console's hardware, for test reports.
sdio=$(for d in /sys/bus/sdio/devices/*; do [ -r "$d/vendor" ] && printf '%s:%s ' "$(cat "$d/vendor")" "$(cat "$d/device")"; done 2>/dev/null || true)
stage "Hardware: storage=$userpart sdio=[${sdio% }] net=[$(ls /sys/class/net 2>/dev/null | tr '\n' ' ' | sed 's/ $//')]"
# Baikal's Ethernet controller (104d:90d8) is not a Yukon. It uses linux-psx's
# polled MTS driver, built for the Baikal kernel, which names it psx0.
for device in /sys/bus/pci/devices/*; do
 [ "$(cat "$device/vendor" 2>/dev/null)" = 0x104d ] && [ "$(cat "$device/device" 2>/dev/null)" = 0x90d8 ] || continue
 stage 'Baikal Ethernet found; loading its driver'
 insmod /lib/modules/$(uname -r)/extra/psx_mts_net.ko autoarm=1 || stage 'Baikal Ethernet driver failed to load; continuing without Ethernet'
done
mkdir -p /mnt/ps4-user /mnt/android-share /mnt/android-sfs /mnt/android-lower /mnt/android-persistent /android
# The user partition is AES-XTS with the EAP key. From the CUH-1200 on
# (Belize and Baikal) the sector tweak also carries the partition index in
# its upper 32 bits, so cryptsetup needs --skip (27-1)<<32; the first Aeolia
# fats use plain sector numbers. Try this southbridge's layout first, then the
# other: with the wrong one the mount just refuses, nothing is written.
open_user() {
 stage "Unlocking PS4 storage (cryptsetup, IV offset $1)"
 cryptsetup open --type plain --cipher aes-xts-plain64 --key-size 256 --offset 0 --skip "$1" --key-file /key/eap_hdd_key.bin "$userpart" ps4-android-user || return 1
 stage "Unlocked; mounting the PS4 user filesystem"
 mount -t ufs -o ufstype=ufs2,noatime /dev/mapper/ps4-android-user /mnt/ps4-user 2>/dev/null && return 0
 cryptsetup close ps4-android-user || true
 return 1
}
if ! grep -q ' /mnt/ps4-user ' /proc/mounts; then
 tweaked=$(( (${userpart##*[!0-9]} - 1) << 32 ))
 if [ "$southbridge" = aeolia ]; then order="0 $tweaked"; else order="$tweaked 0"; fi
 opened=
 for skip in $order; do
  if open_user "$skip"; then opened=$skip; break; fi
  stage "PS4 storage did not open with IV offset $skip"
 done
 [ -n "$opened" ] || { stage 'Could not open the PS4 user partition with either encryption layout'; exit 1; }
 stage "PS4 storage opened (IV offset $opened)"
fi
# Linux starts with the clock at 1 January 1970: the kernel finds no clock it
# can read. Android only corrects it once it reaches the internet, and Google
# sign-in fails before then. The launcher wrote logs/launcher.log seconds
# before the handover, so its timestamp on the PS4 drive is close to the real
# time; set the clock from it, plus the time Linux has been running since.
if [ "$(date +%Y)" -lt 2024 ] && [ -f /mnt/ps4-user/app/ps4-android16/logs/launcher.log ]; then
 launched=$(stat -c %Y /mnt/ps4-user/app/ps4-android16/logs/launcher.log 2>/dev/null || echo 0)
 if [ "$launched" -gt 1700000000 ]; then
  date -s "@$((launched + $(cut -d. -f1 /proc/uptime)))" >/dev/null && stage "Clock set from the PS4: $(date -u '+%Y-%m-%d %H:%M UTC')"
 fi
fi
installation=/mnt/ps4-user/app/ps4-android16
# Later Slim and Pro models (CUH-7215/7216, Belize and Baikal) use a MediaTek
# MT7668 for Wi-Fi. Its driver only loads when that chip is present. Driver
# and firmware live on the drive, not in this initramfs, to keep boot memory low.
mt7668=
case " $sdio " in
 *' 0x037a:0x7608 '*)
  stage 'MediaTek MT7668 Wi-Fi found; loading its driver'
  if tar -xzf "$installation/mt7668-wifi.tar.gz" -C / && insmod /lib/modules/$(uname -r)/extra/wlan_mt76x8_sdio.ko; then mt7668=1
  else stage 'MT7668 Wi-Fi driver failed to load; continuing without Wi-Fi'; fi
  ;;
esac
# The last start's logs move to logs/previous. The launcher has already moved
# its own launcher.log there before starting Linux.
mkdir -p "$installation/logs/previous"
for log in "$installation"/logs/*; do
 if [ -f "$log" ] && [ "${log##*/}" != launcher.log ]; then mv -f "$log" "$installation/logs/previous/"; fi
done
# Builds before 1.1-b132 left their logs loose in the install folder.
for old in launcher-boot.log launcher-boot.prev.log boot.log boot.log.previous kernel.log kernel.log.previous \
 boot-progress.log hardware.txt android-boot.log android-boot.log.previous android-stages.log android-stages.log.previous \
 quota-upgrade.log display-ready.log display-watch.log animation-display.log running.log running.log.part kernel-running.log; do
 rm -f "$installation/$old"
done
save_logs() {
 copy_logs "$installation/logs"
 rm -f /run/androidtv-ps4/bootstrap-active
}
# From here until the console is restarted or shut down, keep the logs folder
# current: every 10 seconds while Android starts, so a hang still leaves its
# logs, then every minute. Once Android runs, running.log also records its
# Wi-Fi interface and recent Wi-Fi and network messages.
# Dev build: purple lights (Codey's choice). The console LED shows violet-blue
# once Android starts, and every controller light bar is kept purple.
lights() {
 set +e
 led=/sys/class/leds
 # The ICC boot step turns the LED blue without the LED driver knowing, so
 # switch to another colour first or the driver skips the request.
 echo 255 > "$led/ps4:white:status/brightness"
 echo 255 > "$led/ps4:violet_blue:status/brightness"
 while [ ! -f /run/androidtv-ps4/power-request ]; do
  for red in "$led"/input*:red; do
   [ -e "$red/brightness" ] || continue
   pad=${red%:red}
   [ "$(cat "$red/brightness")" = 150 ] && [ "$(cat "$pad:blue/brightness")" = 255 ] && continue
   echo 150 > "$red/brightness"; echo 0 > "$pad:green/brightness"; echo 255 > "$pad:blue/brightness"
  done
  sleep 2
 done
}
keep_logs() {
 set +e
 while [ ! -f /run/androidtv-ps4/power-request ]; do
  # Wake every second so a stop request is answered at once.
  if [ -f /run/androidtv-ps4/android.pid ]; then wait_for=60; else wait_for=5; fi
  while [ "$wait_for" -gt 0 ] && [ ! -f /run/androidtv-ps4/power-request ]; do
   sleep 1; wait_for=$((wait_for - 1))
  done
  [ ! -f /run/androidtv-ps4/power-request ] || break
  grep -q ' /mnt/ps4-user ' /proc/mounts || break
  child=$(cat /run/androidtv-ps4/android-init.pid 2>/dev/null)
  if [ -n "$child" ]; then
   {
    echo "Running log at $(cut -d' ' -f1 /proc/uptime) s, build $(sed -n 's/.*androidtv.boot_id=\([^ ]*\).*/\1/p' /proc/cmdline)"
    echo; echo "Wi-Fi in Android:"
    timeout 5 nsenter -t "$child" -n /usr/sbin/iw dev
    timeout 5 nsenter -t "$child" -n /usr/sbin/ip addr show wlan0
    echo; echo "Recent Android Wi-Fi and network messages:"
    timeout 10 nsenter -t "$child" -m -p -n /system/bin/logcat -d -b main,system -t 2000 2>/dev/null | grep -iE 'wifi|wpa|supplicant|dhcp|connectivity|netd|wlan' | tail -200
   } > /run/androidtv-ps4/running.log.part 2>&1
   mv /run/androidtv-ps4/running.log.part /run/androidtv-ps4/running.log
   # Google Play services and Play Store warnings and errors only, for sign-in
   # problems. No other app's messages. Email addresses and anything that looks
   # like a token or key are removed before the file is written.
   timeout 10 nsenter -t "$child" -m -p -n /system/bin/logcat -d -b main,system -t 3000 '*:W' 2>/dev/null |
    grep -iE 'gms|gsf|vending|finsky|checkin|googleauth|gls|auth' |
    sed -E -e 's/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/<removed>/g' -e 's/[A-Za-z0-9_+\/=-]{24,}/<removed>/g' |
    tail -300 > /run/androidtv-ps4/google-services.tmp
   mv /run/androidtv-ps4/google-services.tmp /run/androidtv-ps4/google-services.log
   # Internal dev build only: the whole recent Android log, for bring-up.
   timeout 10 nsenter -t "$child" -m -p -n /system/bin/logcat -d -b main,system,crash,events -t 3000 > /run/androidtv-ps4/logcat.tmp 2>&1
   mv /run/androidtv-ps4/logcat.tmp /run/androidtv-ps4/logcat.log
   # The newest 4 MB of the full log Lineage writes from early boot.
   tail -c 4000000 "/proc/$child/root/data/log.txt" > /run/androidtv-ps4/logcat-full.tmp 2>/dev/null &&
    mv /run/androidtv-ps4/logcat-full.tmp /run/androidtv-ps4/logcat-full.log
   # And the first 6 MB, so the first failure survives any restarts after it.
   head -c 6000000 "/proc/$child/root/data/log.txt" > /run/androidtv-ps4/logcat-start.tmp 2>/dev/null &&
    mv /run/androidtv-ps4/logcat-start.tmp /run/androidtv-ps4/logcat-start.log
   # Every record Android keeps of a system process death: native crash
   # tombstones, watchdog/ANR thread dumps and the dropbox crash archive.
   for kind in tombstones anr system/dropbox; do
    src="/proc/$child/root/data/$kind"
    [ -d "$src" ] || continue
    mkdir -p "/run/androidtv-ps4/crash/${kind#system/}"
    cp -p "$src"/* "/run/androidtv-ps4/crash/${kind#system/}/" 2>/dev/null || true
   done
  fi
  hardware_report
  copy_logs "$installation/logs"
  # Big full logs to the stick every 6th cycle (30 s); the rest every cycle.
  usb_cycle=$(( ${usb_cycle:-0} + 1 ))
  if [ $((usb_cycle % 6)) -eq 0 ]; then usb_mirror || true; else USB_LIGHT=1 usb_mirror || true; fi
 done
 # Android stopped or a restart was asked for: one last copy, with the kernel
 # log that says why, to the logs folder and to a USB stick if one is in.
 if [ -f /run/androidtv-ps4/power-request ]; then
  stage "Android stopped ($(cat /run/androidtv-ps4/power-request)); saving final logs"
  hardware_report
  copy_logs "$installation/logs"
  export_reports || true
  sync
 fi
 rm -f /run/androidtv-ps4/bootstrap-active
}
keep_logs </dev/null >/dev/null 2>&1 &
echo $! > /run/androidtv-ps4/keep-logs.pid
[ -f "$installation/ready" ] || { stage 'Package setup incomplete'; exit 1; }
grep -q '^PS4ANDROID13-SPARSE-V015$' "$installation/ready"
capacity=$(cat "$installation/data-gib.txt")
case "$capacity" in 32|64|128) ;; *) stage 'Invalid data capacity'; exit 1;; esac
# Keep spare physical disk space for both operating systems.
available=$(df -Pk "$installation" | tail -1 | awk '{print $4}')
[ "$available" -gt 2097152 ] || { stage 'Less than 2 GiB physical storage remains'; exit 1; }
# ext4 quota features need both CONFIG_QUOTA and CONFIG_QFMT_V2; an image
# with them will not mount on a kernel without both. Check the running kernel.
kernel_has_quota() {
 zcat /proc/config.gz 2>/dev/null | grep -q '^CONFIG_QUOTA=y' &&
 zcat /proc/config.gz 2>/dev/null | grep -q '^CONFIG_QFMT_V2=y'
}
quota_features=
if kernel_has_quota; then quota_features='-O quota,project'; quota_type=',quotatype=usrquota:grpquota:prjquota'; else quota_type=; fi
if [ ! -f "$installation/data-ready" ]; then
 [ ! -e "$installation/data.img" ] || { stage 'Uncommitted data image preserved; recovery needed'; exit 1; }
 [ ! -e "$installation/data.img.part" ] || { stage 'Interrupted creation preserved; recovery needed'; exit 1; }
 stage "Creating fresh $capacity GiB Android storage"
 truncate -s "$((capacity * 1073741824))" "$installation/data.img.part"
 # Project quotas let Android tag each app's shared-storage folder (vold).
 # shellcheck disable=SC2086
 /usr/sbin/mke2fs -t ext4 -F -m 0 $quota_features -E nodiscard,lazy_itable_init=1,lazy_journal_init=1$quota_type "$installation/data.img.part"
 /usr/sbin/e2fsck -fn "$installation/data.img.part"
 sync
 mv "$installation/data.img.part" "$installation/data.img"
 printf '%s\n' 'PS4ANDROID-DATA-V1' > "$installation/data-ready.next"
 sync
 mv "$installation/data-ready.next" "$installation/data-ready"
 sync
 stage 'Fresh Android storage created'
fi
mount --bind "$installation" /mnt/android-share
stage 'Mounting compressed Android system from internal storage'
mount -t squashfs -o ro,loop "$installation/system.sfs" /mnt/android-sfs
mount -t ext4 -o ro,loop,noload /mnt/android-sfs/system.img /mnt/android-lower
mkdir -p /run/android-overlay/upper /run/android-overlay/work
mount -t overlay overlay -o lowerdir=/mnt/android-lower,upperdir=/run/android-overlay/upper,workdir=/run/android-overlay/work /android
[ "$(stat -c %s "$installation/data.img")" = "$((capacity * 1073741824))" ] || { stage 'Data image size does not match selected edition; data preserved'; exit 1; }
# Images created before 1.1 have no project quotas, so vold failed to tag
# app folders and apps were refused their Android/data folder. Add the
# feature once, offline, checking the image before and after. If anything
# fails, carry on without it; the data itself is never reformatted.
if [ -n "$quota_features" ] && ! /usr/sbin/tune2fs -l "$installation/data.img" 2>/dev/null | grep -q '^Filesystem features:.* project'; then
 stage 'Adding project quota support to Android storage (one time)'
 if /usr/sbin/e2fsck -fy "$installation/data.img" >/run/androidtv-ps4/quota-upgrade.log 2>&1 &&
    /usr/sbin/tune2fs -O quota,project -Q usrquota,grpquota,prjquota "$installation/data.img" >>/run/androidtv-ps4/quota-upgrade.log 2>&1 &&
    /usr/sbin/e2fsck -fy "$installation/data.img" >>/run/androidtv-ps4/quota-upgrade.log 2>&1; then
  stage 'Project quota support added'
 else
  stage 'Could not add project quota support; continuing without it (see quota-upgrade.log)'
 fi
 sync
fi
stage 'Mounting Android app storage'
mount -t ext4 -o loop,noatime "$installation/data.img" /mnt/android-persistent
# Dev build: a hard power-off mid-boot can leave Android's runtime permission
# store truncated, and Android 16 then stops system_server on every start
# ("Missing <runtime-permissions>"). Move a damaged one aside; Android rebuilds it.
perm=/mnt/android-persistent/data/misc_de/0/apexdata/com.android.permission/runtime-permissions.xml
if [ -f "$perm" ] && ! grep -aq 'runtime-permissions' "$perm"; then
 mv "$perm" "$perm.broken-$(date +%s)"
 stage 'Android permission store was damaged; moved aside so Android rebuilds it'
fi
stage 'System and userdata mounted; preparing graphics and media'
# Android 16 dev: Lineage ships its own ARM translation (ndk_translation).
cp "$assets/system-build.prop" /android/system/build.prop
cp "$assets/vendor-build.prop" /android/system/vendor/build.prop
cp "$assets/init.android_x86_64.rc" /android/system/vendor/etc/init/init.android_x86_64.rc
cp "$assets/product-build.prop" /android/system/product/etc/build.prop
# The kernel has no project quotas or case-insensitive ext4 (see build.prop:
# external_storage.projid/casefold are 0), and this build's C library has no
# "virtualmachine" group. The permission mapped to that group gave apps an
# invalid group (-1), so they could not start. The PS4 has no virtual machines.
sed -i '/<permission name="android.permission.MANAGE_VIRTUAL_MACHINE">/,/<\/permission>/d' /android/system/etc/permissions/platform.xml
! grep -q '"virtualmachine"' /android/system/etc/permissions/platform.xml || stage 'Warning: could not remove the virtualmachine group mapping'
cp "$assets/init.sh" /android/system/etc/init.sh
cp "$assets/androidtv-ps4.rc" /android/system/etc/init/androidtv-ps4.rc
# OpenGL ES shaders are compiled with ACO, the compiler Vulkan uses. Mesa's
# default (LLVM) emits a copy for every value carried around a loop on this
# GPU, so loop-heavy shaders ran at half speed. gpu-llvm in the install folder
# switches back if ACO draws something wrong.
# gpu-debug in the install folder replaces the whole setting (letters, digits
# and commas only), for GPU tests without reinstalling.
if [ -f "$installation/gpu-debug" ]; then
 gpu_debug=$(head -c 200 "$installation/gpu-debug" | tr -cd 'a-z0-9,')
 sed -i "s/AMD_DEBUG notiling,useaco/AMD_DEBUG ${gpu_debug:-none}/" /android/system/etc/init/androidtv-ps4.rc
 stage "GPU settings: ${gpu_debug:-none} (gpu-debug found)"
elif [ -f "$installation/gpu-llvm" ]; then
 sed -i 's/AMD_DEBUG notiling,useaco/AMD_DEBUG notiling/' /android/system/etc/init/androidtv-ps4.rc
 stage 'Shader compiler: LLVM (gpu-llvm found)'
else
 stage 'Shader compiler: ACO'
fi
cp "$assets/androidtv-logging.rc" /android/system/etc/init/androidtv-logging.rc
cp "$assets/device-name.rc" /android/system/etc/init/androidtv-device-name.rc
cp "$assets/device-name.sh" /android/system/bin/androidtv-device-name.sh
chmod 644 /android/system/etc/init/androidtv-device-name.rc /android/system/bin/androidtv-device-name.sh
mkdir -p /android/system/vendor/etc/seccomp_policy
chmod 644 /android/system/etc/init/androidtv-logging.rc
# Keep native APKs on internal storage, outside the small initramfs.
# Signed APKs, splits and native libraries have their normal Android layout.
# The read-only app filesystem stays on disk, outside the RAM overlay.
# Android 16 dev: Lineage's own apps, gralloc and Bluetooth. Only the display
# composer comes from 13, because Lineage's hwc3 needs atomic modesetting and
# the PS4 GPU only has the legacy display path.
# Android 16 runs /system/xbin/overlay_remounter when its root is an overlay
# (always, here). The real one remounts every overlay read-only, which fails
# on Android's own root; this one goes straight to second-stage init.
cp "$assets/overlay_remounter" /android/system/xbin/overlay_remounter
chmod 755 /android/system/xbin/overlay_remounter
cp "$assets/bootanimation.zip" /android/system/product/media/bootanimation.zip
chmod 644 /android/system/product/media/bootanimation.zip
rm -f /android/system/vendor/etc/init/hwc3-drm.rc /android/system/vendor/etc/vintf/manifest/hwc3-drm.xml
# No A/B slots here: hide the boot-control HAL, as Lineage's own initrd does
# without androidboot.slot_suffix. Otherwise vold waits for it forever, and
# with it the storage manager and the whole of system_server.
rm -f /android/system/vendor/etc/vintf/manifest/android.hardware.boot@1.2.xml
cp "$assets/privapp-permissions-ps4-gapps.xml" /android/system/product/etc/permissions/privapp-permissions-ps4-gapps.xml
chmod 644 /android/system/product/etc/permissions/privapp-permissions-ps4-gapps.xml
cp "$assets/composer" /android/system/vendor/bin/hw/android.hardware.graphics.composer@2.1-drmfb-service
chmod 755 /android/system/vendor/bin/hw/android.hardware.graphics.composer@2.1-drmfb-service
cp "$assets/libatv-display-modes.so" /android/system/vendor/lib64/libatv-display-modes.so
cp "$assets/libhidltransport.so" /android/system/vendor/lib64/libhidltransport.so
cp "$assets/hwcomposer.rc" /android/system/vendor/etc/init/android.hardware.graphics.composer@2.1-drmfb-service.rc
cp "$assets/composer-drmfb.xml" /android/system/vendor/etc/vintf/manifest/atv-composer-drmfb.xml
chmod 644 /android/system/vendor/lib64/libatv-display-modes.so /android/system/vendor/lib64/libhidltransport.so /android/system/vendor/etc/init/android.hardware.graphics.composer@2.1-drmfb-service.rc /android/system/vendor/etc/vintf/manifest/atv-composer-drmfb.xml
# Lineage's own tables offer USB disks (fstab.lineage_x86_64_tv) or every disk
# (fstab.internal.x86, used with INTERNAL_MOUNT) to Android. A fat PS4's drive
# sits on a USB path, so neither may ever be used: ours replaces the first and
# the second is removed.
rm -f /android/system/vendor/etc/fstab.internal.x86
printf '%s\n' 'none /cache tmpfs noatime,nosuid,nodev,mode=0770,uid=1000,gid=2001 defaults' > /android/fstab.lineage_x86_64_tv
# USB sticks show up in Android as USB storage. The PS4's own drive can sit
# behind the USB controller too (it does on fat Aeolia consoles), and Android
# must never offer to format it, so the port it's on is left out: every other
# USB port gets a rule, and that hub only gets the other ports.
internal_sys=$(readlink -f "/sys/class/block/${userpart#/dev/}" 2>/dev/null || true)
internal_port=$(printf '%s' "$internal_sys" | grep -oE '/usb[0-9]+/[0-9]+-[0-9]+' | head -1 || true)
# If the drive's location can't be read, add no USB rules at all.
[ -n "$internal_sys" ] && [ -n "$userpart" ] || internal_port=unknown
for root in /sys/bus/usb/devices/usb*; do
 [ "$internal_port" != unknown ] || break
 hub=${root##*/usb}
 case "$internal_port" in
  "/usb$hub/$hub-"[0-9]) rule="/devices/*/usb$hub/$hub-[!${internal_port##*-}]*" ;;
  "/usb$hub/"*) continue ;;
  *) rule="/devices/*/usb$hub/*" ;;
 esac
 printf '%s\t\tauto\tauto\tdefaults\tvoldmanaged=usb:auto,noemulatedsd\n' "$rule" >> /android/fstab.lineage_x86_64_tv
done
# Check the rules against the PS4 drive's real path, the way vold matches
# them. If any rule would match it, remove every USB rule.
internal_dev=${internal_sys#/sys}; internal_dev=${internal_dev%/*}
while read -r pattern _; do
 case "$pattern" in /devices/*) ;; *) continue ;; esac
 case "$internal_dev" in $pattern)
  sed -i '/voldmanaged/d' /android/fstab.lineage_x86_64_tv
  stage 'USB storage for Android turned off: a rule matched the PS4 drive'
  break ;;
 esac
done < /android/fstab.lineage_x86_64_tv
# Dev build: USB sticks stay with Linux, which mirrors the logs and unmounts
# after every copy. When Android also mounted the stick it was never
# unmounted, so pulling it damaged the FAT filesystem.
sed -i '/voldmanaged/d' /android/fstab.lineage_x86_64_tv
stage 'USB sticks kept for logs (not offered to Android in this dev build)'
cp "$assets/android-child.sh" /run/androidtv-ps4/android-child.sh
cp "$assets/binder-devices" /run/androidtv-ps4/binder-devices
cp "$assets/xt_quota2.ko" /run/androidtv-ps4/xt_quota2.ko
cp "$assets/android_usb_class.ko" /run/androidtv-ps4/android_usb_class.ko
# Android 16 dev: Lineage's own Mesa 26 runs unmodified. It identifies the
# PS4 GPU as Kaveri, like 13's; the Liverpool raster-config patch 13 applies
# to its Mesa isn't ported yet, so some depth/stencil drawing may glitch.
gpu=$(cat /sys/bus/pci/devices/*/device 2>/dev/null | grep -m1 -E '^0x992[0-4]$' || true)
stage "GPU: ${gpu:-unknown}"
sh "$assets/controller-home.sh"
stage 'Launching Android 16 init (internal dev)'
set -eu
chmod 755 /run/androidtv-ps4/android-child.sh /run/androidtv-ps4/binder-devices
if ! grep -q '^xt_quota2 ' /proc/modules; then
    insmod /run/androidtv-ps4/xt_quota2.ko
fi
# Android's USB service needs /sys/class/android_usb before it starts, or
# Developer options fails. The class stays empty: the PS4 has no USB gadget.
if [ ! -d /sys/class/android_usb ]; then
    insmod /run/androidtv-ps4/android_usb_class.ko || stage 'android_usb class unavailable; Developer options may not open'
fi
# Per-install identifier; no developer identity or fabricated hardware serial.
if [ ! -s "$installation/device-id" ]; then
 od -An -N8 -tx1 /dev/urandom | tr -d ' \n' > "$installation/device-id"
 sync
fi
serial=$(cat "$installation/device-id")
printf '%s\n' "androidboot.hardware=lineage_x86_64_tv INTERNAL_MOUNT=0 androidboot.selinux=permissive androidboot.fake_battery=1 androidboot.enable_console=0 androidboot.serialno=ps4${serial} HWC_HIDL=drmfb" > /run/androidtv-ps4/android-cmdline
echo on > /proc/sys/kernel/printk_devkmsg
echo 7 > /proc/sys/kernel/printk
trap '' HUP
stage 'Starting Android graphics at up to 60 Hz; waiting for its first display frame'
if [ ! -f /mnt/android-persistent/data/system/packages.xml ]; then
 stage 'First start: Android must prepare its bundled apps before setup opens'
fi
/usr/sbin/androidtv-display-ready </dev/null >/run/androidtv-ps4/display-watch.log 2>&1 &
watcher=$!
while [ ! -f /run/androidtv-ps4/display-watch-ready ]; do
 kill -0 "$watcher" 2>/dev/null || { stage 'Display observer unavailable; startup logs remain enabled'; break; }
 sleep 0.05
done
touch /run/androidtv-ps4/allow-console-power
# Retain only the host network namespace for the HCI HAL.
# No host address, remote server, credentials, or external boot dependency.
touch /run/androidtv-ps4/host-netns
mount --bind /proc/self/ns/net /run/androidtv-ps4/host-netns
# From here a failure leaves Android running; no clean-up stops it.
hardware_report
touch /run/androidtv-ps4/android-launched
lights </dev/null >/dev/null 2>&1 &
# PS button: short press Home, hold for the power menu (Restart / Power off).
/opt/androidtv-ps4/ps-button </dev/null >/run/androidtv-ps4/ps-button.log 2>&1 &
touch /run/androidtv-ps4/bootstrap-active
/usr/sbin/android-supervisor -- /bin/sh /run/androidtv-ps4/android-child.sh </dev/null >/run/androidtv-ps4/android.log 2>&1 &
echo $! > /run/androidtv-ps4/android-supervisor.pid
for attempt in $(seq 1 50); do
 [ ! -s /run/androidtv-ps4/android-init.pid ] || break
 sleep 0.1
done
child=$(cat /run/androidtv-ps4/android-init.pid 2>/dev/null || true)
if [ -z "$child" ]; then
    echo 'Android init exited; no network interface will be moved.'
    exit 1
fi
# Android owns its DHCP configuration; the host has no management address.
# A console without a recognised Ethernet port still boots, just without it.
wired=eth0
[ -e /sys/class/net/eth0/address ] || wired=psx0
if [ -e "/sys/class/net/$wired/address" ]; then
 /usr/sbin/ip link set "$wired" up
 hwmac=$(cat "/sys/class/net/$wired/address")
 /usr/sbin/ip link add link "$wired" name atv0 address "02${hwmac#??}" type macvlan mode bridge
 /usr/sbin/ip link set atv0 netns "$child"
 nsenter -t "$child" -n /usr/sbin/ip link set atv0 name eth0
 nsenter -t "$child" -n /usr/sbin/ip link set eth0 up
else
 stage 'No Ethernet interface found; continuing without Ethernet'
fi
# The MT7668 creates its interface after loading firmware; give it time.
if [ -n "$mt7668" ]; then
 for attempt in $(seq 1 100); do
  /usr/sbin/iw dev 2>/dev/null | grep -q Interface && break
  sleep 0.1
 done
fi
# Move the console's Wi-Fi radio into Android: the one that owns the Wi-Fi
# interface, which isn't always phy0 (the MediaTek MT7668 driver registers a
# second radio for Wi-Fi Direct). A failure is logged and Android still starts.
radio=$(/usr/sbin/iw dev 2>/dev/null | awk '$1 == "Interface" && $2 !~ /^p2p/ {print $2; exit}')
if [ -n "$radio" ]; then
    wiphy=$(/usr/sbin/iw dev "$radio" info 2>/dev/null | awk '$1 == "wiphy" {print $2}')
    /usr/sbin/ip link set "$radio" down || true
    if [ -n "$wiphy" ] && /usr/sbin/iw "phy#$wiphy" set netns "$child" 2>>/run/androidtv-ps4/progress.log; then
        if [ "$radio" != wlan0 ]; then
            nsenter -t "$child" -n /usr/sbin/ip link set "$radio" name wlan0 || true
        fi
        nsenter -t "$child" -n /usr/sbin/ip link set wlan0 up || true
        stage "Wi-Fi ($radio on radio $wiphy) handed to Android"
    else
        stage "Wi-Fi ($radio on radio ${wiphy:-unknown}) could not be handed to Android; Wi-Fi won't work this start"
    fi
else
    stage 'No Wi-Fi interface found; Wi-Fi unavailable this start'
fi
echo "Android network attached to namespace owned by PID $child"

stage 'Android init launched; diagnostics continue in android-stages.log'
# These logs are created on this console, never included in the release package.
# Android init owns the rotating collector in /data/misc/logd; it survives setup.
# Dev build: about 10 minutes, since Android 16's first boot compiles a lot.
for attempt in $(seq 1 300); do
 if [ -f /run/androidtv-ps4/power-request ]; then exit 0; fi
 now=$(cut -d' ' -f1 /proc/uptime)
 props=$(timeout 3 nsenter -t "$child" -m -p -n /system/bin/getprop 2>/dev/null || true)
 property() { printf '%s\n' "$props" | sed -n "s/^\[$1\]: \[\(.*\)\]$/\1/p"; }
 if [ "$(property init.svc.bootanim)" = running ] && [ ! -f /run/androidtv-ps4/animation-captured ]; then
  touch /run/androidtv-ps4/animation-captured
  ( timeout 5 nsenter -t "$child" -m -p -n /system/bin/dumpsys SurfaceFlinger > /run/androidtv-ps4/animation-display.log 2>&1 || true ) &
 fi
 if [ ! -f /run/androidtv-ps4/display-ready ]; then
  stage "Android startup: graphics=$(property init.svc.surfaceflinger) animation=$(property init.svc.bootanim) apps=$(property init.svc.zygote); waiting for a display frame"
 fi
 { echo "[$now seconds]"; printf '%s\n' "$props" | grep -E '^\[(sys.boot_completed|init.svc.(zygote|zygote_secondary|surfaceflinger|bootanim|adbd)|service.bootanim.exit|sys.user.0.ce_available)\]' || true; } >> /run/androidtv-ps4/android-stages.log
 if printf '%s\n' "$props" | grep -q '^\[sys.boot_completed\]: \[1\]'; then
  echo "[$now seconds] Android boot complete" >> /run/androidtv-ps4/android-stages.log
  echo "$child" > /run/androidtv-ps4/android.pid
  exit 0
 fi
 sleep 2
done
stage 'Android startup has not completed. Logs are saved; restarting returns to PS4.'
echo 'Android boot timeout; check Android /data/misc/logd and android-stages.log' >> /run/androidtv-ps4/android-stages.log
exit 1
