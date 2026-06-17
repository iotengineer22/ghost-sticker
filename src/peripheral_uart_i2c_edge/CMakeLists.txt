#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_uart_4)

# Add the Edge Impulse SDK as a subdirectory.
include(edge-impulse-sdk/cmake/utils.cmake)
add_subdirectory(edge-impulse-sdk/cmake/zephyr)

# Recursively find and add the model source files.
RECURSIVE_FIND_FILE_APPEND(MODEL_SOURCE "tflite-model" "*.cpp")
target_sources(app PRIVATE ${MODEL_SOURCE})

# Add the main application source file (main.cpp).
target_sources(app PRIVATE src/main.cpp)

# Specify include directories.
target_include_directories(app PRIVATE .)

# Suppress warnings from Edge Impulse SDK
target_compile_options(app PRIVATE -Wno-double-promotion -Wno-maybe-uninitialized)
