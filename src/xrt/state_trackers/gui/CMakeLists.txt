# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(GUI_SOURCE_FILES
	gui_common.h
	gui_imgui.h
	gui_ogl.c
	gui_prober.c
	gui_scene.cpp
	gui_scene_calibrate.c
	gui_scene_debug.c
	gui_scene_main_menu.c
	gui_scene_video.c
	../../../external/imgui/cimgui.cpp
	../../../external/imgui_monado/imgui_monado.cpp
	../../../external/imgui/cimgui.h
	../../../external/imgui/imconfig.h
	../../../external/imgui/imgui.cpp
	../../../external/imgui/imgui.h
	../../../external/imgui/imgui_demo.cpp
	../../../external/imgui/imgui_draw.cpp
	../../../external/imgui/imgui_impl_opengl3.cpp
	../../../external/imgui/imgui_impl_opengl3.h
	../../../external/imgui/imgui_internal.h
	../../../external/imgui/imgui_widgets.cpp
	../../../external/imgui/imstb_rectpack.h
	../../../external/imgui/imstb_textedit.h
	../../../external/imgui/imstb_truetype.h
	)

add_library(st_gui STATIC
	${GUI_SOURCE_FILES}
	)

target_link_libraries(st_gui PRIVATE
	xrt-external-headers
	aux_util
	)
target_include_directories(st_gui PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/..
	)

if(BUILD_WITH_SDL2)
	add_library(imgui_impl_sdl STATIC 
		../../../external/imgui/cimgui_sdl.cpp
		../../../external/imgui/imgui_impl_sdl.cpp
		../../../external/imgui/imgui_impl_sdl.h
		)
	target_link_libraries(imgui_impl_sdl PRIVATE
		${SDL2_LIBRARIES}
		xrt-external-headers
		)
	target_include_directories(imgui_impl_sdl PRIVATE
		${SDL2_INCLUDE_DIRS}
		)
endif()
