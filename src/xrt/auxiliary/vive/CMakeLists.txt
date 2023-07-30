# Copyright 2019-2023, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	aux_vive STATIC
	vive_bindings.c
	vive_bindings.h
	vive_calibration.c
	vive_calibration.h
	vive_config.h
	vive_config.c
	vive_poses.h
	vive_poses.c
	vive_tweaks.c
	vive_tweaks.h
	)
target_link_libraries(
	aux_vive
	PRIVATE
		xrt-interfaces
		aux_util
		aux_math
		aux_tracking
		xrt-external-cjson
	)
