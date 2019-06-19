# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# This is where we collect all of the pieces from the different parts of
# the source tree and build a complete driver or integration part.


# Set up these two lists for use across multiple targets.
set(DRIVER_OBJECTS)
set(DRIVER_LIBRARIES)

if(BUILD_DRIVER_HDK)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_hdk>)
	list(APPEND DRIVER_LIBRARIES ${HIDAPI_LIBRARIES})
endif()

if(BUILD_DRIVER_OHMD)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_ohmd>)
	list(APPEND DRIVER_LIBRARIES OpenHMD::OpenHMD)
endif()

if(BUILD_DRIVER_PSMV)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_psmv>)
endif()

if(BUILD_DRIVER_PSVR)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_psvr>)
	list(APPEND DRIVER_LIBRARIES ${HIDAPI_LIBRARIES})
endif()

add_subdirectory(common)
add_subdirectory(openxr)
add_subdirectory(prober)
