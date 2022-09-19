# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(aux_vive STATIC vive_config.h vive_config.c vive_bindings.c vive_bindings.h)
target_link_libraries(
	aux_vive
	PRIVATE
		xrt-interfaces
		aux_util
		aux_math
		aux_tracking
		xrt-external-cjson
		${ZLIB_LIBRARIES}
	)
target_include_directories(aux_vive PRIVATE ${ZLIB_INCLUDE_DIRS})
