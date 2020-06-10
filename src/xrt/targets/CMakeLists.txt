# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# This is where we collect all of the pieces from the different parts of
# the source tree and build a complete driver or integration part.


add_subdirectory(common)

if(XRT_FEATURE_OPENXR)
	add_subdirectory(openxr)
endif()

add_subdirectory(cli)

if(XRT_HAVE_SDL2)
	add_subdirectory(gui)
endif()

if(XRT_FEATURE_SERVICE)
	add_subdirectory(ctl)
endif()

if(XRT_FEATURE_SERVICE AND XRT_FEATURE_OPENXR)
	if(ANDROID)
		add_subdirectory(service-lib)
	else()
		add_subdirectory(service)
	endif()
endif()

if(XRT_FEATURE_STEAMVR_PLUGIN)
	add_subdirectory(steamvr_drv)
endif()
