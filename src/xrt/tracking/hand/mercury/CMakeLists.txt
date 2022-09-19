# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Mercury hand tracking library!

add_subdirectory(kine_lm)
add_subdirectory(kine_ccdik)

add_library(t_ht_mercury_model STATIC hg_model.cpp)

target_link_libraries(t_ht_mercury_model PRIVATE aux_math aux_tracking aux_os aux_util)

target_include_directories(
	t_ht_mercury_model SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS} ${EIGEN3_INCLUDE_DIR}
	)

target_link_libraries(
	t_ht_mercury_model
	PRIVATE
		aux_math
		aux_tracking
		aux_os
		aux_util
		${OpenCV_LIBRARIES}
		ONNXRuntime::ONNXRuntime
	)

add_library(t_ht_mercury STATIC hg_sync.cpp hg_sync.hpp hg_interface.h kine_common.hpp)

target_link_libraries(
	t_ht_mercury
	PUBLIC aux-includes xrt-external-cjson
	PRIVATE
		aux_math
		aux_tracking
		aux_os
		aux_util
		ONNXRuntime::ONNXRuntime
		t_ht_mercury_kine_lm
		t_ht_mercury_kine_ccdik
		t_ht_mercury_model
		# ncnn # Soon...
		${OpenCV_LIBRARIES}
	)

if(XRT_HAVE_OPENCV)
	target_include_directories(
		t_ht_mercury SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS} ${EIGEN3_INCLUDE_DIR}
		)
	target_link_libraries(t_ht_mercury PUBLIC ${OpenCV_LIBRARIES})
endif()

# Below is entirely just so that tests can find us.
add_library(t_ht_mercury_includes INTERFACE)

target_include_directories(
	t_ht_mercury_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
	)
