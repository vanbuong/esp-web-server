#!/usr/bin/env bash
# Builds test/host for ESP-IDF's "linux" target and runs the Unity tests on the PC.
# Why WSL: the linux target needs a POSIX host; it does not build on native Windows.
set -uo pipefail
source "$(dirname "$0")/env.sh"
cd "$REPO/test/host"
log="build-linux.log"
if ! idf.py -B build-linux -D IDF_TARGET=linux build >"$log" 2>&1; then
    echo "host test build FAILED — last lines of test/host/$log:"
    grep -E ": (fatal )?error:|undefined reference|CMake Error" "$log" | sort -u | head -30
    tail -15 "$log"
    exit 1
fi
./build-linux/host_test.elf
