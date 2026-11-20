# Host test app: builds the pure-logic components for ESP-IDF's "linux" target and runs
# their Unity tests on the PC (in WSL). No board needed.
cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(host_test)

# linux target only: IDF v6.1.1's NVS (nvs_partition_manager.cpp:86) compares a Partition* with
# an intrusive_list iterator; under C++20+ rules that comparison is ambiguous and host GCC 11/12
# reject it. NVS is C++17 code, so compile just that library as C++17 (the last -std wins).
target_compile_options(__idf_nvs_flash PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>")
