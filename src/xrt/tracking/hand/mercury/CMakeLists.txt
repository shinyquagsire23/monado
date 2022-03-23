# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


# Mercury hand tracking library!

add_library(
	t_ht_mercury_kine STATIC
	kine/kinematic_interface.hpp
	kine/kinematic_main.cpp
)

target_link_libraries(
	t_ht_mercury_kine 
	PRIVATE
		aux_math 
		aux_tracking
		aux_os
		aux_util
)

target_include_directories(t_ht_mercury_kine SYSTEM PRIVATE ${EIGEN3_INCLUDE_DIR})

add_library(
	t_ht_mercury STATIC
	hg_interface.h
	hg_model.hpp
	hg_sync.cpp
	hg_sync.hpp

	)
target_link_libraries(
	t_ht_mercury
	PUBLIC aux-includes xrt-external-cjson
	PRIVATE 
		aux_math 
		aux_tracking
		aux_os
		aux_util
		aux_gstreamer
		ONNXRuntime::ONNXRuntime
		t_ht_mercury_kine
		# ncnn # Soon...
		${OpenCV_LIBRARIES}
	)
if(XRT_HAVE_OPENCV)
	target_include_directories(t_ht_mercury SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS} ${EIGEN3_INCLUDE_DIR})
	target_link_libraries(t_ht_mercury PUBLIC ${OpenCV_LIBRARIES})
endif()
