# Host test app: builds the pure-logic components for ESP-IDF's "linux" target and runs
# their Unity tests on the PC (in WSL). No board needed.
cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(host_test)
