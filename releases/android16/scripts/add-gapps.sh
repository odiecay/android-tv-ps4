#!/bin/sh
# Adds the WayDroid-ATV MindTheGapps (baklava, x86) files to a copy of the
# LineageOS 23.2 TV system image, as root-owned files labelled system_file.
# Usage: add-gapps.sh <lineage system.img> <gapps-baklava checkout> <output.img>
set -eu
src=$1 gapps=$2 out=$3
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
debugfs=/usr/sbin/debugfs

cp --sparse=always "$src" "$out"
/usr/sbin/e2fsck -fy "$out" >/dev/null

printf 'u:object_r:system_file:s0\0' > "$work/label"
cmds=$work/cmds
: > "$cmds"

# file list: <source> <path inside the image>
list=$work/list
for tree in "$gapps/x86/proprietary" "$gapps/common/proprietary"; do
	(cd "$tree" && find . -type f) | sed 's|^\./||' | while read -r f; do
		echo "$tree/$f /system/$f"
	done
done > "$list"
# Architecture-independent APKs that only the arm64 tree carries.
for f in product/app/GoogleCalendarSyncAdapter/GoogleCalendarSyncAdapter.apk \
	 product/priv-app/GoogleOneTimeInitializer/GoogleOneTimeInitializer.apk \
	 system_ext/priv-app/GoogleFeedback/GoogleFeedback.apk; do
	echo "$gapps/arm64/proprietary/$f /system/$f" >> "$list"
done

made=$work/made
: > "$made"
while read -r from to; do
	dir=$(dirname "$to")
	# Create each missing parent directory once, root:root 0755.
	path=
	for part in $(echo "$dir" | tr '/' ' '); do
		path=$path/$part
		grep -qx "$path" "$made" && continue
		echo "$path" >> "$made"
		if ! $debugfs -R "stat $path" "$out" 2>/dev/null | grep -q 'Type: directory'; then
			{
				echo "mkdir $path"
				echo "sif $path mode 040755"
				echo "sif $path uid 0"
				echo "sif $path gid 0"
				echo "ea_set -f $work/label $path security.selinux"
			} >> "$cmds"
		fi
	done
	{
		echo "write $from $to"
		echo "sif $to mode 0100644"
		echo "sif $to uid 0"
		echo "sif $to gid 0"
		echo "ea_set -f $work/label $to security.selinux"
	} >> "$cmds"
done < "$list"

$debugfs -w -f "$cmds" "$out" > "$work/debugfs.log" 2>&1
if grep -iE 'error|not found|exists|cannot' "$work/debugfs.log"; then
	echo "debugfs reported problems; see above" >&2
	exit 1
fi
/usr/sbin/e2fsck -fn "$out"
echo "added $(wc -l < "$list") files"
