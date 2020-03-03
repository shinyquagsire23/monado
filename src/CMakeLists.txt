# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# "Link" against this interface target if your module
# uses the files in "external".
add_library(xrt-external-headers INTERFACE)
target_include_directories(xrt-external-headers SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/external)

add_subdirectory(xrt)
