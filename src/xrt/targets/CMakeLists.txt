# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# This is where we collect all of the pieces from the different parts of
# the source tree and build a complete driver or integration part.


# Set up these two lists for use across multiple targets.
set(DRIVER_OBJECTS)
set(DRIVER_LIBRARIES)

if(BUILD_DRIVER_HDK)
	set(XRT_BUILD_HDK TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_hdk>)
	list(APPEND DRIVER_LIBRARIES ${HIDAPI_LIBRARIES})
endif()

if(BUILD_DRIVER_OHMD)
	set(XRT_BUILD_OHMD TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_ohmd>)
	list(APPEND DRIVER_LIBRARIES OpenHMD::OpenHMD)
endif()

if(BUILD_DRIVER_PSMV)
	set(XRT_BUILD_PSMV TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_psmv>)
endif()

if(BUILD_DRIVER_PSVR)
	set(XRT_BUILD_PSVR TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_psvr>)
	list(APPEND DRIVER_LIBRARIES ${HIDAPI_LIBRARIES})
endif()

configure_file(targets_enabled_drivers.h.cmake_in ${CMAKE_CURRENT_BINARY_DIR}/targets_enabled_drivers.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(common)
add_subdirectory(openxr)
add_subdirectory(cli)
