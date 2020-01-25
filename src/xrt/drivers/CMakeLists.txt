# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${CMAKE_CURRENT_SOURCE_DIR}/../auxiliary
	)


set(ENABLED_HEADSET_DRIVERS)
set(ENABLED_DRIVERS)

if(BUILD_DRIVER_DUMMY)
	set(DUMMY_SOURCE_FILES
		dummy/dummy_hmd.c
		dummy/dummy_interface.h
		dummy/dummy_prober.c
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_dummy OBJECT ${DUMMY_SOURCE_FILES})
	list(APPEND ENABLED_HEADSET_DRIVERS dummy)
endif()

if(BUILD_DRIVER_HDK)
	set(HDK_SOURCE_FILES
		hdk/hdk_device.cpp
		hdk/hdk_device.h
		hdk/hdk_interface.h
		hdk/hdk_prober.c
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_hdk OBJECT ${HDK_SOURCE_FILES})
	list(APPEND ENABLED_HEADSET_DRIVERS hdk)
endif()


if(BUILD_DRIVER_HYDRA)
	set(HYDRA_SOURCE_FILES
		hydra/hydra_driver.c
		hydra/hydra_interface.h
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_hydra OBJECT ${HYDRA_SOURCE_FILES})
	list(APPEND ENABLED_DRIVERS hydra)
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
	target_include_directories(drv_ohmd SYSTEM
		PRIVATE ${OPENHMD_INCLUDE_DIRS}
		)
	list(APPEND ENABLED_HEADSET_DRIVERS openhmd)
endif()


if(BUILD_DRIVER_PSMV)
	set(PSMOVE_SOURCE_FILES
		psmv/psmv_driver.c
		psmv/psmv_interface.h
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_psmv OBJECT ${PSMOVE_SOURCE_FILES})
	list(APPEND ENABLED_DRIVERS psmv)
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
	target_include_directories(drv_psvr SYSTEM
		PRIVATE ${HIDAPI_INCLUDE_DIRS}
		)
	list(APPEND ENABLED_HEADSET_DRIVERS psvr)
endif()

if(BUILD_DRIVER_VIVE)
	set(VIVE_SOURCE_FILES
		vive/vive_device.h
		vive/vive_device.c
		vive/vive_prober.h
		vive/vive_prober.c
		vive/vive_protocol.h
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_vive OBJECT ${VIVE_SOURCE_FILES})
	target_include_directories(drv_vive SYSTEM PRIVATE ../../external)
	target_link_libraries(drv_vive z)
	list(APPEND ENABLED_HEADSET_DRIVERS vive)
endif()

if(BUILD_DRIVER_V4L2)
	set(V4L2_SOURCE_FILES
		v4l2/v4l2_driver.c
		)

	# Use OBJECT to not create a archive, since it just gets in the way.
	add_library(drv_v4l2 OBJECT ${V4L2_SOURCE_FILES})
	list(APPEND ENABLED_DRIVERS v4l2)
endif()

if(ENABLED_HEADSET_DRIVERS)
	set(ENABLED_DRIVERS ${ENABLED_HEADSET_DRIVERS} ${ENABLED_DRIVERS})
	list(SORT ENABLED_DRIVERS)
	string(REPLACE ";" " " ENABLED_DRIVERS "${ENABLED_DRIVERS}")
	message(STATUS "Enabled drivers: ${ENABLED_DRIVERS}")
else()
	message(FATAL_ERROR "You must enable at least one headset driver to build Monado.")
endif()
