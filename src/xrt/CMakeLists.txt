# Copyright 2019-2023, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Order matters.
add_subdirectory(include)
add_subdirectory(auxiliary)
add_subdirectory(tracking)
add_subdirectory(drivers)
if(XRT_MODULE_COMPOSITOR)
	add_subdirectory(compositor)
endif()
add_subdirectory(state_trackers)
if(XRT_MODULE_IPC)
	add_subdirectory(ipc)
endif()
add_subdirectory(targets)
