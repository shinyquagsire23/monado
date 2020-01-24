# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# This is where we collect all of the pieces from the different parts of
# the source tree and build a complete driver or integration part.


# Set up these two lists for use across multiple targets.
set(DRIVER_OBJECTS)
set(DRIVER_LIBRARIES)

list(APPEND DRIVER_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

if(BUILD_WITH_LIBUSB)
	list(APPEND DRIVER_LIBRARIES ${LIBUSB1_LIBRARIES})
endif()

if(BUILD_WITH_JPEG)
	list(APPEND DRIVER_LIBRARIES ${JPEG_LIBRARIES})
endif()

if(BUILD_DRIVER_DUMMY)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_dummy>)
endif()

if(BUILD_DRIVER_HDK)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_hdk>)
	list(APPEND DRIVER_LIBRARIES ${HIDAPI_LIBRARIES})
endif()

if(BUILD_DRIVER_HYDRA)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_hydra>)
endif()

if(BUILD_DRIVER_NS)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_ns>)
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

if(BUILD_DRIVER_V4L2)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_v4l2>)
endif()

if(BUILD_DRIVER_VIVE)
	list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:drv_vive>)
	list(APPEND DRIVER_LIBRARIES ${ZLIB_LIBRARIES})
endif()

list(APPEND DRIVER_OBJECTS $<TARGET_OBJECTS:aux_tracking>)

if(BUILD_TRACKING)
	list(APPEND DRIVER_LIBRARIES ${OpenCV_LIBRARIES})
endif()

add_subdirectory(common)
add_subdirectory(openxr)
add_subdirectory(cli)

if(BUILD_TARGET_GUI)
	add_subdirectory(gui)
endif()
