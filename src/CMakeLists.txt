# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


if(BUILD_TRACKING)
	set(XRT_BUILD_TRACKING TRUE)
endif()

if(BUILD_DRIVER_DUMMY)
	set(XRT_BUILD_DRIVER_DUMMY TRUE)
endif()

if(BUILD_DRIVER_HDK)
	set(XRT_BUILD_DRIVER_HDK TRUE)
endif()

if(BUILD_DRIVER_HYDRA)
	set(XRT_BUILD_DRIVER_HYDRA TRUE)
endif()

if(BUILD_DRIVER_OHMD)
	set(XRT_BUILD_DRIVER_OHMD TRUE)
endif()

if(BUILD_DRIVER_PSMV)
	set(XRT_BUILD_DRIVER_PSMV TRUE)
endif()

if(BUILD_DRIVER_PSVR)
	set(XRT_BUILD_DRIVER_PSVR TRUE)
endif()

if(BUILD_DRIVER_V4L2)
	set(XRT_BUILD_DRIVER_V4L2 TRUE)
endif()

if(BUILD_DRIVER_VIVE)
	set(XRT_BUILD_DRIVER_VIVE TRUE)
endif()

configure_file(targets_enabled_drivers.h.cmake_in ${CMAKE_CURRENT_BINARY_DIR}/targets_enabled_drivers.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(xrt)
