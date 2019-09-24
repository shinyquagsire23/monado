# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a loadable OpenXR driver.

# runtime_bare_suffix = 'xrt'
# runtime_prefix = runtime_bare_prefix + '_'
runtime_prefix = ''

runtime_bare_suffix = 'monado'
runtime_suffix = '_' + runtime_bare_suffix

runtime_target = '@0@openxr@1@'.format(runtime_prefix, runtime_suffix)

# OpenXR 1.0
xr_api_major = 1

openxr = library(
	runtime_target,
	files('target.c'),
	link_whole: [
		lib_aux_os,
		lib_aux_util,
		lib_aux_math,
		lib_comp,
		lib_st_oxr,
		lib_st_prober,
		lib_target_lists,
	] + driver_libs,
	include_directories: [
		aux_include,
		common_include,
		drv_include,
		xrt_include,
	],
	dependencies: [
		libusb,
		libuvc,
		opengl,
		pthreads,
		targets_enabled,
		udev,
		vulkan,
		x11,
		xcb,
		xcb_randr,
	] + driver_deps,
	install: true,
)

manifest_in = files('openxr_monado.meson.in.json')

manifest_conf = configuration_data()
# https://github.com/mesonbuild/meson/issues/5941
sopath = join_paths(get_option('prefix'),
	get_option('libdir'), 'libopenxr_monado.so')
manifest_conf.set('runtime_path', sopath)

manifest_path = join_paths(
	get_option('prefix'), get_option('datadir'), xr_api_major.to_string())

# configure_file(install: ...) was added in meson 0.50
# TODO: drop the version check once 0.50 is more widely adopted (i.e. Debian)
# https://repology.org/badge/vertical-allrepos/meson.svg?minversion=0.50.0
meson_version = meson.version().split('.')

if meson_version[1].to_int() < 50
	manifest_json = configure_file(
		input: manifest_in,
		output: 'openxr_monado.json',
		configuration: manifest_conf,
	)

	install_data([manifest_json], install_dir: manifest_path)
else
	manifest_json = configure_file(
		input: manifest_in,
		output: 'openxr_monado.json',
		configuration: manifest_conf,
		install: true,
		install_dir: manifest_path,
	)
endif

meson.add_install_script('install_active_runtime.sh', get_option('sysconfdir'),
	join_paths(manifest_path, runtime_target + '.json'), xr_api_major.to_string())
