#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_uart_4)

include(utils/cmake/utils.cmake)

# Needed for colorful output
zephyr_compile_options(-fdiagnostics-color=always)

# Use hardware acceleration for DSP and Neural Network code
add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=1
                -DARM_MATH_LOOPUNROLL
                )

# Add the Edge Impulse SDK
add_subdirectory(edge-impulse-sdk/cmake/zephyr)

# Edge impulse SDK include directories
set(INCLUDES
    .
    src
    tflite-model
    model-parameters
    )
include_directories(${INCLUDES})

RECURSIVE_FIND_FILE(MODEL_FILES "tflite-model" "*.cpp")

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.cpp
  ${MODEL_FILES}
)

# NORDIC SDK APP END
