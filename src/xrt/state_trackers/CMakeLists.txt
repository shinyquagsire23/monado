# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

if(BUILD_WITH_OPENGL)
	add_subdirectory(gui)
endif()
add_subdirectory(oxr)
add_subdirectory(prober)
