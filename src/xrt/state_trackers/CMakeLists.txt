# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

if(XRT_HAVE_OPENGL)
	add_subdirectory(gui)
endif()
add_subdirectory(oxr)
add_subdirectory(prober)

if(XRT_FEATURE_STEAMVR_PLUGIN)
	add_subdirectory(steamvr_drv)
endif()
