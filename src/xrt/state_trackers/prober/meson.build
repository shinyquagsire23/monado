# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

prober_sources = [
	'p_documentation.h',
	'p_dump.c',
	'p_prober.c',
	'p_prober.h',
	'p_tracking.c',
]

prober_deps = [aux, targets_enabled]

if udev.found()
	prober_sources += ['p_udev.c']
	prober_deps += [udev]
endif

if libusb.found()
	prober_sources += ['p_libusb.c']
	prober_deps += [libusb]
endif

if libuvc.found()
	prober_sources += ['p_libuvc.c']
	prober_deps += [libuvc]
endif

if v4l2.found()
	prober_deps += [v4l2]
endif

lib_st_prober = static_library(
	'st_prober',
	files(prober_sources),
	include_directories: [
		xrt_include,
		drv_include,
		external_include,
	],
	dependencies: prober_deps,
)
