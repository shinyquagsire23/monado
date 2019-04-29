# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${CMAKE_CURRENT_SOURCE_DIR}/../auxiliary
	)


if(BUILD_DRIVER_HDK)
	set(HDK_SOURCE_FILES
		hdk/hdk_device.cpp
		hdk/hdk_device.h
		hdk/hdk_interface.h
		hdk/hdk_prober.c
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_hdk OBJECT ${HDK_SOURCE_FILES})
	set_property(TARGET drv_hdk PROPERTY POSITION_INDEPENDENT_CODE ON)
	target_include_directories(drv_hdk SYSTEM
		PRIVATE ${HIDAPI_INCLUDE_DIRS}
		)
endif()


if(BUILD_DRIVER_OHMD)
	set(OHMD_SOURCE_FILES
		ohmd/oh_device.c
		ohmd/oh_device.h
		ohmd/oh_interface.h
		ohmd/oh_prober.c
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_ohmd OBJECT ${OHMD_SOURCE_FILES})
	set_property(TARGET drv_ohmd PROPERTY POSITION_INDEPENDENT_CODE ON)
	target_include_directories(drv_ohmd SYSTEM
		PRIVATE ${OPENHMD_INCLUDE_DIRS}
		)
endif()
