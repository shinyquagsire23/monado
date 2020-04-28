# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# This is where we collect all of the pieces from the different parts of
# the source tree and build a complete driver or integration part.


add_subdirectory(common)

if(BUILD_TARGET_OPENXR)
	add_subdirectory(openxr)
endif()

add_subdirectory(cli)

if(BUILD_TARGET_GUI)
	add_subdirectory(gui)
endif()

if(XRT_BUILD_IPC)
	add_subdirectory(service)
endif()
