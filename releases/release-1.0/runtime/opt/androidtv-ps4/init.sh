#
# Copyright (C) 2013-2018 The Android-x86 Open Source Project
#
# License: GNU Public License v2 or later
#

function set_property()
{
	setprop "$1" "$2"
	[ -n "$DEBUG" ] && echo "$1"="$2" >> /dev/x86.prop
}

function set_prop_if_empty()
{
	[ -z "$(getprop $1)" ] && set_property "$1" "$2"
}

function rmmod_if_exist()
{
	for m in $*; do
		[ -d /sys/module/$m ] && rmmod $m
	done
}

function init_misc()
{
	# a hack for USB modem
	lsusb | grep 1a8d:1000 && eject

	# in case no cpu governor driver autoloads
	[ -d /sys/devices/system/cpu/cpu0/cpufreq ] || modprobe acpi-cpufreq

	# enable sdcardfs/esdfs if /data is not mounted on tmpfs or 9p
	mount | grep /data\ | grep -qE 'tmpfs|9p'
	[ $? -eq 0 ] && SDCARDFS_DISABLE=${SDCARDFS_DISABLE:-1}

	# PS4: no sdcardfs/esdfs in this kernel.
	SDCARDFS_DISABLE=1

	# Allow force disable sdcardfs/esdfs
	if [ "$SDCARDFS_DISABLE" -ge 1 ]; then
		set_property external_storage.sdcardfs.enabled false
	fi

	# esdfs and FUSE passthrough are incompatible: passthrough opens the real
	# backing file through the esdfs stack and MediaStore writes fail (ELOOP).
	set_property persist.sys.fuse.passthrough.enable false

	# remove wl if it's not used
	local wifi
	if [ -d /sys/class/net/wlan0 ]; then
		wifi=$(basename `readlink /sys/class/net/wlan0/device/driver`)
		[ "$wifi" != "wl" ] && rmmod_if_exist wl
	fi

	# disable virt_wifi by default, only turn on when user set VIRT_WIFI=1
	local wifi_name=${VIRT_WIFI_NAME:-wifi_eth}
	local eth=`getprop net.virt_wifi eth0`
	if [ -d /sys/class/net/$eth -a "$VIRT_WIFI" -gt "0" ]; then
		if [ -n "$wifi" -a "$VIRT_WIFI" -ge "1" ]; then
			rmmod_if_exist iwlmvm $wifi
		fi
		if [ ! -d /sys/class/net/wlan0 ]; then
			ifconfig $eth down
			ip link set $eth name $wifi_name
			ifconfig $wifi_name up
			ip link add link $wifi_name name wlan0 type virt_wifi
		fi
	fi

	#Set CPU name into a property
	setprop ro.bliss.cpuname "$(grep "model name" /proc/cpuinfo | sort -u | cut -d : -f 2 | cut -c2-)"

	# Tell vold to use ntfs3 driver instead of ntfs-3g
    if [ "$USE_NTFS3" -ge "1" ] || [ "$VOLD_USE_NTFS3" -ge 1 ]; then
        set_property ro.vold.use_ntfs3 true
    fi

    if [ "$DEBUG_VSOCK" -ge "1" ]; then
        set_property service.adb.listen_addrs "vsock:5555"
    fi
}

function init_hal_audio()
{
	# PS4: HDMI is the only output, on device 3 of the first card.
	set_property hal.audio.out pcmC0D3p
	set_property hal.audio.out.hdmi pcmC0D3p
	set_property hal.audio.primary.hdmi 1

	case "$PRODUCT" in
		VirtualBox*|Bochs*)
			[ -d /proc/asound/card0 ] || modprobe snd-sb16 isapnp=0 irq=5
			;;
		TS10*)
			set_prop_if_empty hal.audio.out pcmC0D2p
			;;
	esac

	case "$(ls /proc/asound)" in
		*sofhdadsp*)
			AUDIO_PRIMARY=x86_celadon
			;;
	esac
	set_property ro.hardware.audio.primary ${AUDIO_PRIMARY:-x86}

	if [ "$BOARD" == "Jupiter" ] && [ "$VENDOR" == "Valve" ]
	then
		pcm_card=$(cat /proc/asound/cards | grep acp5x | awk '{print $1}')
		# headset microphone on d0, 32bit only
		set_property hal.audio.in.headset "pcmC${pcm_card}D0c"
		set_property hal.audio.in.headset.format 1

		# internal microphone on d0, 32bit only
		set_property hal.audio.in.mic "pcmC${pcm_card}D0c"
		set_property hal.audio.in.mic.format 1

		# headphone jack on d0, 32bit only
		set_property hal.audio.out.headphone "pcmC${pcm_card}D0p"
		set_property hal.audio.out.headphone.format 1

		# speaker on d1, 16bit only
		set_property hal.audio.out.speaker "pcmC${pcm_card}D1p"
		set_property hal.audio.out.speaker.format 0

		# enable hdmi audio on the 3rd output, but it really depends on how docks wire things
		# to make matters worse, jack detection on alsa does not seem to always work on my setup, so a dedicated hdmi hal might want to send data to all ports instead of just probing
		pcm_card=$(cat /proc/asound/cards | grep HDA-Intel | awk '{print $1}')
		set_property hal.audio.out.hdmi "pcmC${pcm_card}D8p"
	fi
}

function init_hal_brcm_wifi()
{
##	Function created by Chaozhuo's PhoenixOS
##	Currently these devices are supported to use broadcom-wl driver
#	Broadcom BCM4313 (PCI ID 14e4:4727)
#	Broadcom BCM4321 (PCI IDs 14e4:4328, 14e4:4329, 14e4:432a)
#	Broadcom BCM4322 (PCI IDs 14e4:432b, 14e4:432d)
#	Broadcom BCM43142 (PCI ID 14e4:4365)
#	Broadcom BCM4352 (PCI ID 14e4:43b1)
#	Broadcom BCM4360 (PCI IDs 14e4:43a0, 14e4:4360)
## 	These devices are also on the list, but removed due to the fact that 
##	b43 or brcm80211 supports it.
##	https://wireless.wiki.kernel.org/en/users/drivers/b43
#	Broadcom BCM4311 (PCI IDs 14e4:4311, 14e4:4312)
#	Broadcom BCM4312 (PCI ID 14e4:4315)
#	Broadcom BCM4322 (PCI IDs 14e4:432c)
#	Broadcom BCM43224 (PCI IDs 14e4:0576, 14e4:4353)
#	Broadcom BCM43225 (PCI ID 14e4:4357)
#	Broadcom BCM43227 (PCI ID 14e4:4358)
#	Broadcom BCM43228 (PCI ID 14e4:4359)
#	Broadcom BCM4331 (PCI ID 14e4:4331)

	BCMAID=`lspci | grep "14e4" | awk '{print $4}'`

	case "${BCMAID##*:}" in
			4727 | \
			4328 | 4329 | 432a | \
			432b | 432d | \
			4365 | \
			43b1 | \
			43a0 | 4360 )

			rmmod b43
			rmmod b44
			rmmod b43legacy
			rmmod ssb
			rmmod bcma
			rmmod brcm80211
			rmmod brcmsmac
			rmmod wl

			modprobe wl
			;;
		*)
			;;
	esac
}

# TEMPORARY WORKAROUND (los-audio-rescue) — remove once the kernel-side
# root cause is understood and fixed.
#
# On some Elkhart Lake boards (HP Pro t550, PCI 8086:4b58) the HDA codec
# probe intermittently fails hard: the controller binds (snd_hda_intel),
# but the codec probe dies with "azx_get_response timeout" / "rates == 0"
# ending in "Cannot probe codecs, giving up". No ALSA card is created, so
# the whole audio userspace is dead ("Unable to find the mixer") for the
# entire boot. The trigger is boot/environment-dependent and not yet
# isolated; this userspace fallback retries the probe at bootcomplete by
# unloading/reloading the controller module, escalating module parameters
# that change the codec access mode and bus mask:
#   1. fresh reload (no parameters)
#   2. probe_mask=1            - probe only bus 0 (codec hangs on slot 0)
#   3. probe_mask=1 single_cmd - bypass the CORB/RIRB DMA rings entirely
# Codec driver modules (snd_hda_codec*) are deliberately NOT touched:
# when the controller rebinds, hda_core re-enumerates codec devices and
# the already loaded codec drivers rebind automatically.
# Safety: no-op unless ALL of these hold -
#   a) persist.sys.los.audio.rescue is exactly "1" (opt IN; disabled by default),
#      enable from CLI: adb shell setprop persist.sys.los.audio.rescue 1 (+ reboot),
#   b) a PCI audio device is present (no rescue for USB-only machines),
#   c) /proc/asound/cards is empty (any card present = healthy boot),
#   d) dmesg contains the exact kernel failure signature above.
# Healthy machines spend a few milliseconds on gates and return.
function _audio_rescue_log()
{
	echo "los-audio-rescue: $1" > /dev/kmsg 2>/dev/null
	log -t los-audio-rescue "$1" 2>/dev/null
}

function _audio_rescue_try()
{
	rmmod snd_hda_intel 2>/dev/null
	sleep 1
	modprobe snd_hda_intel $* 2>/dev/null
	sleep 6
	[ -d /proc/asound/card0 ]
}

function init_hal_audio_rescue()
{
	[ "$(getprop persist.sys.los.audio.rescue)" = "1" ] || return 0

	lspci 2>/dev/null | grep -qi audio || return 0
	[ -n "$(cat /proc/asound/cards 2>/dev/null)" ] && return 0
	dmesg 2>/dev/null | grep -q "Cannot probe codecs" || return 0

	_audio_rescue_log "no cards + 'Cannot probe codecs' in dmesg, retrying HDA probe"

	local params
	for params in "" "probe_mask=1" "probe_mask=1 single_cmd=1"; do
		if _audio_rescue_try $params; then
			_audio_rescue_log "recovered with params='$params'"
			break
		fi
		_audio_rescue_log "attempt with params='$params' failed"
	done
	[ -d /proc/asound/card0 ] || {
		_audio_rescue_log "all attempts failed, giving up"
		return 0
	}

	# Unmute defaults for the freshly created card(s) (subset of the
	# defaults in init_hal_audio_bootcomplete(); the alsa state restore
	# path below uses /data/vendor/asound.state, which predates this card)
	local c i
	for c in $(grep '\[.*\]' /proc/asound/cards | awk '{print $1}'); do
		alsa_amixer -c $c set Master on
		alsa_amixer -c $c set Master 100%
		alsa_amixer -c $c set Headphone on
		alsa_amixer -c $c set Headphone 100%
		alsa_amixer -c $c set Speaker on
		alsa_amixer -c $c set Speaker 100%
		alsa_amixer -c $c set PCM 100% unmute
		for i in 1 2 3 4 5 6 7; do
			alsa_amixer -c $c cset "name='IEC958 Playback Switch',index=$i" on 2>/dev/null
		done
	done

	# The HAL started while the card was still missing (its cached card
	# slots stayed empty, so its next open_output_stream re-scans, but the
	# AudioPolicyManager of the running audioserver gave up on the module):
	# bounce audioserver so the new card is picked up by policy/hal.
	stop audioserver
	start audioserver
}

function init_vendor_fw_overlay()
{
# Firmware tool overlay: /data/vendor_fw mirrors the /vendor/firmware
# layout. Every file found there is bind-mounted over the matching
# /vendor/firmware file, so a user-selected firmware survives reboots
# and full-image OTA (which rewrite system/vendor but not /data).
# Removing the file from /data/vendor_fw undoes the swap on next boot.
# Runs as init (root), no su needed at boot time.
	local ff dir
	[ -d /data/vendor_fw ] || return 0
	find /data/vendor_fw -type f 2>/dev/null | while IFS= read -r ff; do
		dir=${ff#/data/vendor_fw}
		dir=${dir#/}
		[ -n "$dir" ] || continue
		target=/vendor/firmware/$dir
		[ -f "$target" ] || continue
		grep -q " $target " /proc/mounts && continue
		mount -o bind "$ff" "$target"
		log -t los-fwoverlay "bind $dir: $?"
	done
	return 0
}

function init_hal_audio_bootcomplete()
{
	if [ -e "/data/vendor/asound.state" ]; then
		# Initialize once in case a new audio device is plugged in
		alsa_ctl init
		# Restore /data/vendor/asound.state if found
		alsa_ctl restore
		# Restored states predate the per-pin HDMI unmute: unmute all
		# HDMI pins unconditionally (same as in the defaults below).
		for c in $(grep '\[.*\]' /proc/asound/cards | awk '{print $1}'); do
			for i in 1 2 3 4 5 6 7; do
				alsa_amixer -c $c cset "name='IEC958 Playback Switch',index=$i" on 2>/dev/null
			done
		done
	else
		# Initialize as usual
		# 1. Initialize the all the audio cards
		alsa_ctl init

		# 2. Restore alsa state for specific cards or turn on all of the 
		# default controls & set value to max (usually for PCH)
		for c in $(grep '\[.*\]' /proc/asound/cards | awk '{print $1}'); do
			f=/system/etc/alsa/$(cat /proc/asound/card$c/id).state
			d=/data/vendor/alsa/$(cat /proc/asound/card$c/id).state
			if [ -e $f ]; then
				alsa_ctl -f $f restore $c
			elif [ -e $d ]; then
				alsa_ctl -f $d restore $c
			else
				alsa_amixer -c $c set Master on
				alsa_amixer -c $c set Master 100%
				alsa_amixer -c $c set Headphone on
				alsa_amixer -c $c set Headphone 100%
				alsa_amixer -c $c set Speaker on
				alsa_amixer -c $c set Speaker 100%
				alsa_amixer -c $c set Capture 80%
				alsa_amixer -c $c set Capture cap
				alsa_amixer -c $c set PCM 100% unmute
				alsa_amixer -c $c set SPO unmute
				alsa_amixer -c $c set IEC958 on
				# HDMI pins live on per-index IEC958 switches (index N =
				# device 3+N on HDA); the TV is rarely pin 0, unmute all.
				for i in 1 2 3 4 5 6 7; do
					alsa_amixer -c $c cset "name='IEC958 Playback Switch',index=$i" on 2>/dev/null
				done
				alsa_amixer -c $c set 'Mic Boost' 1
				alsa_amixer -c $c set 'Internal Mic Boost' 1

				# 2.5. Set alsaucm HiFi verb & devices for specific hardware
				for d in $(grep '\[.*\]' /proc/asound/cards | awk '{print $4}'); do
					# Check if the card is rt5651, this card can not be able to 
					# use both Speaker & Headphones at the same time. So we will enable  
					# only Speaker for this card and tell users to turn Headphones on
					# manually if they want to use it.
					if [ "$(cat /proc/asound/cards | grep rt5651)" ]; then 
						card_is_rt5651=true
					fi

					case $d in
					*bytcht*|*bytcr*|*cht-bsw*|*chtmax*|*chtrt*|*chtnau*)
						if [ "$card_is_rt5651" == true ]; then
							alsaucm -c $d set _verb HiFi \
							set _enadev Speaker \
							set _enadev Headset \
							set _enadev Mic
						else 
							alsaucm -c $d set _verb HiFi \
							set _enadev Speaker \
							set _enadev Headphones \
							set _enadev Headset \
							set _enadev Mic
						fi
						;;
					*SOF*)
						if [ "$(cat /proc/asound/cards | grep -E 'bytcht|bdw')" ]; then
							if [ "$card_is_rt5651" == true ]; then
								alsaucm -c $d set _verb HiFi \
								set _enadev Speaker \
								set _enadev Headset \
								set _enadev Mic
							else
								alsaucm -c $d set _verb HiFi \
								set _enadev Speaker \
								set _enadev Headphones \
								set _enadev Headset \
								set _enadev Mic
							fi
						fi
						;;
					*acp5x*)
						alsaucm -c $d set _verb HiFi \
						set _enadev Speaker \
						set _enadev Headphones \
						set _enadev Headset \
						set _enadev Mic
						;;
					esac
				done
			fi
		done

		# 3. Store everything into /data/vendor/asound.state
		alsa_ctl store
	fi
}

function init_hal_bluetooth()
{
	for r in /sys/class/rfkill/*; do
		type=$(cat $r/type)
		[ "$type" = "wlan" -o "$type" = "bluetooth" ] && echo 1 > $r/state
	done

	case "$PRODUCT" in
		T100TAF)
			set_property bluetooth.interface hci1
			;;
		T10*TA|M80TA|HP*Omni*)
			BTUART_PORT=/dev/ttyS1
			set_property hal.bluetooth.uart.proto bcm
			;;
		MacBookPro8*)
			rmmod b43
			modprobe b43 btcoex=0
			modprobe btusb
			;;
		# FIXME
		# Fix MacBook 2013-2015 (Air6/7&Pro11/12) BCM4360 ssb&wl conflict.
		MacBookPro11* | MacBookPro12* | MacBookAir6* | MacBookAir7*)
			rmmod b43
			rmmod ssb
			rmmod bcma
			rmmod wl
			modprobe wl
			modprobe btusb
			;;
		*)
			for bt in $(toybox lsusb -v | awk ' /Class:.E0/ { print $9 } '); do
				chown 1002.1002 $bt && chmod 660 $bt
			done
			;;
	esac

	if [ -n "$BTUART_PORT" ]; then
		set_property hal.bluetooth.uart $BTUART_PORT
		chown bluetooth.bluetooth $BTUART_PORT
		start btattach
	fi

	if [ "$BT_BLE_DISABLE" = "1" ]; then
		set_property bluetooth.core.le.disabled true
		set_property bluetooth.hci.disabled_commands 246
	fi

	if [ "$BT_BLE_NO_VENDORCAPS" = "1" ]; then
		set_property bluetooth.core.le.vendor_capabilities.enabled false
		set_property persist.sys.bt.max_vendor_cap 0
	fi
}

function init_hal_camera()
{
	if [ "$EMULATED_CAMERA" != "1" ]; then
		stop vendor.camera-provider-2-7-google
	fi
}

function init_hal_gps()
{
	# TODO
	return
}

function set_drm_mode()
{
	case "$PRODUCT" in
		ET1602*)
			drm_mode=1366x768
			;;
		*)
			[ -n "$video" ] && drm_mode=$video
			;;
	esac

	[ -n "$drm_mode" ] && set_property debug.drm.mode.force $drm_mode
}

function init_hal_gralloc()
{
	# Only search for a card if GPU_OVERRIDE hasn't already been set
	# 
	# There is a tested case that when init_hal_gralloc() start, simpledrm still haven't 
	# properly uninitialize yet, making /dev/dri/card0 still be able to exist and
	# we accidentally pick it. Search to see if the remains of simpledrm is still here 
	# and put in another for loop and run until we get the correct result.
	if [ -z "$GPU_OVERRIDE" ]; then
		# Loop with a timeout (e.g., 20 attempts, 0.5s each = 10 seconds max)
		for attempt in $(seq 1 20); do
			for i in $(seq 0 9); do
				if [ -c "/dev/dri/card$i" ]; then
					local driver_name=""
					
					if [ -L "/sys/class/drm/card$i/device/driver" ]; then
						driver_name=$(basename $(readlink /sys/class/drm/card$i/device/driver))
					elif [ -f "/sys/class/drm/card$i/device/uevent" ]; then
						driver_name=$(grep DRIVER= /sys/class/drm/card$i/device/uevent | cut -d= -f2)
					fi

					# Ignore simple framebuffers or unpopulated sysfs entries
					if [[ "$driver_name" == *simple* ]] || [ -z "$driver_name" ]; then
						if [ "$HWACCEL" != "0" ]; then
							continue
						fi
					fi

					GPU_OVERRIDE="card$i"
					break 2
				fi
			done
			sleep 0.5
		done
	fi

	# Fallback just in case /dev/dri is completely empty or inaccessible
	GPU_OVERRIDE=${GPU_OVERRIDE:-card0}

	# Set default GPU render
	set_property gralloc.gbm.device /dev/dri/$GPU_OVERRIDE
	set_property vendor.hwc.drm.device /dev/dri/$GPU_OVERRIDE
	# PS4: allocate through the render node, as on Android TV for PS4 13.
	set_property gralloc.gbm.device /dev/dri/renderD128

	# The "Graphics & decoding" settings screen chooses the gralloc backend via
	# persist.sys.los.hardware.gralloc, applied natively by init.los_codec.rc
	# before this script runs. The auto-detected defaults below use
	# set_prop_if_empty so they never override a user choice.

	case "$GPU" in
		*virtio_gpu|*virtio-pci)
			HWC=${HWC:-drm_minigbm}
			GRALLOC=${GRALLOC:-minigbm_arcvm}
			#video=${video:-1280x768}
			set_property ro.vendor.hwc.drm.present_fence_not_reliable true
			# drm hwc3 (composer3, required by SurfaceFlinger) only works
			# with the GBM gralloc backend. init.los_codec.rc is not
			# imported on this device, so its on-property trigger never
			# fires: start the allocator directly instead of relying on it.
			# Default virtio to it unless the user picked another backend
			# in Settings.
			if [ -z "$(getprop persist.sys.los.hardware.gralloc)" ]; then
				set_property persist.sys.los.hardware.gralloc minigbm_gbm_mesa
				set_property debug.ui.default_mapper 4
				set_property debug.ui.default_gralloc 4
				set_property ro.hardware.gralloc minigbm_gbm_mesa
				start vendor.graphics.allocator-4-0-gbm_mesa
			fi
			;&
		*nouveau)
			GRALLOC=${GRALLOC:-minigbm_gbm_mesa}
			HWC=${HWC:-drm_minigbm}
			;&
		*i915)
			I915_GPU_GEN=$(cat /sys/kernel/debug/dri/${GPU_OVERRIDE#card}/i915_capabilities 2>/dev/null |
				grep -e 'gen' -e 'graphics version' |
				awk '{print $NF}')

			if [ "$I915_GPU_GEN" -lt 9 ]; then
				set_property vendor.hwc.drm.avoid_using_alpha_bits_for_framebuffer 1
				set_property vendor.hwc.drm.disable_planes 1
			fi
			;&
		*amdgpu|*vmwgfx*|*xe)
			GRALLOC=${GRALLOC:-minigbm}
			HWC=${HWC:-drm_minigbm}
			;&
		*radeon)
			# Remove when Virtualbox got svga working well
			if { [[ "$BOARD" != *VirtualBox* ]] &&
				 [ "$HWACCEL" != "0" ]; } ||
				 [ "$HWACCEL" == "1" ]; then
				true
			else
				export HWACCEL=0
			fi
			;;
		*)
			export HWACCEL=0
			;;
	esac

	# NOTE: init.los_codec.rc is not imported on this device, so its
	# on-property triggers never fire: allocator services are started
	# directly here instead.
	gralloc_choice=$(getprop persist.sys.los.hardware.gralloc)
	# Breadcrumb for the minigbm_intel mystery: log the effective
	# choice every boot so dmesg always shows what was selected.
	echo "los-gralloc: choice='$gralloc_choice' GPU='$GPU'" > /dev/kmsg 2>/dev/null || true
	# Live sessions move between machines (Intel box today, Ryzen
	# tomorrow): a persisted GPU backend from another machine is
	# poison here. Always auto-detect the present hardware on live.
	if grep -q "androidboot.live=true" /proc/cmdline 2>/dev/null; then
		gralloc_choice=""
	fi
	case "$gralloc_choice" in
		minigbm_arcvm)
			set_property debug.ui.default_mapper 4
			set_property debug.ui.default_gralloc 4
			set_property ro.hardware.gralloc minigbm_arcvm
			start vendor.graphics.allocator-4-0-arcvm
			;;
		minigbm_intel)
			# Intel (i915) backend: Y-tiled gralloc4, matches LOS21
			# minigbm_intel; the only one where c2.intel decodes HW.
			set_property debug.ui.default_mapper 4
			set_property debug.ui.default_gralloc 4
			set_property ro.hardware.gralloc minigbm_intel
			start vendor.graphics.allocator-4-0-intel
			;;
		minigbm_nouveau)
			set_property debug.ui.default_mapper 4
			set_property debug.ui.default_gralloc 4
			set_property ro.hardware.gralloc minigbm_nouveau
			start vendor.graphics.allocator-4-0-nouveau
			;;
		minigbm|gbm)
			# Explicit legacy choice: HIDL gralloc 2.0. Note that
			# composer3 (required by SurfaceFlinger) will not work with it.
			set_property debug.ui.default_mapper 2
			set_property debug.ui.default_gralloc 2
			start vendor.gralloc-2-0
			;;
		*)
			# Default (empty/auto, incl. minigbm_gbm_mesa): GBM backend,
			# the only one compatible with drm hwc3 (composer3).
			set_property debug.ui.default_mapper 4
			set_property debug.ui.default_gralloc 4
			set_property ro.hardware.gralloc minigbm_gbm_mesa
			start vendor.graphics.allocator-4-0-gbm_mesa
			;;
	esac

	[ -n "$DEBUG" ] && set_property debug.egl.trace error
}

function init_egl()
{

	# Remove when Virtualbox got svga working well
    if [[ "$BOARD" == *VirtualBox* ]]; then
        export HWACCEL=0
    fi

	if [ "$HWACCEL" == "0" ] && [ "$MESA_LLVMPIPE" != "1" ]; then
		export EGL=${EGL:-angle}
	fi

	set_property ro.hardware.egl ${EGL:-mesa}

	if [ "$MESA_LLVMPIPE" -ge "1" ]; then
		set_property mesa.libgl.always.software true
	fi

	if [ "$MESA_ZINK" -ge "1" ]; then
		set_property mesa.loader.driver.override zink
	fi

	# Set OpenGLES version
	case "$FORCE_GLES" in
		*2.0*)
    	    set_property ro.opengles.version 131072
            set_property mesa.gles.version.override 2.0
		;;
        *3.0*)
    	    set_property ro.opengles.version 196608
            set_property mesa.gles.version.override 3.0
		;;
		*3.1*)
    		set_property ro.opengles.version 196609
            set_property mesa.gles.version.override 3.1
		;;
		*3.2*)
    		set_property ro.opengles.version 196610
            set_property mesa.gles.version.override 3.2
		;;
		*)
    		set_property ro.opengles.version 196608
		;;
	esac

	# Set RenderEngine backend
	if [ -z ${FORCE_RENDERENGINE+x} ]; then
		set_property debug.renderengine.backend skiaglthreaded
	else
		set_property debug.renderengine.backend $FORCE_RENDERENGINE
	fi
}

function init_hal_hwcomposer()
{
	if [ "$HWACCEL" == "0" ] || 
	[[ "$GPU" == *radeon* ]]; then
		export HWC_HIDL=${HWC_HIDL:-default-2.1}
	fi

	# Ensure hw_get_module() finds the right HWC1 .so on live boot
	# (init.los_codec.rc sets this via persist props which need /data)
	if [ -z "$(getprop ro.hardware.hwcomposer)" ]; then
		set_property ro.hardware.hwcomposer ${HWC:-drm_minigbm}
	fi

	case "$HWC_HIDL" in
		drmfb)
			set_property debug.sf.hwc_service_name drmfb
			start vendor.hwcomposer-2-1.drmfb
			;;
		default-2.1)
			set_property debug.sf.hwc_service_name default_clone
			start vendor.hwcomposer-2-1-clone
			;;
		default-2.4|*)
			set_property debug.sf.hwc_service_name default
			start vendor.hwcomposer-2-4
			;;
	esac

	if [[ "$HWC" == "drm_celadon" || "$HWC" == "drm_minigbm_celadon" ]]; then
		set_property vendor.hwcomposer.planes.enabling $MULTI_PLANE
		set_property vendor.hwcomposer.planes.num $MULTI_PLANE_NUM
		set_property vendor.hwcomposer.preferred.mode.limit $HWC_PREFER_MODE
		set_property vendor.hwcomposer.connector.id $CONNECTOR_ID
		set_property vendor.hwcomposer.mode.id $MODE_ID
		set_property vendor.hwcomposer.connector.multi_refresh_rate $MULTI_REFRESH_RATE
	fi
}

function init_hal_media()
{
	# PS4: software FFmpeg codecs first, RGBX output (as on 13).
	FFMPEG_HWACCEL_DISABLE=${FFMPEG_HWACCEL_DISABLE:-0}
	set_property debug.ffmpeg-codec2.rank 0
	set_property persist.ffmpeg.codec2.prefer 1
	set_property system.ffmpeg.codec2.prefer 1
	set_property vendor.ffmpeg.codec2.prefer 1
	set_property debug.stagefright.ccodec 4
	set_property persist.ffmpeg-codec2.pixel_format RGBX_8888


## User choices made on the "Graphics & decoding" settings screen are applied
## natively by init.los_codec.rc (property triggers on persist.sys.los.*) before
## this script runs. Only the kernel-cmdline defaults are handled here, using
## set_prop_if_empty so a user choice is never overridden.

## Enable logging
    if [ "$FFMPEG_CODEC_LOG" -ge "1" ]; then
        set_property debug.ffmpeg.loglevel verbose
    fi	
## Disable HWAccel (currently only VA-API) and use software rendering.
	if [ "$FFMPEG_HWACCEL_DISABLE" -ge "1" ]; then
		set_prop_if_empty media.sf.hwaccel 0
	else
		set_prop_if_empty media.sf.hwaccel 1
	fi
## FFMPEG deinterlace, we will put both software mode and VA-API one here
	if [ -z "${FFMPEG_CODEC2_DEINTERLACE+x}" ]; then
		echo ""
	else
		set_property debug.ffmpeg-codec2.deinterlace $FFMPEG_CODEC2_DEINTERLACE
	fi
	if [ -z "${FFMPEG_CODEC2_DEINTERLACE_VAAPI+x}" ]; then
		echo ""
	else
		set_property debug.ffmpeg-codec2.deinterlace.vaapi $FFMPEG_CODEC2_DEINTERLACE_VAAPI
	fi
## Handle which GPU driver will use which pixel format
## c2.ffmpeg can be able to switch now
	case "$GPU" in
		*virtio_gpu|*virtio-pci|*nouveau|*radeon)
			set_prop_if_empty persist.ffmpeg-codec2.pixel_format RGBX_8888
			;;
		*i915|*xe|*amdgpu)
			# Only a literal "minigbm" allocator can hand out YUV planes.
			# The Mesa based allocator (minigbm_gbm_mesa) goes through
			# gbm, which does not support YUV, so it must use RGBX and
			# let the component convert.
			if [ "$(getprop ro.hardware.gralloc)" != "minigbm" ]; then
				set_prop_if_empty persist.ffmpeg-codec2.pixel_format RGBX_8888
			else
				set_prop_if_empty persist.ffmpeg-codec2.pixel_format YUV_420
			fi
			;;
		*)
			set_prop_if_empty persist.ffmpeg-codec2.pixel_format RGB_565
			;;
	esac

## Handle DRM prime on ffmpeg codecs. This gives c2.ffmpeg a true zero-copy
## path: VA surfaces are created straight from the gralloc dma-buf, so decoded
## frames reach the consumer without a CPU readback (and, on Intel, without
## Y-tiled detiling). It needs a YUV gralloc buffer with exported dma-buf
## planes, which every minigbm allocator provides.
##
## Keep this disabled by default: the zero-copy path is not verified yet, and
## when it breaks it takes the whole c2-ffmpeg service down with it. Enable
## explicitly with FFMPEG_CODEC2_DRM=1 once the surface handling is proven.
	if [ -z "${FFMPEG_CODEC2_DRM+x}" ]; then
	    set_prop_if_empty debug.ffmpeg-codec2.hwaccel.drm 0
	elif [ "$FFMPEG_CODEC2_DRM" -ge "1" ]; then
	    set_prop_if_empty debug.ffmpeg-codec2.hwaccel.drm 1
	else
	    set_prop_if_empty debug.ffmpeg-codec2.hwaccel.drm 0
	fi

## Codec2 engine selection ("Codec2 backend" on the settings screen).
## Intel MediaSDK-C2 was never part of stock and is fully removed from this
## product, so the only engine left is FFmpeg. The per-GPU switch and the
## c2.intel.* bind mount are gone; FFmpeg is used unconditionally.
	mount --bind /vendor/etc/media_codecs/codecs_ffmpeg.xml /vendor/etc/media_codecs.xml 2>/dev/null

}

function init_hal_vulkan()
{
	if [ "$HWACCEL" != "0" ]; then
		case "$GPU" in
			*i915)
				if [ "$I915_GPU_GEN" -lt 9 ]; then
					VULKAN=${VULKAN:-intel_hasvk}
				else
					VULKAN=${VULKAN:-intel}
				fi
				;&
			*xe)
				VULKAN=${VULKAN:-intel}
				;&
			*amdgpu)
				VULKAN=${VULKAN:-radeon}
				;&
			*virtio_gpu|*virtio-pci)
				VULKAN=${VULKAN:-virtio}
				;&
			*nouveau)
				VULKAN=${VULKAN:-nouveau}
				;&
			*)
				set_property ro.hardware.vulkan ${VULKAN:-pastel}
				;;
		esac
	else
		set_property ro.hardware.vulkan ${VULKAN:-pastel}
	fi
}

function init_hal_lights()
{
	chown 1000.1000 /sys/class/backlight/*/brightness
}

function init_hal_power()
{
	for p in /sys/class/rtc/*; do
		echo disabled > $p/device/power/wakeup
	done

	# TODO
	case "$PRODUCT" in
		HP*Omni*|OEMB|Standard*PC*|Surface*3|T10*TA|VMware*)
			export SLEEP_STATE=${SLEEP_STATE:-none}
			;;
		e-tab*Pro)
			export SLEEP_STATE=${SLEEP_STATE:-force}
			;;
		*)
			;;
	esac

	set_property sleep.state ${SLEEP_STATE}
}

function init_hal_thermal()
{
    # Check if thermald needs to be disabled
    # 1. VMs (QEMU, VMware, Oracle VirtualBox)
    # 2. AMD CPUs
    case "$VENDOR" in
        *QEMU*|*VMware*)
            export THERMALD_DISABLE=${THERMALD_DISABLE:-1}
            ;;
        *)
            ;;
    esac

    if [[ "$BOARD" == *VirtualBox* ]]; then
        export THERMALD_DISABLE=${THERMALD_DISABLE:-1}
    fi

    if grep -q "AuthenticAMD" /proc/cpuinfo; then
        export THERMALD_DISABLE=${THERMALD_DISABLE:-1}
    fi

    if [ "$THERMALD_DISABLE" -lt 1 ]; then
        start thermal-daemon
    fi
}

function init_hal_sensors()
{
	# Pause for amount of seconds before initializing sensors
	if [ -n "$SENSORS_DELAY_INIT" ]; then
		sleep "$SENSORS_DELAY_INIT"
	fi

    if [ "$SENSORS_FORCE_KBDSENSOR" == "1" ]; then
        # Option to force kbd sensor
        hal_sensors=kbd
        has_sensors=true
    else
        # if we have sensor module for our hardware, use it
        ro_hardware=$(getprop ro.hardware)
        [ -f /system/lib/hw/sensors.${ro_hardware}.so ] && return 0

        local hal_sensors=kbd
        local has_sensors=true
        case "$UEVENT" in
            *MS-N0E1*)
                set_property ro.ignore_atkbd 1
                set_property poweroff.doubleclick 0
                setkeycodes 0xa5 125
                setkeycodes 0xa7 1
                setkeycodes 0xe3 142
                ;;
            *Aspire1*25*)
                modprobe lis3lv02d_i2c
                echo -n "enabled" > /sys/class/thermal/thermal_zone0/mode
                ;;
            *Aspire*SW5-012*|*Venue*8*Pro*3845*|*ST70416-6*|*Akoya*P2213T*)
                set_property ro.iio.accel.order 102
                ;;
            *LenovoideapadD330*)
                set_property ro.iio.accel.order 102
                set_property ro.ignore_atkbd 1
                ;&
            *LINX1010B*)
                set_property ro.iio.accel.x.opt_scale -1
                set_property ro.iio.accel.z.opt_scale -1
                ;;
            *i7Stylus*|*SARY*TAB3*)
                set_property ro.iio.accel.x.opt_scale -1
                ;;
            *LenovoMIIX320*|*MIIX510*|*MIIX300-10IBY*|*ONDATablet*| \
			*TECLAST*X4*|*SF133AYR110*|*SolTIVW*)
                set_property ro.iio.accel.order 102
                set_property ro.iio.accel.x.opt_scale -1
                set_property ro.iio.accel.y.opt_scale -1
                ;;
            *Hi10*plus*|*T*0*TA*|*M80TA*|*TECLAST*X16*)
                set_property ro.iio.accel.y.opt_scale -1
                ;;
			*TAIFAElimuTab*)
				set_property ro.ignore_atkbd 1
				set_property ro.iio.accel.quirks no-trig
				set_property ro.iio.accel.order 102
				;;
            *SwitchSA5-271*|*SwitchSA5-271P*)
                set_property ro.ignore_atkbd 1
                has_sensors=true
                hal_sensors=iio
                ;&
            *)
                has_sensors=false
                ;;
        esac

            # has iio sensor-hub?
            if [ -n "`ls /sys/bus/iio/devices/iio:device* 2> /dev/null`" ]; then
                toybox chown -R 1000.1000 /sys/bus/iio/devices/iio:device*/
                [ -n "`ls /sys/bus/iio/devices/iio:device*/in_accel_x_raw 2> /dev/null`" ] && has_sensors=true
                hal_sensors=iio
            elif [ "$hal_sensors" != "kbd" ] | [ hal_sensors=iio ]; then
                has_sensors=true
            fi

            # is steam deck?
            if [ "$BOARD" == "Jupiter" ] && [ "$VENDOR" == "Valve" ]
            then
                set_property poweroff.disable_virtual_power_button 1
                hal_sensors=jupiter
                has_sensors=true
            fi
    fi

    set_property ro.iio.accel.quirks "no-trig,no-event"
    set_property ro.iio.anglvel.quirks "no-trig,no-event"
    set_property ro.iio.magn.quirks "no-trig,no-event"
    set_property ro.hardware.sensors $hal_sensors
    set_property config.override_forced_orient ${HAS_SENSORS:-$has_sensors}
}

function init_hal_surface()
{
	case "$PRODUCT" in
		*Surface*Pro*[4-9]*|*Surface*Book*|*Surface*Laptop*[1-4]*|*Surface*Laptop*Studio*)
			set_property vendor.iptsd.device "$(iptsd-find-hidraw)"
			;;
	esac

	# Handle a seperate case for Surface Pro 5 as it's initially being called
	# Surface Pro LTE 2017, which gives "Surface Pro" in product_name
	sku=$(cat /sys/class/dmi/id/product_sku 2>/dev/null)
	case "$sku" in
		Surface_Pro_1796|Surface_Pro_1807)
			# Execute command with hidraw device from iptsd-find-hidraw
			set_property vendor.iptsd.device "$(iptsd-find-hidraw)"
			;;
	esac
}

function create_pointercal()
{
	if [ ! -e /data/misc/tscal/pointercal ]; then
		mkdir -p /data/misc/tscal
		touch /data/misc/tscal/pointercal
		chown 1000.1000 /data/misc/tscal /data/misc/tscal/*
		chmod 775 /data/misc/tscal
		chmod 664 /data/misc/tscal/pointercal
	fi

	# setprop to tell the system to turn on TSCalibrartion
	set_property ro.tscal.enable true 
}

function init_tscal()
{
    if [ "$FORCE_TSCAL" -ge "1" ]; then
        create_pointercal
	else
		case "$UEVENT" in
			*T91*|*T101*|*ET2002*|*74499FU*|*945GSE-ITE8712*|*CF-19[CDYFGKLP]*|*TECLAST:rntPAD*)
				create_pointercal
				return
				;;
			*)
				;;
		esac

		for usbts in $(lsusb | awk '{ print $6 }'); do
			case "$usbts" in
				0596:0001|0eef:0001|14e1:6000|14e1:5000)
					create_pointercal
					return
					;;
				*)
					;;
			esac
		done
    fi
}

function init_ril()
{
	case "$UEVENT" in
		*TEGA*|*2010:svnIntel:*|*Lucid-MWE*)
			set_property rild.libpath /system/lib/libhuaweigeneric-ril.so
			set_property rild.libargs "-d /dev/ttyUSB2 -v /dev/ttyUSB1"
			set_property ro.radio.noril no
			;;
		*)
			set_property ro.radio.noril yes
			;;
	esac
}

function init_cpu_governor()
{
	governor=$(getprop cpu.governor)

	[ $governor ] && {
		for cpu in $(ls -d /sys/devices/system/cpu/cpu?); do
			echo $governor > $cpu/cpufreq/scaling_governor || return 1
		done
	}
}

function set_custom_ota()
{
	for c in `cat /proc/cmdline`; do
		case $c in
			*=*)
				eval $c
				if [ -z "$1" ]; then
					case $c in
						# Set TimeZone
					SET_CUSTOM_OTA_URI=*)
						# The Updater app reads lineage.updater.uri
						# (Updater app/src/main/java/org/lineageos/updater/
						# deviceinfo/DeviceInfoUtils.kt PROP_UPDATER_URI).
						# 'bliss.updater.uri' was never read by this app and
						# made the SET_CUSTOM_OTA_URI cmdline hook dead.
						setprop lineage.updater.uri "$SET_CUSTOM_OTA_URI"
						;;
					esac
				fi
				;;
		esac
	done
	
}

function init_prepare_ota()
{
	# If there's slot set, turn on bootctrl
	# If not, disable the OTA app (in bootcomplete)
	if [ "$(getprop ro.boot.slot_suffix)" ]; then
		start vendor.boot-hal-1-2
	fi
}

function set_custom_timezone()
{
	for c in `cat /proc/cmdline`; do
		case $c in
			*=*)
				eval $c
				if [ -z "$1" ]; then
					case $c in
						# Set TimeZone
						SET_TZ_LOCATION=*)
							settings put global time_zone "$SET_TZ_LOCATION"
							setprop persist.sys.timezone "$SET_TZ_LOCATION"
							;;
					esac
				fi
				;;
		esac
	done
	
}

function do_init()
{
	# Resolution switching without display destruction: SurfaceFlinger must
	# see this before its first flag read (statically cached), i.e. here in
	# post-fs, long before the graphics stack starts. Paired with the
	# modeset_state_machine OFF default and the HWC bounded modeset commit,
	# a resolution switch goes: initiate directly -> KMS modeset (black
	# buffer covers the gap) -> finalize without destroying the display ->
	# client framebuffers reallocate on the next invalidate. No wm size,
	# no persistent votes (TvSettings stores only on OK).
	set_property debug.sf.synced_resolution_switch true
	init_misc
	set_custom_timezone
	# Unbind fbcon from the framebuffer console: with fbcon bound and the
	# kernel console on the display, any printk during a KMS modeset lets
	# the text console steal the CRTC (black screen + cursor) no matter
	# whether the atomic commit itself succeeded. TV boxes need no text
	# console; this must run before graphics comes up.
	echo 0 > /sys/class/vtconsole/vtcon1/bind 2>/dev/null
	init_hal_audio
	set_custom_ota
	init_hal_brcm_wifi
	init_hal_bluetooth
	init_hal_camera
	init_hal_gps
	init_hal_gralloc
	init_hal_hwcomposer
	init_hal_media
	init_hal_vulkan
	init_hal_lights
	init_hal_power
	init_hal_sensors
	init_tscal
	init_ril
	init_prepare_ota
	post_init
}

function do_netconsole()
{
	modprobe netconsole netconsole="@/,@$(getprop dhcp.eth0.gateway)/"
}

function do_bootcomplete()
{
	init_cpu_governor

	[ -z "$(getprop persist.sys.root_access)" ] && setprop persist.sys.root_access 3

	lsmod | grep -Ehq "brcmfmac|rtl8723be" && setprop wlan.no-unload-driver 1

	case "$PRODUCT" in
		Surface*Go)
			echo on > /sys/devices/pci0000:00/0000:00:15.1/i2c_designware.1/power/control
			;;
		VMware*)
			;;
		X80*Power)
			export POWER_NONBOOT_CPU_OFF=${POWER_NONBOOT_CPU_OFF:-1}
			;;
		*)
			;;
	esac

    if [ "$POWER_NONBOOT_CPU_OFF" -ge 1 ]; then
        set_property power.nonboot-cpu-off 1
    fi

	# TEMPORARY WORKAROUND (los-audio-rescue): retry a failed kernel HDA
	# codec probe BEFORE any audio userspace init runs, so that a rescued
	# card goes through the normal bootcomplete audio path. Disabled by
	# default: run only when persist.sys.los.audio.rescue = "1" (opt-in).
	# See the comment block above init_hal_audio_rescue().
	init_hal_audio_rescue

	# FirmwareTool persistence: re-apply user firmware overrides
	# (/data/vendor_fw → bind mount over /vendor/firmware).
	init_vendor_fw_overlay

	# initialize audio in bootcomplete
	init_hal_audio_bootcomplete

	# Turn off TSCalibration if property isn't set
	if [ "$(getprop ro.tscal.enable)" == "true" ]; then
		pm enable org.zeroxlab.util.tscal
	else
		pm disable org.zeroxlab.util.tscal
	fi

	# check wifi setup
	FILE_CHECK=/data/misc/wifi/wpa_supplicant.conf

	if [ ! -f "$FILE_CHECK" ]; then
	    cp -a /system/etc/wifi/wpa_supplicant.conf $FILE_CHECK
            chown 1010.1010 $FILE_CHECK
            chmod 660 $FILE_CHECK
	fi

	POST_INST=/data/vendor/post_inst_complete
	USER_APPS=/system/etc/user_app/*
	BUILD_DATETIME=$(getprop ro.build.date.utc)
	POST_INST_NUM=$(cat $POST_INST)

	if [ ! "$BUILD_DATETIME" == "$POST_INST_NUM" ]; then
		for apk in $USER_APPS
		do		
			pm install $apk
		done
		rm "$POST_INST"
		touch "$POST_INST"
		echo $BUILD_DATETIME > "$POST_INST"
	fi

	# Seed prebuilt Magisk module(s) into /data/adb/modules on first boot.
	# Installs every zip in /system/etc/magisk-modules/ via its own installer.
	# Each module activates on the next reboot (Magisk magic mount).
	for MAGISK_SEED_ZIP in /system/etc/magisk-modules/*.zip; do
		[ -f "$MAGISK_SEED_ZIP" ] || continue
		[ -d /data/adb/modules ] || break
		MAGISK_SEEDED=/data/vendor/magisk_modules_seeded_$(basename "$MAGISK_SEED_ZIP" .zip)
		if [ ! -f "$MAGISK_SEEDED" ]; then
			mkdir -p /tmp/mtgseed
			if unzip -o -q "$MAGISK_SEED_ZIP" 'META-INF/com/google/android/update-binary' -d /tmp/mtgseed \
			    && sh /tmp/mtgseed/META-INF/com/google/android/update-binary 3 1 "$MAGISK_SEED_ZIP"; then
				touch "$MAGISK_SEEDED"
			fi
			rm -rf /tmp/mtgseed
		fi
	done

	#Auto activate XtMapper
	#nohup env LD_LIBRARY_PATH=$(echo /data/app/*/xtr.keymapper*/lib/x86_64) \
	#CLASSPATH=$(echo /data/app/*/xtr.keymapper*/base.apk) /system/bin/app_process \
	#/system/bin xtr.keymapper.server.InputService > /dev/null 2>&1 &

	if [ ! "$(getprop ro.boot.slot_suffix)" ]; then
		pm disable org.lineageos.updater
	fi

	# Enable LeanKeyKeyboard as the default IME on first boot only.
	# (It declares no locale subtypes, so the framework never picks it
	# automatically.) Only fills an empty selection, never overrides a
	# user choice.
	IME_PROVISIONED=/data/vendor/leankeyboard_ime_provisioned
	CUR_IME="$(settings get secure default_input_method)"
	CUR_PKG="${CUR_IME%%/*}"
	if [ ! -f "$IME_PROVISIONED" ] \
	    && [ -n "$(pm path org.liskovsoft.androidtv.rukeyboard 2>/dev/null)" ] \
	    && { [ -z "$CUR_IME" ] || [ -z "$(pm path $CUR_PKG 2>/dev/null)" ]; }; then
		ime enable org.liskovsoft.androidtv.rukeyboard/com.liskovsoft.leankeyboard.ime.LeanbackImeService
		ime set org.liskovsoft.androidtv.rukeyboard/com.liskovsoft.leankeyboard.ime.LeanbackImeService
		appops set --user 0 org.liskovsoft.androidtv.rukeyboard SYSTEM_ALERT_WINDOW allow
		touch "$IME_PROVISIONED"
	fi

	init_hal_thermal
	init_hal_surface
	post_bootcomplete
}

PATH=/sbin:/system/bin:/system/xbin:/vendor/bin:/product/bin:/system_ext/bin:/system/vendor/bin:/system/product/bin:/system/system_ext/bin

DMIPATH=/sys/class/dmi/id
BOARD=$(cat $DMIPATH/board_name)
PRODUCT=$(cat $DMIPATH/product_name)
VENDOR=$(cat $DMIPATH/sys_vendor)
UEVENT=$(cat $DMIPATH/uevent)
GPU=$(readlink /sys/class/graphics/fb0/device/driver)

# import cmdline variables
for c in `cat /proc/cmdline`; do
	case $c in
		BOOT_IMAGE=*|iso-scan/*|*.*=*)
			;;
		nomodeset)
			HWACCEL=0
			set_property mesa.libgl.always.software true
			;;
		*=*)
			eval $c
			if [ -z "$1" ]; then
				case $c in
					DEBUG=*)
						[ -n "$DEBUG" ] && set_property debug.logcat 1
						[ "$DEBUG" = "0" ] || SETUPWIZARD=${SETUPWIZARD:-0}
						;;
					DPI=*)
						set_property ro.sf.lcd_density "$DPI"
						;;
					SET_SF_ROTATION=*)
						set_property ro.sf.hwrotation "$SET_SF_ROTATION"
						;;
					SET_OVERRIDE_FORCED_ORIENT=*)
						set_property config.override_forced_orient "$SET_OVERRIDE_FORCED_ORIENT"
						;;
					SET_SYS_APP_ROTATION=*)
						# property: persist.sys.app.rotation has three cases:
						# 1.force_land: always show with landscape, if a portrait apk, system will scale up it
						# 2.middle_port: if a portrait apk, will show in the middle of the screen, left and right will show black
						# 3.original: original orientation, if a portrait apk, will rotate 270 degree
						set_property persist.sys.app.rotation "$SET_SYS_APP_ROTATION"
						;;
					# Battery Stats
					SET_FAKE_BATTERY_LEVEL=*)
						# Let us fake the total battery percentage
						# Range: 0-100
						dumpsys battery set level "$SET_FAKE_BATTERY_LEVEL"
						;;
					SET_FAKE_CHARGING_STATUS=*)
						# Allow forcing battery charging status
						# Off: 0  On: 1
						dumpsys battery set ac "$SET_FAKE_CHARGING_STATUS"
						;;
					FORCE_DISABLE_NAVIGATION=*)
						# Force disable navigation bar
						# options: true, false
						set_property persist.bliss.disable_navigation_bar "$FORCE_DISABLE_NAVIGATION"
						;;
					FORCE_DISABLE_NAV_HANDLE=*)
						# Force disable navigation handle
						# options: true, false
						set_property persist.bliss.disable_navigation_handle "$FORCE_DISABLE_NAV_HANDLE"
						;;
					FORCE_DISABLE_NAV_TASKBAR=*)
						# Force disable navigation taskbar
						# options: true, false
						set_property persist.bliss.disable_taskbar "$FORCE_DISABLE_NAV_TASKBAR"
						;;
					FORCE_DISABLE_STATUSBAR=*)
						# Force disable statusbar
						# options: true, false
						set_property persist.bliss.disable_statusbar "$FORCE_DISABLE_STATUSBAR"
						;;
					FORCE_DISABLE_RECENTS=*)
						# Force disable recents
						# options: true, false
						set_property persist.bliss.disable_recents "$FORCE_DISABLE_RECENTS"
						;;
				esac
				[ "$SETUPWIZARD" = "0" ] && set_property ro.setupwizard.mode DISABLED
			fi
			;;
	esac
done

[ -n "$DEBUG" ] && set -x || exec &> /dev/null

# import the vendor specific script
hw_sh=/vendor/etc/init.sh
[ -e $hw_sh ] && source $hw_sh

case "$1" in
	eglsetup)
		init_egl
		;;
	vendor-fw-overlay)
		init_vendor_fw_overlay
		;;
	netconsole)
		[ -n "$DEBUG" ] && do_netconsole
		;;
	bootcomplete)
		do_bootcomplete
		;;
	kmsg)
		dmesg -w > /data/kmsg.txt
		;;
	init|"")
		do_init
		;;
esac

return 0
