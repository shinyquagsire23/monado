# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Old RGB hand tracking library.
add_library(
	t_ht_old_rgb STATIC
	rgb_hand_math.hpp
	rgb_image_math.hpp
	rgb_interface.h
	rgb_model.hpp
	rgb_nms.hpp
	rgb_sync.cpp
	rgb_sync.hpp
	)
target_link_libraries(
	t_ht_old_rgb
	PUBLIC aux-includes xrt-external-cjson
	PRIVATE
		aux_math
		aux_tracking
		aux_os
		aux_util
		ONNXRuntime::ONNXRuntime
		${OpenCV_LIBRARIES}
	)
if(XRT_HAVE_OPENCV)
	target_include_directories(
		t_ht_old_rgb SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS} ${EIGEN3_INCLUDE_DIR}
		)
	target_link_libraries(t_ht_old_rgb PUBLIC ${OpenCV_LIBRARIES})
endif()
