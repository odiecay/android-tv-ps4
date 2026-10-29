#!/bin/sh
# Make Sony controllers act as the TV remote: cross selects and
# circle goes back. The PS button and circle are handled by ps-button (Home,
# or hold for the power menu; Back on release), so the PS button stays
# BUTTON_MODE and circle is left unmapped here: mapped to BACK, Android 16
# took one press as two Backs. Apps built for a remote often take gamepad A/B and ignore
# them, so the Android fallback from BUTTON_A/BUTTON_B never happens.
# Every controller button wakes the display (WAKE), as a remote's do.
# The sticks get a small dead zone: the driver reports none, and resting
# jitter otherwise counts as constant user activity, so the screensaver never
# starts.
set -eu
for file in /android/system/usr/keylayout/Vendor_054c_Product_*.kl /android/system/product/usr/keylayout/Vendor_054c_Product_*.kl; do
 [ -f "$file" ] || continue
 sed -E \
  -e 's/^(key[[:space:]]+0x[0-9a-fA-F]+[[:space:]]+)BUTTON_A([[:space:]]|$)/\1DPAD_CENTER\2/' \
  -e '/^key[[:space:]]+0x[0-9a-fA-F]+[[:space:]]+BUTTON_B([[:space:]]|$)/d' \
  -e 's/^(axis[[:space:]]+0x0[0134][[:space:]]+(X|Y|Z|RZ))[[:space:]]*$/\1 flat 12/' \
  -e '/^key[[:space:]]/{/[[:space:]]WAKE([[:space:]]|#|$)/!s/^(key[[:space:]]+[^#]*[^#[:space:]])/\1 WAKE/}' \
  "$file" > /run/ps4-controller-layout
 cat /run/ps4-controller-layout > "$file"
done
rm -f /run/ps4-controller-layout
