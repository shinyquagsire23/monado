# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a small SDL2 based GUI for Monado.

add_executable(gui gui_sdl2.c gui_sdl2_imgui.c gui_sdl2_main.c)

set_target_properties(gui PROPERTIES OUTPUT_NAME monado-gui PREFIX "")
add_sanitizers(gui)

target_link_libraries(
	gui
	PRIVATE
		aux_os
		aux_ogl
		aux_util
		aux_math
		st_gui
		target_instance_no_comp
		xrt-external-imgui-sdl2
	)
if(WIN32)
	target_link_libraries(gui PRIVATE SDL2::SDL2main)
endif()

install(TARGETS gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
