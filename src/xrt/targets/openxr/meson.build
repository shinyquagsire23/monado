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

hack_deps = []
hack_src = []
hack_libs = []
hack_incs = []

if sdl2.found()
	hack_deps += sdl2
	hack_src += [
		'oxr_sdl2_hack.c',
		'../../../external/imgui/cimgui_sdl.cpp',
		'../../../external/imgui/imgui_impl_sdl.cpp',
		'../../../external/imgui/imgui_impl_sdl.h',
	]
	hack_libs += lib_st_gui
	hack_incs += [
		st_include,
	]
else
	hack_src += 'oxr_sdl2_hack.c'
endif

openxr_deps = [
		libusb,
		libuvc,
		pthreads,
		targets_enabled,
		udev,
		vulkan,
	]

if build_opengl
	openxr_deps += [opengl]
endif

if build_opengl and build_xlib
	openxr_deps += [x11]
endif

if build_xcb
	openxr_deps += [xcb]
endif

if build_xcb_xrandr_direct
	openxr_deps += [xcb_randr]
endif

openxr = library(
	runtime_target,
	files(
		'target.c',
		hack_src,
	),
	link_whole: [
		lib_aux_vk,
		lib_aux_os,
		lib_aux_ogl,
		lib_aux_util,
		lib_aux_math,
		lib_comp,
		lib_st_oxr,
		lib_st_prober,
		lib_target_lists,
	] + driver_libs + hack_libs,
	include_directories: [
		external_include,
		st_include, # Sigh debian meson requires this.
		aux_include,
		common_include,
		drv_include,
		xrt_include,
	] + hack_incs,
	dependencies: openxr_deps + driver_deps + hack_deps,
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

manifest_json = configure_file(
	input: manifest_in,
	output: 'openxr_monado.json',
	configuration: manifest_conf,
	install_dir: manifest_path,
)

meson.add_install_script('install_active_runtime.sh', get_option('sysconfdir'),
	join_paths(manifest_path, runtime_target + '.json'), xr_api_major.to_string())
