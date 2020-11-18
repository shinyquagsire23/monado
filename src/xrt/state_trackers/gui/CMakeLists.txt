# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# c-imgui doesn't do well with IPO - lots of warnings.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

set(GUI_SOURCE_FILES
	gui_common.h
	gui_imgui.h
	gui_ogl.c
	gui_prober.c
	gui_scene.cpp
	gui_scene_calibrate.c
	gui_scene_debug.c
	gui_scene_main_menu.c
	gui_scene_remote.c
	gui_scene_video.c
	../../../external/imgui/imgui/cimgui.cpp
	../../../external/imgui/imgui/cimgui.h
	../../../external/imgui/imgui/cimplot.cpp
	../../../external/imgui/imgui/cimplot.h
	../../../external/imgui/imgui/imconfig.h
	../../../external/imgui/imgui/imgui.cpp
	../../../external/imgui/imgui/imgui.h
	../../../external/imgui/imgui/imgui_demo.cpp
	../../../external/imgui/imgui/imgui_draw.cpp
	../../../external/imgui/imgui/imgui_impl_opengl3.cpp
	../../../external/imgui/imgui/imgui_impl_opengl3.h
	../../../external/imgui/imgui/imgui_internal.h
	../../../external/imgui/imgui/imgui_widgets.cpp
	../../../external/imgui/imgui/implot.cpp
	../../../external/imgui/imgui/implot.h
	../../../external/imgui/imgui/implot_internal.h
	../../../external/imgui/imgui/implot_items.cpp
	../../../external/imgui/imgui/imstb_rectpack.h
	../../../external/imgui/imgui/imstb_textedit.h
	../../../external/imgui/imgui/imstb_truetype.h
	../../../external/imgui/imgui_monado/imgui_monado.cpp
	)

add_library(st_gui STATIC
	${GUI_SOURCE_FILES}
	)

target_link_libraries(st_gui PRIVATE
	xrt-external-glad
	aux_util
	aux_os
	)
target_include_directories(st_gui PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${CMAKE_CURRENT_SOURCE_DIR}/../../../external/imgui
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	)
target_compile_definitions(st_gui PUBLIC
	CIMGUI_NO_EXPORT
	)

if(XRT_BUILD_DRIVER_REMOTE)
	target_link_libraries(st_gui PRIVATE
		drv_remote
		)
endif()

if(XRT_HAVE_SDL2)
	add_library(imgui_impl_sdl STATIC
		../../../external/imgui/imgui/cimgui_sdl.cpp
		../../../external/imgui/imgui/imgui_impl_sdl.cpp
		../../../external/imgui/imgui/imgui_impl_sdl.h
		)
	target_link_libraries(imgui_impl_sdl PRIVATE
		${SDL2_LIBRARIES}
		)
	target_include_directories(imgui_impl_sdl
		PRIVATE
		${SDL2_INCLUDE_DIRS}
		PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/../../../external/imgui
		)
endif()
