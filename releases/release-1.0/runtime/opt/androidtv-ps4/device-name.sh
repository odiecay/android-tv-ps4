#!/system/bin/sh
# Boot-time Android defaults for the PS4. Runs on every boot once Android
# reports boot completion. On a fresh install, Android TV setup is still
# starting at that point and an early write can fail, so each setting is
# retried and read back for up to five minutes. Every step is logged:
# logcat -s androidtv-defaults
note() { log -p i -t androidtv-defaults "$*"; }

# ensure TABLE KEY VALUE: write the setting until reading it back matches.
ensure() {
    attempt=1
    while [ "$attempt" -le 60 ]; do
        if [ "$(settings get "$1" "$2" 2>&1)" = "$3" ]; then
            note "$1 $2=$3 (attempt $attempt)"
            return 0
        fi
        result=$(settings put "$1" "$2" "$3" 2>&1) || note "$1 $2: write failed: $result"
        if [ "$(settings get "$1" "$2" 2>&1)" = "$3" ]; then
            note "$1 $2=$3 (attempt $attempt)"
            return 0
        fi
        attempt=$((attempt + 1))
        sleep 5
    done
    note "$1 $2: gave up, still $(settings get "$1" "$2" 2>&1)"
    return 1
}

note "applying PS4 defaults"

# Android sleep locks up PS4 Linux, so idle time runs the screensaver instead
# and never goes to sleep. The screensaver itself is the build default
# (screensaver-overlay); these settings back it up on installs from 1.0.
ensure secure screensaver_enabled 1
ensure secure screensaver_activate_on_sleep 1
ensure secure sleep_timeout -1

# Name the device "PlayStation 4" without altering Build.MODEL. The overlay
# makes it the setup default; this also renames a 1.0 install once. A name
# chosen later in Settings is kept.
if [ "$(getprop persist.vendor.androidtv.named)" != 1 ]; then
    if ensure global device_name 'PlayStation 4'; then
        setprop persist.vendor.androidtv.named 1 || note "could not record that the name was set"
    fi
fi
# Google voice typing ships as a keyboard and grabs every text field, opening
# its own setup screen. Make Gboard the keyboard once; a keyboard chosen
# later in Settings is kept.
gboard=com.google.android.inputmethod.latin/com.android.inputmethod.latin.LatinIME
voice=com.google.android.tts/com.google.android.apps.speech.tts.googletts.settings.asr.voiceime.VoiceInputMethodService
if [ "$(getprop persist.vendor.androidtv.keyboard)" != 1 ] && ime list -s -a | grep -qx "$gboard"; then
    ime enable "$gboard" >/dev/null 2>&1
    ime list -s -a | grep -qx "$voice" && ime disable "$voice" >/dev/null 2>&1
    if ensure secure default_input_method "$gboard"; then
        setprop persist.vendor.androidtv.keyboard 1 || note "could not record that the keyboard was set"
    fi
fi
note "done"
