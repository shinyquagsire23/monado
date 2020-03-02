# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

add_subdirectory(include)
add_subdirectory(auxiliary)
add_subdirectory(drivers)
add_subdirectory(compositor)
add_subdirectory(state_trackers)
add_subdirectory(targets)
