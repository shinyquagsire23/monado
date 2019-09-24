# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

configure_file(targets_enabled_drivers.h.cmake_in ${CMAKE_CURRENT_BINARY_DIR}/targets_enabled_drivers.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(xrt)
