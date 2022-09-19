# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(comp_null STATIC null_compositor.c null_compositor.h)
target_link_libraries(
	comp_null
	PUBLIC xrt-interfaces
	PRIVATE
		aux_util
		aux_os
		aux_vk
		comp_util
		comp_multi
	)
target_include_directories(comp_null PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
