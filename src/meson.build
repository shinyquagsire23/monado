# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

conf_data = configuration_data()
conf_data.set('XRT_BUILD_DRIVER_OHMD', 'ohmd' in drivers)
conf_data.set('XRT_BUILD_DRIVER_HDK', 'hdk' in drivers)
conf_data.set('XRT_BUILD_DRIVER_PSMV', 'psmv' in drivers)
conf_data.set('XRT_BUILD_DRIVER_PSVR', 'psvr' in drivers)

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
