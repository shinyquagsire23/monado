# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(SOURCE_FILES
	target_lists.c
	)

add_library(target_lists STATIC ${SOURCE_FILES})
target_link_libraries(target_lists PRIVATE xrt-interfaces)
target_include_directories(target_lists PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers)
target_include_directories(target_lists PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

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
