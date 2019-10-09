# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../../external
	)

set(GUI_INCLUDES)

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

# Use OBJECT to not create a archive, since it just gets in the way.
add_library(st_gui OBJECT ${GUI_SOURCE_FILES})

target_include_directories(st_gui
	PRIVATE
	${GUI_INCLUDES}
	)

set_property(TARGET st_gui PROPERTY POSITION_INDEPENDENT_CODE ON)
