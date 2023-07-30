# Copyright 2020-2023, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_executable(
	sdl-test
	sdl_compositor.c
	sdl_device.c
	sdl_hack_stubs.c
	sdl_instance.c
	sdl_internal.h
	sdl_internal.hpp
	sdl_main.c
	sdl_program.cpp
	sdl_swapchain.c
	)
add_sanitizers(sdl-test)

target_link_libraries(
	sdl-test
	PRIVATE
		xrt-external-imgui-sdl2
		aux_os
		aux_vk
		aux_ogl
		aux_util
		comp_util
		comp_multi
		ipc_server
		st_gui
		drv_includes
		drv_simulated
	)

if(WIN32)
	# For UTF-8 in sdl_program.cpp, MSVC defaults to UTF-16.
	target_compile_options(sdl-test PRIVATE "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
	target_compile_options(sdl-test PRIVATE "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

	# Need to link against SDL2main on Windows.
	target_link_libraries(sdl-test PRIVATE SDL2::SDL2main)
endif()
