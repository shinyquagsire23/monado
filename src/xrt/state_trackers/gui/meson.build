# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

gui_sources = [
	'gui_common.h',
	'gui_imgui.h',
	'gui_ogl.c',
	'gui_prober.c',
	'gui_scene.cpp',
	'gui_scene_calibrate.c',
	'gui_scene_debug.c',
	'gui_scene_main_menu.c',
	'gui_scene_video.c',
	'../../../external/imgui/cimgui.cpp',
	'../../../external/imgui/cimgui.h',
	'../../../external/imgui/imconfig.h',
	'../../../external/imgui/imgui.cpp',
	'../../../external/imgui/imgui.h',
	'../../../external/imgui/imgui_demo.cpp',
	'../../../external/imgui/imgui_draw.cpp',
	'../../../external/imgui/imgui_impl_opengl3.cpp',
	'../../../external/imgui/imgui_impl_opengl3.h',
	'../../../external/imgui/imgui_internal.h',
	'../../../external/imgui/imgui_widgets.cpp',
	'../../../external/imgui/imstb_rectpack.h',
	'../../../external/imgui/imstb_textedit.h',
	'../../../external/imgui/imstb_truetype.h',
]

gui_deps = [aux, xrt_config_have]

lib_st_gui = static_library(
	'st_gui',
	files(gui_sources),
	include_directories: [
		xrt_include,
		external_include,
	],
	dependencies: gui_deps,
)
