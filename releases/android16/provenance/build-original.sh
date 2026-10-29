#!/bin/sh
# Android TV for PS4 16 v0.1: one 7.2 kernel for every southbridge (branch v0.1).
# Output: test-builds/Android-TV-for-PS4-16-<version>.pkg
set -e
N=${1:?usage: build.sh <version, e.g. v0.1>}
P=/home/amber/projects/ps4-android; R=/mnt/nas/apps/ps4-android/releases/release-1.0
A=$P/android16; V=$A/v0.1; M="lib/modules/7.2.11-androidtv-ps4-a16+/extra"
[ "$(git -C /mnt/nas/apps/ps4-android branch --show-current)" = v0.1 ] || { echo "STOP: repo is not on branch v0.1" >&2; exit 1; }
# Out-of-tree modules must be rebuilt whenever the kernel is: a config change
# can move struct fields, and an old xt_quota2.ko crashed the kernel that way.
for ko in $V/runtime-additions/$M/xt_quota2.ko $V/runtime-additions/$M/psx_mts_net.ko \
          $V/runtime-additions/$M/wlan_mt76x8_sdio.ko \
          $V/runtime-additions/opt/androidtv-ps4/xt_quota2.ko $V/runtime-additions/opt/androidtv-ps4/android_usb_class.ko; do
 [ "$(modinfo -F vermagic "$ko" | cut -d' ' -f1)" = "$(strings $V/bzImage | grep -m1 -o '^7\.2\.11-androidtv-ps4-a16+')" ] || { echo "STOP: $ko does not match the kernel" >&2; exit 1; }
 [ "$ko" -nt $V/bzImage ] || { echo "STOP: $ko is older than the kernel; rebuild it" >&2; exit 1; }
done
cd /mnt/nas/apps/ps4-android && git show 94c8e34:releases/release-1.0/runtime-inputs.json > $R/runtime-inputs.json
python3 -c "import json;f='$R/runtime/manifest.json';m=json.load(open(f));m['initramfs_sha256']='074e526e7d971758b05f7b26a4b61c4bd3b0e21149a0cd47e89f5b2ba8eaffa3';json.dump(m,open(f,'w'),indent=2);open(f,'a').write(chr(10))"
# The installer only replaces system.sfs when its size differs from
# SYSTEM_BYTES; a stale value silently keeps the old image installed.
sed -i "s/^#define SYSTEM_BYTES .*/#define SYSTEM_BYTES $(stat -c %s $A/work/system.sfs)ULL/" $R/image-metadata.h
cd $R
grep -q "ATV_BUILD_ID \"$N\"" source/build-id.h || { echo "STOP: source/build-id.h is not $N" >&2; exit 1; }
# Modules for the 7.2 kernel replace the 6.15 ones (list restored after the build).
python3 - <<'PY'
import json,hashlib,os
R='/mnt/nas/apps/ps4-android/releases/release-1.0'; V='/home/amber/projects/ps4-android/android16/v0.1/runtime-additions'
M='lib/modules/7.2.11-androidtv-ps4-a16+/extra/'
j=json.load(open(R+'/runtime-additions.json')); f=j['files']
for k in [k for k in f if k.startswith('lib/modules/6.15.4-androidtv-ps4-aeolia/')]: del f[k]
f[M+'xt_quota2.ko']={'sha256':'','source':'quota2/xt_quota2.c built against kernel 7.2.11','mode':'0644'}
f[M+'psx_mts_net.ko']={'sha256':'','source':'linux-psx MTS Baikal Ethernet, in-tree module of kernel 7.2.11'}
f[M+'wlan_mt76x8_sdio.ko']={'sha256':'','source':'MT7668 Wi-Fi, in-tree mt76x8 driver of kernel 7.2.11 built as a module (WIPHY_FLAG_NETNS_OK)','location':'drive'}
f['opt/androidtv-ps4/AtvSetupWizardOverlay.apk']={'sha256':'','source':'setupwizard-overlay (setup starts at Welcome when a controller is connected), signed with the AOSP platform test key','mode':'0644'}
f['lib/firmware/edid/1920x1080.bin']={'sha256':'','source':'generic 1080p60 EDID (Linux tools/edid), Baikal fallback when the TV EDID cannot be read','mode':'0644'}
f['opt/androidtv-ps4/Gboard-split_config.mdpi.apk']={'sha256':'','source':'Gboard for TV 17.3.11 x86_64, density split that goes with the base APK in the image','mode':'0644'}
f['opt/androidtv-ps4/ps-button']={'sha256':'','source':'android16/tools/ps-button.c (static, PS button: Home / hold for power menu)','mode':'0755'}
for p in f:
    f[p]['sha256']=hashlib.sha256(open(os.path.join(V,p),'rb').read()).hexdigest()
json.dump(j,open(R+'/runtime-additions.json','w'),indent=2); open(R+'/runtime-additions.json','a').write('\n')
PY
B=$P/build-v01; rm -rf $B-assets $B $B-dist
python3 repack-runtime.py --assets $P/build-release-131-assets --output $B-assets --additions $V/runtime-additions > $V/repack-$N.log 2>&1
D=$B-assets
rm -f $D/bzImage $D/bzImage-baikal $D/system.sfs $D/bootargs.txt
cp $V/bzImage $D/bzImage
cp $P/build-release-131-assets/bootargs.txt $D/bootargs.txt
ln $A/work/system.sfs $D/system.sfs
python3 - <<'PY'
import json,hashlib
R='/mnt/nas/apps/ps4-android/releases/release-1.0'
m=json.load(open(R+'/runtime/manifest.json')); ri=json.load(open(R+'/runtime-inputs.json'))
m['initramfs_sha256']=ri['initramfs_sha256']
for p in list(m['text_files']):
    m['text_files'][p]=hashlib.sha256(open(R+'/runtime/'+p,'rb').read()).hexdigest()
json.dump(m,open(R+'/runtime/manifest.json','w'),indent=2); open(R+'/runtime/manifest.json','a').write('\n')
PY
python3 build-release.py --sdk $P/toolchains/OpenOrbis/PS4Toolchain --template $P/public-build-check/prepared/template --objects $P/public-build-check/prepared/objects --runtime $P/build-release-110-runtime --assets $D --probe-build $P/public-build-check/prepared/payload --probe-linker $R/helpers/linker.x --build $B --output $B-dist > $V/build-$N.log 2>&1
# Check what went in: runtime script, 7.2 modules, Wi-Fi package.
X=$V/initrd-check; rm -rf $X; mkdir $X; cd $X; xz -dc $D/initramfs.cpio.xz | cpio -idm --quiet 2>/dev/null
cmp $X/usr/sbin/androidtv-ps4-start $R/runtime/usr/sbin/androidtv-ps4-start && echo "initramfs has the v0.1 start script"
for f in device-name.sh device-name.rc init.sh controller-home.sh ps-button Gboard-split_config.mdpi.apk; do [ -f $X/opt/androidtv-ps4/$f ] && echo "initramfs has $f $(sha256sum $X/opt/androidtv-ps4/$f | cut -c1-12)" || echo "MISSING $f"; done
ls $X/$M/
tar -tzf $D/mt7668-wifi.tar.gz | grep '\.ko$'
mkdir -p $A/test-builds && cp $B-dist/Android-TV-for-PS4.pkg $A/test-builds/Android-TV-for-PS4-16-$N.pkg
sha256sum $A/test-builds/Android-TV-for-PS4-16-$N.pkg
cd /mnt/nas/apps/ps4-android && git checkout -- releases/release-1.0/runtime-additions.json
