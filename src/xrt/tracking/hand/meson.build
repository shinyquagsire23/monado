# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

subdir('old_rgb')


###
# Async wrapper around sync helper.
#
lib_t_hand_async = static_library(
	't_hand_async',
	files(
		't_hand_tracking_async.c'
	),
	include_directories: [xrt_include],
	dependencies: [aux]
)
