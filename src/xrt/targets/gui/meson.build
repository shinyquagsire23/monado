# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

gui_deps = [
		libusb,
		libuvc,
		pthreads,
		sdl2,
		udev,
]

if libjpeg.found()
		gui_deps += [libjpeg]
endif

gui = executable(
	'monado-gui',
	files(
		'gui_sdl2.c',
		'gui_sdl2_imgui.c',
		'gui_sdl2_main.c',
		'gui_sdl2_prober.c',
		'../../../external/imgui/cimgui_sdl.cpp',
		'../../../external/imgui/imgui_impl_sdl.cpp',
		'../../../external/imgui/imgui_impl_sdl.h',
	),
	link_whole: [
		lib_aux_os,
		lib_aux_ogl,
		lib_aux_util,
		lib_aux_math,
		lib_st_prober,
		lib_st_gui,
		lib_target_lists,
	] + driver_libs,
	include_directories: [
		aux_include,
		common_include,
		external_include,
		st_include,
		drv_include,
		xrt_include,
	],
	dependencies: gui_deps + driver_deps,
)
