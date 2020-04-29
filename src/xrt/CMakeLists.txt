# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_subdirectory(include)
add_subdirectory(auxiliary)
add_subdirectory(drivers)
add_subdirectory(compositor)
add_subdirectory(state_trackers)
add_subdirectory(targets)

if(XRT_FEATURE_SERVICE)
	add_subdirectory(ipc)
endif()
