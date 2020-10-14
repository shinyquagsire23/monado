# Copyright 2019-2020, Collabora, Ltd.
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
	'gui_scene_remote.c',
	'gui_scene_video.c',
	'../../../external/imgui/imgui/cimgui.cpp',
	'../../../external/imgui/imgui/cimgui.h',
	'../../../external/imgui/imgui/cimplot.cpp',
	'../../../external/imgui/imgui/cimplot.h',
	'../../../external/imgui/imgui/imconfig.h',
	'../../../external/imgui/imgui/imgui.cpp',
	'../../../external/imgui/imgui/imgui.h',
	'../../../external/imgui/imgui/imgui_demo.cpp',
	'../../../external/imgui/imgui/imgui_draw.cpp',
	'../../../external/imgui/imgui/imgui_impl_opengl3.cpp',
	'../../../external/imgui/imgui/imgui_impl_opengl3.h',
	'../../../external/imgui/imgui/imgui_internal.h',
	'../../../external/imgui/imgui/imgui_widgets.cpp',
	'../../../external/imgui/imgui/implot.cpp',
	'../../../external/imgui/imgui/implot.h',
	'../../../external/imgui/imgui/implot_internal.h',
	'../../../external/imgui/imgui/implot_items.cpp',
	'../../../external/imgui/imgui/imstb_rectpack.h',
	'../../../external/imgui/imgui/imstb_textedit.h',
	'../../../external/imgui/imgui/imstb_truetype.h',
	'../../../external/imgui/imgui_monado/cimgui_monado.h',
	'../../../external/imgui/imgui_monado/imgui_monado.cpp'
]

gui_deps = [aux, xrt_config_have]

lib_st_gui = static_library(
	'st_gui',
	files(gui_sources),
	include_directories: [
		xrt_include,
		drv_include,
		glad_include,
		cjson_include,
		imgui_include,
	],
	dependencies: gui_deps,
)
