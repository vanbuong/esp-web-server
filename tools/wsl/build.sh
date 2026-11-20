#!/usr/bin/env bash
# Usage: build.sh <profile> [extra idf.py actions, e.g. size]
# Builds firmware profile <profile> (esp32, esp32_psram, ...) into build/<profile>.
# Each profile keeps its own sdkconfig inside its build dir, so profiles never overwrite each other.
# IDF_TARGET is passed with -D instead of `idf.py set-target`, which would wipe the build dir every time.
set -uo pipefail
source "$(dirname "$0")/env.sh"
profile="${1:?profile required}"; shift
target="${profile%%_*}"             # esp32_psram -> esp32
cd "$REPO"
log="build/$profile.log"
mkdir -p build
if ! idf.py -B "build/$profile" -D IDF_TARGET="$target" -D SDKCONFIG="build/$profile/sdkconfig" \
        -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.$profile" build "$@" >"$log" 2>&1; then
    echo "build $profile FAILED — see $log"
    grep -E ": (fatal )?error:|undefined reference|CMake Error|unknown kconfig" "$log" | sort -u | head -30
    tail -15 "$log"
    exit 1
fi
grep -E ": warning:|unknown kconfig" "$log" | sort -u | head -20
tail -12 "$log"
