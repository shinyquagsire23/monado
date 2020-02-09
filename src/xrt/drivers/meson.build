# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

drv_include = include_directories('.')

lib_drv_dummy = static_library(
	'drv_dummy',
	files(
		'dummy/dummy_hmd.c',
		'dummy/dummy_interface.h',
		'dummy/dummy_prober.c',
	),
	include_directories: xrt_include,
	dependencies: [aux],
	build_by_default: 'dummy' in drivers,
)

lib_drv_hdk = static_library(
	'drv_hdk',
	files(
		'hdk/hdk_device.cpp',
		'hdk/hdk_device.h',
		'hdk/hdk_interface.h',
		'hdk/hdk_prober.c',
	),
	include_directories: xrt_include,
	dependencies: [aux],
	build_by_default: 'hdk' in drivers,
)

lib_drv_hydra = static_library(
	'drv_hydra',
	files(
		'hydra/hydra_driver.c',
		'hydra/hydra_interface.h',
	),
	include_directories: xrt_include,
	dependencies: [aux],
	build_by_default: 'hydra' in drivers,
)

lib_drv_ns = static_library(
	'drv_ns',
	files(
		'north_star/ns_hmd.c',
		'north_star/ns_interface.h',
		'north_star/ns_prober.c',
	),
	include_directories: xrt_include,
	dependencies: [aux],
	build_by_default: 'ns' in drivers,
)

lib_drv_ohmd = static_library(
	'drv_ohmd',
	files(
		'ohmd/oh_device.c',
		'ohmd/oh_device.h',
		'ohmd/oh_interface.h',
		'ohmd/oh_prober.c',
	),
	include_directories: xrt_include,
	dependencies: [aux, openhmd],
	build_by_default: 'ohmd' in drivers,
)

lib_drv_psmv = static_library(
	'drv_psmv',
	files(
		'psmv/psmv_driver.c',
		'psmv/psmv_interface.h',
	),
	include_directories: xrt_include,
	dependencies: [aux],
	build_by_default: 'psmv' in drivers,
)

lib_drv_psvr = static_library(
	'drv_psvr',
	files(
		'psvr/psvr_device.c',
		'psvr/psvr_device.h',
		'psvr/psvr_interface.h',
		'psvr/psvr_packet.c',
		'psvr/psvr_prober.c',
	),
	include_directories: xrt_include,
	dependencies: [aux, hidapi],
	build_by_default: 'psvr' in drivers,
)

lib_drv_rs = static_library(
	'drv_rs',
	files(
		'realsense/rs_6dof.c',
		'realsense/rs_interface.h',
	),
	include_directories: xrt_include,
	dependencies: [aux, rs],
	build_by_default: 'rs' in drivers,
)

lib_drv_v4l2 = static_library(
	'drv_v4l2',
	files(
		'v4l2/v4l2_driver.c',
	),
	include_directories: xrt_include,
	dependencies: [aux, v4l2],
	build_by_default: 'v4l2' in drivers,
)

lib_drv_vive = static_library(
	'drv_vive',
	files(
		'vive/vive_device.c',
		'vive/vive_device.h',
		'vive/vive_protocol.h',
		'vive/vive_prober.h',
		'vive/vive_prober.c',
	),
	include_directories: [xrt_include,
	                      include_directories('../../external/')],
	dependencies: [aux, zlib],
	build_by_default: 'vive' in drivers,
)
