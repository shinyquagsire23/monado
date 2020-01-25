# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

conf_data = configuration_data()
conf_data.set('XRT_BUILD_TRACKING', build_tracking)
conf_data.set('XRT_BUILD_DRIVER_DUMMY', 'dummy' in drivers)
conf_data.set('XRT_BUILD_DRIVER_HDK', 'hdk' in drivers)
conf_data.set('XRT_BUILD_DRIVER_HYDRA', 'hydra' in drivers)
conf_data.set('XRT_BUILD_DRIVER_OHMD', 'ohmd' in drivers)
conf_data.set('XRT_BUILD_DRIVER_PSMV', 'psmv' in drivers)
conf_data.set('XRT_BUILD_DRIVER_PSVR', 'psvr' in drivers)
conf_data.set('XRT_BUILD_DRIVER_V4L2', 'v4l2' in drivers)
conf_data.set('XRT_BUILD_DRIVER_VIVE', 'vive' in drivers)

targets_enabled_h = configure_file(
	input: 'targets_enabled_drivers.h.meson_in',
	output: 'targets_enabled_drivers.h',
	configuration: conf_data,
)

targets_enabled = declare_dependency(
	sources: targets_enabled_h,
	include_directories: include_directories('.'),
)

subdir('external')
subdir('xrt')
