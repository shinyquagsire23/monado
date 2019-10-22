# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a small SDL2 based GUI for Monado.

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../targets/common
	${CMAKE_CURRENT_SOURCE_DIR}/../../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	${CMAKE_CURRENT_SOURCE_DIR}/../../state_trackers
	${CMAKE_CURRENT_SOURCE_DIR}/../../../external
	${SDL2_INCLUDE_DIRS}
	)

set(SOURCE_FILES
	gui_sdl2.c
	gui_sdl2_imgui.c
	gui_sdl2_main.c
	gui_sdl2_prober.c
	../../../external/imgui/cimgui_sdl.cpp
	../../../external/imgui/imgui_impl_sdl.cpp
	../../../external/imgui/imgui_impl_sdl.h
	)

add_executable(gui
	${SOURCE_FILES}
	$<TARGET_OBJECTS:aux_os>
	$<TARGET_OBJECTS:aux_ogl>
	$<TARGET_OBJECTS:aux_util>
	$<TARGET_OBJECTS:aux_math>
	$<TARGET_OBJECTS:st_gui>
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

if(DRIVER_OBJECTS)
	target_sources(gui PRIVATE ${DRIVER_OBJECTS})
endif()

if(BUILD_WITH_JPEG)
	target_link_libraries(gui PRIVATE ${JPEG_LIBRARIES})
endif()

if(DRIVER_LIBRARIES)
	target_link_libraries(gui PRIVATE ${DRIVER_LIBRARIES})
endif()
