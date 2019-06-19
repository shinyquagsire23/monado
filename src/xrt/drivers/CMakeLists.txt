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


if(BUILD_DRIVER_HYDRA)
	set(HYDRA_SOURCE_FILES
		hydra/hydra_driver.c
		hydra/hydra_interface.h
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_hydra OBJECT ${HYDRA_SOURCE_FILES})
	set_property(TARGET drv_hydra PROPERTY POSITION_INDEPENDENT_CODE ON)
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


if(BUILD_DRIVER_PSMV)
	set(PSMOVE_SOURCE_FILES
		psmv/psmv_driver.c
		psmv/psmv_interface.h
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_psmv OBJECT ${PSMOVE_SOURCE_FILES})
	set_property(TARGET drv_psmv PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()


if(BUILD_DRIVER_PSVR)
	set(PSVR_SOURCE_FILES
		psvr/psvr_device.c
		psvr/psvr_device.h
		psvr/psvr_interface.h
		psvr/psvr_packet.c
		psvr/psvr_prober.c
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_psvr OBJECT ${PSVR_SOURCE_FILES})
	set_property(TARGET drv_psvr PROPERTY POSITION_INDEPENDENT_CODE ON)
	target_include_directories(drv_psvr SYSTEM
		PRIVATE ${HIDAPI_INCLUDE_DIRS}
		)
endif()


if(BUILD_DRIVER_V4L2)
	set(V4L2_SOURCE_FILES
		v4l2/v4l2_driver.c
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_v4l2 OBJECT ${V4L2_SOURCE_FILES})
	set_property(TARGET drv_v4l2 PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()
