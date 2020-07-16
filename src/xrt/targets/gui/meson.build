# Copyright 2019-2020, Collabora, Ltd.
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
		'../../../external/imgui/imgui/cimgui_sdl.cpp',
		'../../../external/imgui/imgui/imgui_impl_sdl.cpp',
		'../../../external/imgui/imgui/imgui_impl_sdl.h',
	),
	link_whole: [
		lib_aux_os,
		lib_aux_ogl,
		lib_aux_util,
		lib_aux_math,
		lib_st_prober,
		lib_st_gui,
		lib_target_instance_no_comp,
	] + driver_libs,
	include_directories: [
		aux_include,
		common_include,
		glad_include,
		st_include,
		drv_include,
		xrt_include,
		imgui_include,
	],
	dependencies: gui_deps + driver_deps,
	install: true,
)
