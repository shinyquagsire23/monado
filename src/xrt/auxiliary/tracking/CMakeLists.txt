# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	aux_tracking STATIC
	t_data_utils.c
	t_imu_fusion.hpp
	t_imu.cpp
	t_imu.h
	t_tracking.h
	)
target_link_libraries(
	aux_tracking
	PUBLIC aux-includes xrt-external-cjson
	PRIVATE aux_math aux_util xrt-external-flexkalman xrt-external-hungarian
	)
target_include_directories(aux_tracking SYSTEM PRIVATE ${EIGEN3_INCLUDE_DIR})

if(XRT_HAVE_OPENCV)
	target_sources(
		aux_tracking
		PRIVATE
			t_calibration_opencv.hpp
			t_calibration.cpp
			t_convert.cpp
			t_debug_hsv_filter.cpp
			t_debug_hsv_picker.cpp
			t_debug_hsv_viewer.cpp
			t_euroc_recorder.cpp
			t_euroc_recorder.h
			t_file.cpp
			t_frame_cv_mat_wrapper.cpp
			t_frame_cv_mat_wrapper.hpp
			t_fusion.hpp
			t_helper_debug_sink.hpp
			t_hsv_filter.c
			t_kalman.cpp
			t_tracker_psmv_fusion.hpp
			t_tracker_psmv.cpp
			t_tracker_psvr.cpp
		)
	target_include_directories(aux_tracking SYSTEM PRIVATE ${OpenCV_INCLUDE_DIRS})
	target_link_libraries(aux_tracking PUBLIC ${OpenCV_LIBRARIES})
endif()

if(XRT_FEATURE_SLAM)
	target_sources(aux_tracking PRIVATE t_tracker_slam.cpp)
	target_link_libraries(aux_tracking PRIVATE xrt-external-slam)
endif()
