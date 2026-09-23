#!/usr/bin/env bash
# Installs the APK on a connected Quest and pushes the game data extracted from your ROM.
# Run `cmake --build build-cmake --target ExtractAssets` on the desktop build first.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG=dev.spaghettikart.vr
DEST=/sdcard/Android/data/$PKG/files
APK="${APK:-$ROOT/android/app/build/outputs/apk/release/app-release.apk}"
DATA="${DATA_DIR:-$ROOT/build-cmake}"

adb install -r "$APK"
adb shell mkdir -p "$DEST"

# Carry over saves and settings from the Quest-only builds, which used a different package name
OLD_DEST=/sdcard/Android/data/dev.spaghettikart.quest/files
for f in default.sav controllerPak_header.sav spaghettify.cfg.json; do
    if [[ -z "$(adb shell "[ -e $DEST/$f ] && echo yes")" ]]; then
        adb shell "[ -e $OLD_DEST/$f ] && cp $OLD_DEST/$f $DEST/$f" || true
    fi
done
for f in mk64.o2r spaghetti.o2r; do
    adb push "$DATA/$f" "$DEST/$f"
done
adb push "$ROOT/config.yml" "$DEST/"
# adb push can't create nested folders under Android/data, so unpack an archive on the device instead
TMP_TAR=/data/local/tmp/spaghettikart-data.tar
tar -C "$ROOT" -cf - yamls meta | adb shell "cat > $TMP_TAR"
adb shell "cd $DEST && tar -xf $TMP_TAR && rm $TMP_TAR"

if [[ "${1:-}" == "--run" ]]; then
    adb logcat -c
    adb shell am start -n $PKG/.MainActivity
    adb logcat -s SDL SDL/APP SDL/ERROR libc DEBUG AndroidRuntime
fi
