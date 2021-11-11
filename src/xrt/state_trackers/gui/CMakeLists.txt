# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# c-imgui doesn't do well with IPO - lots of warnings.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

add_library(
	st_gui STATIC
	gui_common.h
	gui_imgui.h
	gui_ogl.c
	gui_prober.c
	gui_scene.cpp
	gui_scene_calibrate.c
	gui_scene_debug.c
	gui_scene_main_menu.c
	gui_scene_record.c
	gui_scene_remote.c
	gui_scene_video.c
	gui_scene_tracking_overrides.c
	gui_stb.c
	gui_window_record.c
	gui_window_record.h
	)

target_link_libraries(
	st_gui
	PRIVATE
		xrt-external-stb
		aux_util
		aux_os
		aux_ogl
		drv_includes
		xrt-external-imgui
	)
target_include_directories(st_gui INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(XRT_HAVE_GST)
	target_link_libraries(st_gui PRIVATE aux_gstreamer)
endif()

if(XRT_BUILD_DRIVER_DEPTHAI)
	target_link_libraries(st_gui PRIVATE drv_depthai)
endif()

if(XRT_BUILD_DRIVER_REMOTE)
	target_link_libraries(st_gui PRIVATE drv_remote)
endif()

if(XRT_HAVE_SDL2)
	target_link_libraries(st_gui PUBLIC xrt-external-imgui-sdl2)
endif()
