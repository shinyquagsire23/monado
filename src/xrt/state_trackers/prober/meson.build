# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

prober_sources = [
	'p_documentation.h',
	'p_dump.c',
	'p_json.c',
	'p_prober.c',
	'p_prober.h',
	'p_tracking.c',
]

prober_deps = [aux, xrt_config_drivers, xrt_config_have]

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

lib_st_prober = static_library(
	'st_prober',
	files(prober_sources),
	include_directories: [
		xrt_include,
		drv_include,
		cjson_include,
	],
	dependencies: prober_deps,
)
