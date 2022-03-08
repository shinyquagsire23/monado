# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Order matters.
add_subdirectory(include)
add_subdirectory(auxiliary)
add_subdirectory(tracking)
add_subdirectory(drivers)
add_subdirectory(compositor)
add_subdirectory(state_trackers)
add_subdirectory(targets)

if(XRT_FEATURE_IPC)
	add_subdirectory(ipc)
endif()
