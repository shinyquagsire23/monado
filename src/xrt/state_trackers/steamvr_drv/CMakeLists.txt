# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(OVRD_SOURCE_FILES
	ovrd_driver.cpp
	ovrd_interface.h
	)

add_library(st_ovrd STATIC
	${OVRD_SOURCE_FILES}
	)
target_include_directories(st_ovrd INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}
	)
target_link_libraries(st_ovrd PRIVATE
	xrt-interfaces
	xrt-external-openvr
	aux_math
	aux_generated_bindings
	)
