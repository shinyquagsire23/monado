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
		'gui_common.h',
		'gui_imgui.h',
		'gui_ogl.c',
		'gui_prober.c',
		'gui_scene.cpp',
		'gui_scene_calibrate.c',
		'gui_scene_debug.c',
		'gui_scene_main_menu.c',
		'gui_scene_video.c',
		'gui_sdl2.c',
		'gui_sdl2_imgui.c',
		'gui_sdl2_main.c',
		'gui_sdl2_prober.c',
		'../../../external/glad/gl.h',
		'../../../external/glad/gl.c',
		'../../../external/imgui/cimgui.cpp',
		'../../../external/imgui/cimgui.h',
		'../../../external/imgui/imconfig.h',
		'../../../external/imgui/imgui.cpp',
		'../../../external/imgui/imgui.h',
		'../../../external/imgui/imgui_demo.cpp',
		'../../../external/imgui/imgui_draw.cpp',
		'../../../external/imgui/imgui_impl_opengl3.cpp',
		'../../../external/imgui/imgui_impl_opengl3.h',
		'../../../external/imgui/imgui_impl_sdl.cpp',
		'../../../external/imgui/imgui_impl_sdl.h',
		'../../../external/imgui/imgui_internal.h',
		'../../../external/imgui/imgui_widgets.cpp',
		'../../../external/imgui/imstb_rectpack.h',
		'../../../external/imgui/imstb_textedit.h',
		'../../../external/imgui/imstb_truetype.h',
	),
	link_whole: [
		lib_aux_os,
		lib_aux_util,
		lib_aux_math,
		lib_st_prober,
		lib_target_lists,
	] + driver_libs,
	include_directories: [
		aux_include,
		common_include,
		external_include,
		drv_include,
		xrt_include,
	],
	dependencies: gui_deps + driver_deps,
)
