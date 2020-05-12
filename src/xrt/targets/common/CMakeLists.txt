# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


####
# Lists
#

add_library(target_lists STATIC
	target_lists.c
	)
target_link_libraries(target_lists PRIVATE
	xrt-interfaces
	aux_util # TODO Remove this after removing #include "util/u_time.h" from xrt_defines.h
	)
target_include_directories(target_lists PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	)
target_include_directories(target_lists PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	)

if(BUILD_DRIVER_ARDUINO)
	target_link_libraries(target_lists PRIVATE drv_arduino)
endif()

if(BUILD_DRIVER_DAYDREAM)
	target_link_libraries(target_lists PRIVATE drv_daydream)
endif()

if(BUILD_DRIVER_DUMMY)
	target_link_libraries(target_lists PRIVATE drv_dummy)
endif()

if(BUILD_DRIVER_HDK)
	target_link_libraries(target_lists PRIVATE drv_hdk)
endif()

if(BUILD_DRIVER_HYDRA)
	target_link_libraries(target_lists PRIVATE drv_hydra)
endif()

if(BUILD_DRIVER_NS)
	target_link_libraries(target_lists PRIVATE drv_ns)
endif()

if(BUILD_DRIVER_OHMD)
	target_link_libraries(target_lists PRIVATE drv_ohmd)
endif()

if(BUILD_DRIVER_PSMV)
	target_link_libraries(target_lists PRIVATE drv_psmv)
endif()

if(BUILD_DRIVER_PSVR)
	target_link_libraries(target_lists PRIVATE drv_psvr)
endif()

if(BUILD_DRIVER_RS)
	target_link_libraries(target_lists PRIVATE drv_rs)
endif()

if(BUILD_DRIVER_V4L2)
	target_link_libraries(target_lists PRIVATE drv_v4l2)
endif()

if(BUILD_DRIVER_VIVE)
	target_link_libraries(target_lists PRIVATE drv_vive)
endif()

if(BUILD_DRIVER_SURVIVE)
	target_link_libraries(target_lists PRIVATE drv_survive)
endif()

####
# Instance
#

add_library(target_instance STATIC
	target_instance.c
	)
target_link_libraries(target_instance PRIVATE
	xrt-interfaces
	aux_util
	)
target_include_directories(target_instance PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	)
target_include_directories(target_instance PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	)
