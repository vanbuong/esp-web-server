cmake_minimum_required(VERSION 3.22)
# Firmware version; shown by /api/status and paired with the web UI version later (plan 3).
set(PROJECT_VER "0.1.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only the components main (and what it needs) pulls in — faster builds, smaller app.
idf_build_set_property(MINIMAL_BUILD ON)
project(gateway)
