# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(MATH_SOURCE_FILES
	math/m_api.h
	math/m_base.cpp
	math/m_eigen_interop.h
	math/m_hash.cpp
	math/m_optics.c
	math/m_quatexpmap.cpp
	)

set(OGL_SOURCE_FILES
	ogl/ogl_api.c
	ogl/ogl_api.h
	ogl/ogl_documentation.h
	)

set(OS_SOURCE_FILES
	os/os_documentation.h
	os/os_hid.h
	os/os_hid_hidraw.c
	os/os_threading.h
	)

set(TRACKING_SOURCE_FILES
	tracking/t_calibration.cpp
	tracking/t_calibration_opencv.h
	tracking/t_convert.cpp
	tracking/t_debug_hsv_filter.cpp
	tracking/t_debug_hsv_picker.cpp
	tracking/t_debug_hsv_viewer.cpp
	tracking/t_file.cpp
	tracking/t_fusion.h
	tracking/t_hsv_filter.c
	tracking/t_imu_fusion.h
	tracking/t_imu.cpp
	tracking/t_imu.h
	tracking/t_tracker_psmv.cpp
	tracking/t_tracker_psvr.cpp
	tracking/t_tracking.h
	)

set(UTIL_SOURCE_FILES
	util/u_misc.c
	util/u_misc.h
	util/u_debug.c
	util/u_debug.h
	util/u_device.c
	util/u_device.h
	util/u_documentation.h
	util/u_format.c
	util/u_format.h
	util/u_frame.c
	util/u_frame.h
	util/u_hashmap.cpp
	util/u_hashmap.h
	util/u_hashset.cpp
	util/u_hashset.h
	util/u_sink.h
	util/u_sink_converter.c
	util/u_sink_queue.c
	util/u_sink_split.c
	util/u_time.cpp
	util/u_time.h
	util/u_var.cpp
	util/u_var.h
	)

# Common includes
include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${CMAKE_CURRENT_SOURCE_DIR}
	)

# OpenGL library.
# Use OBJECT to not create a archive, since it just gets in the way.
add_library(aux_ogl OBJECT ${OGL_SOURCE_FILES})
set_property(TARGET aux_ogl PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(aux_ogl SYSTEM
		PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../../external
		)

# Util library.
# Use OBJECT to not create a archive, since it just gets in the way.
add_library(aux_util OBJECT ${UTIL_SOURCE_FILES})
set_property(TARGET aux_util PROPERTY POSITION_INDEPENDENT_CODE ON)

# OS library.
add_library(aux_os OBJECT ${OS_SOURCE_FILES})
set_property(TARGET aux_os PROPERTY POSITION_INDEPENDENT_CODE ON)

# Math library.
# Use OBJECT to not create a archive, since it just gets in the way.
add_library(aux_math OBJECT ${MATH_SOURCE_FILES})
set_property(TARGET aux_math PROPERTY POSITION_INDEPENDENT_CODE ON)

# Math files has extra include(s).
target_include_directories(aux_math SYSTEM
	PRIVATE ${EIGEN3_INCLUDE_DIR}
	)


if(BUILD_TRACKING)
	# Tracking library.
	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(aux_tracking OBJECT ${TRACKING_SOURCE_FILES})
	set_property(TARGET aux_tracking PROPERTY POSITION_INDEPENDENT_CODE ON)

	# Tracking files have extra includes.
	target_include_directories(aux_tracking SYSTEM
		PRIVATE
		${EIGEN3_INCLUDE_DIR}
		${OpenCV_INCLUDE_DIRS}
		${CMAKE_CURRENT_SOURCE_DIR}/../../external
		)
endif()
