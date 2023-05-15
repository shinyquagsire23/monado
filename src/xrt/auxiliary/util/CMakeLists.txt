# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

####
# Main util library
#
# Avoid linking with libraries that brings in DSOs as this
# library is used everywhere. Including libraries loaded in
# by applications like the OpenXR runtime library.
#

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/u_git_tag.c.in" "${CMAKE_CURRENT_BINARY_DIR}/u_git_tag.c"
	@ONLY
	)

add_library(
	aux_util STATIC
	u_autoexpgain.c
	u_autoexpgain.h
	u_bitwise.c
	u_bitwise.h
	u_builders.c
	u_builders.h
	u_debug.c
	u_debug.h
	u_deque.cpp
	u_deque.h
	u_device.c
	u_device.h
	u_distortion.c
	u_distortion.h
	u_distortion_mesh.c
	u_distortion_mesh.h
	u_documentation.h
	u_file.c
	u_file.cpp
	u_file.h
	u_format.c
	u_format.h
	u_frame.c
	u_frame.h
	u_generic_callbacks.hpp
	u_git_tag.h
	u_hand_tracking.c
	u_hand_tracking.h
	u_hand_simulation.c
	u_hand_simulation.h
	u_handles.c
	u_handles.h
	u_hashmap.cpp
	u_hashmap.h
	u_hashset.cpp
	u_hashset.h
	u_id_ringbuffer.cpp
	u_id_ringbuffer.h
	u_imu_sink_split.c
	u_imu_sink_force_monotonic.c
	u_json.c
	u_json.h
	u_json.hpp
	u_logging.c
	u_logging.h
	u_metrics.c
	u_metrics.h
	u_misc.c
	u_misc.h
	u_pacing.h
	u_pacing_app.c
	u_pacing_compositor.c
	u_pacing_compositor_fake.c
	u_pretty_print.c
	u_pretty_print.h
	u_prober.c
	u_prober.h
	u_space_overseer.c
	u_space_overseer.h
	u_string_list.cpp
	u_string_list.h
	u_string_list.hpp
	u_system_helpers.c
	u_system_helpers.h
	u_template_historybuf.hpp
	u_time.cpp
	u_time.h
	u_trace_marker.c
	u_trace_marker.h
	u_tracked_imu_3dof.c
	u_tracked_imu_3dof.h
	u_var.cpp
	u_var.h
	u_vector.cpp
	u_vector.h
	u_config_json.c
	u_config_json.h
	u_verify.h
	u_win32_com_guard.cpp
	u_win32_com_guard.hpp
	u_worker.c
	u_worker.cpp
	u_worker.h
	u_worker.hpp
	"${CMAKE_CURRENT_BINARY_DIR}/u_git_tag.c"
	)
target_link_libraries(
	aux_util
	PUBLIC
		xrt-external-nanopb
		aux-includes
		aux_generated_bindings
		aux_os
		aux_math
	)

# Only uses normal Windows libraries, doesn't add anything extra.
if(WIN32)
	target_sources(aux_util PRIVATE u_windows.c u_windows.h)
endif()

# Is basically used everywhere, unavoidable.
if(XRT_HAVE_SYSTEM_CJSON)
	target_link_libraries(aux_util PUBLIC cJSON::cJSON)
	target_compile_definitions(aux_util PRIVATE XRT_HAVE_SYSTEM_CJSON)
else()
	target_link_libraries(aux_util PUBLIC xrt-external-cjson)
endif()

# Is basically used everywhere, only used in debugging.
if(XRT_FEATURE_TRACING AND XRT_HAVE_PERCETTO)
	target_link_libraries(aux_util PUBLIC percetto::percetto)
endif()

# Is basically used everywhere, only used in debugging.
if(XRT_FEATURE_TRACING AND XRT_HAVE_TRACY)
	target_link_libraries(aux_util PUBLIC xrt-external-tracy)
endif()

# Is basically used everywhere, so link with here.
if(ANDROID)
	target_link_libraries(aux_util PUBLIC ${ANDROID_LOG_LIBRARY})
endif()

# Internal dependency and doesn't bring in any DSO.
target_include_directories(aux_util PRIVATE ${EIGEN3_INCLUDE_DIR})

####
# Debug UI library
#

add_library(aux_util_debug_gui STATIC u_debug_gui.c u_debug_gui.h) # Always built, for stubs.
target_link_libraries(
	aux_util_debug_gui
	INTERFACE xrt-interfaces
	PRIVATE aux-includes
	)

if(XRT_FEATURE_DEBUG_GUI)
	target_link_libraries(aux_util_debug_gui PRIVATE aux_util)

	if(XRT_HAVE_OPENGL)
		target_link_libraries(aux_util_debug_gui PUBLIC aux_ogl)
	endif()

	if(XRT_HAVE_SDL2)
		target_link_libraries(
			aux_util_debug_gui PRIVATE st_gui xrt-external-imgui-sdl2 SDL2::SDL2
			)

		if(XRT_BUILD_DRIVER_QWERTY)
			target_link_libraries(
				aux_util_debug_gui PRIVATE drv_qwerty drv_qwerty_includes
				)
		endif()
	endif()
endif()

####
# Sink library
#

add_library(
	aux_util_sink STATIC
	u_sink.h
	u_sink_combiner.c
	u_sink_force_genlock.c
	u_sink_converter.c
	u_sink_deinterleaver.c
	u_sink_queue.c
	u_sink_simple_queue.c
	u_sink_quirk.c
	u_sink_split.c
	u_sink_stereo_sbs_to_slam_sbs.c
	)
target_link_libraries(
	aux_util_sink
	PUBLIC
		aux-includes
		xrt-pthreads
		aux_os
		aux_math
		aux_util
	)

if(XRT_HAVE_JPEG)
	target_link_libraries(aux_util_sink PRIVATE ${JPEG_LIBRARIES})
	target_include_directories(aux_util_sink PRIVATE ${JPEG_INCLUDE_DIRS})
endif()

####
# Process library
#

add_library(aux_util_process STATIC u_process.c u_process.h)
target_link_libraries(aux_util_process PUBLIC aux-includes aux_util)

if(XRT_HAVE_LIBBSD)
	target_include_directories(aux_util_process SYSTEM PRIVATE ${LIBBSD_INCLUDE_DIRS})
	target_link_libraries(aux_util_process PUBLIC ${LIBBSD_LIBRARIES})
endif()
