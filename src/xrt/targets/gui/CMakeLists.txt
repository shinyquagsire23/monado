# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a small SDL2 based GUI for Monado.

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../targets/common
	${CMAKE_CURRENT_SOURCE_DIR}/../../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	${CMAKE_CURRENT_SOURCE_DIR}/../../../external
	${SDL2_INCLUDE_DIRS}
	)

set(SOURCE_FILES
	gui_common.h
	gui_imgui.c
	gui_imgui.h
	gui_main.c
	gui_ogl.c
	gui_prober.c
	gui_scene.cpp
	gui_scene_debug.c
	gui_scene_video.c
	gui_sdl2.c
	../../../external/glad/gl.h
	../../../external/glad/gl.c
	../../../external/imgui/cimgui.cpp
	../../../external/imgui/cimgui.h
	../../../external/imgui/imconfig.h
	../../../external/imgui/imgui.cpp
	../../../external/imgui/imgui.h
	../../../external/imgui/imgui_demo.cpp
	../../../external/imgui/imgui_draw.cpp
	../../../external/imgui/imgui_impl_opengl3.cpp
	../../../external/imgui/imgui_impl_opengl3.h
	../../../external/imgui/imgui_impl_sdl.cpp
	../../../external/imgui/imgui_impl_sdl.h
	../../../external/imgui/imgui_internal.h
	../../../external/imgui/imgui_widgets.cpp
	../../../external/imgui/imstb_rectpack.h
	../../../external/imgui/imstb_textedit.h
	../../../external/imgui/imstb_truetype.h
	)

add_executable(gui
	${SOURCE_FILES}
	$<TARGET_OBJECTS:aux_os>
	$<TARGET_OBJECTS:aux_util>
	$<TARGET_OBJECTS:aux_math>
	$<TARGET_OBJECTS:st_prober>
	$<TARGET_OBJECTS:target_lists>
	)

set_target_properties(gui PROPERTIES
	OUTPUT_NAME monado-gui
	PREFIX ""
	)

target_link_libraries(gui PRIVATE
	${LIBUSB_LIBRARIES}
	${LIBUVC_LIBRARIES}
	${UDEV_LIBRARIES}
	${SDL2_LIBRARIES}
	)

if(BUILD_TRACKING)
	target_link_libraries(gui PRIVATE
		$<TARGET_OBJECTS:aux_tracking>
		${OpenCV_LIBRARIES}
		)
endif()

if(DRIVER_OBJECTS)
	target_sources(gui PRIVATE ${DRIVER_OBJECTS})
endif()

if(BUILD_WITH_JPEG)
	target_link_libraries(gui PRIVATE ${JPEG_LIBRARIES})
endif()

if(DRIVER_LIBRARIES)
	target_link_libraries(gui PRIVATE ${DRIVER_LIBRARIES})
endif()
