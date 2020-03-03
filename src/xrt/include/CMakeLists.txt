# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_subdirectory(xrt)

add_library(xrt-interfaces INTERFACE)
target_include_directories(xrt-interfaces INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
