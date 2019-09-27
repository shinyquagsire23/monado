# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# This is where we collect all of the pieces from the different parts of
# the source tree and build a complete driver or integration part.


# Set up these two lists for use across multiple targets.
set(DRIVER_OBJECTS)
set(DRIVER_LIBRARIES)

if(BUILD_WITH_LIBUSB)
	list(APPEND DRIVER_LIBRARIES ${LIBUSB1_LIBRARIES})
endif()

if(BUILD_WITH_JPEG)
	list(APPEND DRIVER_LIBRARIES ${JPEG_LIBRARIES})
endif()

if(BUILD_DRIVER_HDK)
	set(XRT_BUILD_DRIVER_HDK TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_hdk>)
	list(APPEND DRIVER_LIBRARIES ${HIDAPI_LIBRARIES})
endif()

if(BUILD_DRIVER_HYDRA)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_hydra>)
endif()

if(BUILD_DRIVER_OHMD)
	set(XRT_BUILD_DRIVER_OHMD TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_ohmd>)
	list(APPEND DRIVER_LIBRARIES OpenHMD::OpenHMD)
endif()

if(BUILD_DRIVER_PSMV)
	set(XRT_BUILD_DRIVER_PSMV TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_psmv>)
endif()

if(BUILD_DRIVER_PSVR)
	set(XRT_BUILD_DRIVER_PSVR TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_psvr>)
	list(APPEND DRIVER_LIBRARIES ${HIDAPI_LIBRARIES})
endif()

if(BUILD_DRIVER_V4L2)
	set(XRT_BUILD_DRIVER_V4L2 TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_v4l2>)
endif()

if(BUILD_TRACKING)
	set(XRT_BUILD_DRIVER_TRACKING TRUE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:aux_tracking>)
	list(APPEND DRIVER_LIBRARIES ${OpenCV_LIBRARIES})
endif()

add_subdirectory(common)
add_subdirectory(openxr)
add_subdirectory(cli)

if(BUILD_TARGET_GUI)
	add_subdirectory(gui)
endif()
