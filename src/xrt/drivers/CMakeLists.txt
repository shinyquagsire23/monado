# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


set(ENABLED_HEADSET_DRIVERS)
set(ENABLED_DRIVERS)

if(BUILD_DRIVER_DUMMY)
	set(DUMMY_SOURCE_FILES
		dummy/dummy_hmd.c
		dummy/dummy_interface.h
		dummy/dummy_prober.c
		)

	add_library(drv_dummy STATIC ${DUMMY_SOURCE_FILES})
	target_link_libraries(drv_dummy PRIVATE xrt-interfaces aux_util)
	list(APPEND ENABLED_HEADSET_DRIVERS dummy)
endif()

if(BUILD_DRIVER_HDK)
	set(HDK_SOURCE_FILES
		hdk/hdk_device.cpp
		hdk/hdk_device.h
		hdk/hdk_interface.h
		hdk/hdk_prober.c
		)

	add_library(drv_hdk STATIC ${HDK_SOURCE_FILES})
	target_link_libraries(drv_hdk PRIVATE xrt-interfaces aux_math)
	list(APPEND ENABLED_HEADSET_DRIVERS hdk)
endif()


if(BUILD_DRIVER_HYDRA)
	set(HYDRA_SOURCE_FILES
		hydra/hydra_driver.c
		hydra/hydra_interface.h
		)

	add_library(drv_hydra STATIC ${HYDRA_SOURCE_FILES})
	target_link_libraries(drv_hydra PRIVATE xrt-interfaces aux_os)
	list(APPEND ENABLED_DRIVERS hydra)
endif()

if(BUILD_DRIVER_NS)
	set(NS_SOURCE_FILES
		north_star/distortion/utility_northstar.h
		north_star/distortion/deformation_northstar.h
		north_star/distortion/deformation_northstar.cpp
		north_star/ns_hmd.h
		north_star/ns_hmd.c
		north_star/ns_interface.h
		north_star/ns_prober.c
		)

	add_library(drv_ns STATIC ${NS_SOURCE_FILES})
	target_link_libraries(drv_ns PRIVATE xrt-interfaces aux_math xrt-external-headers)
	list(APPEND ENABLED_HEADSET_DRIVERS ns)
endif()

if(BUILD_DRIVER_OHMD)
	set(OHMD_SOURCE_FILES
		ohmd/oh_device.c
		ohmd/oh_device.h
		ohmd/oh_interface.h
		ohmd/oh_prober.c
		)

	add_library(drv_ohmd STATIC ${OHMD_SOURCE_FILES})
	target_link_libraries(drv_ohmd PRIVATE xrt-interfaces OpenHMD::OpenHMD aux_util aux_math)
	list(APPEND ENABLED_HEADSET_DRIVERS openhmd)
endif()


if(BUILD_DRIVER_PSMV)
	set(PSMOVE_SOURCE_FILES
		psmv/psmv_driver.c
		psmv/psmv_interface.h
		)

	add_library(drv_psmv STATIC ${PSMOVE_SOURCE_FILES})
	target_link_libraries(drv_psmv PRIVATE xrt-interfaces PUBLIC aux_os aux_tracking)
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

	add_library(drv_psvr STATIC ${PSVR_SOURCE_FILES})
	target_link_libraries(drv_psvr PRIVATE xrt-interfaces HIDAPI::hidapi aux_util)
	list(APPEND ENABLED_HEADSET_DRIVERS psvr)
endif()

if(BUILD_DRIVER_RS)
	set(RS_SOURCE_FILES
		realsense/rs_6dof.c
		)

	add_library(drv_rs STATIC ${RS_SOURCE_FILES})
	target_link_libraries(drv_rs PRIVATE xrt-interfaces realsense2::realsense2 aux_util)
	list(APPEND ENABLED_HEADSET_DRIVERS rs)
endif()

if(BUILD_DRIVER_VIVE)
	set(VIVE_SOURCE_FILES
		vive/vive_device.h
		vive/vive_device.c
		vive/vive_prober.h
		vive/vive_prober.c
		vive/vive_protocol.h
		)

	add_library(drv_vive STATIC ${VIVE_SOURCE_FILES})
	target_link_libraries(drv_vive PRIVATE xrt-interfaces aux_os aux_util aux_math)
	target_link_libraries(drv_vive PRIVATE ${ZLIB_LIBRARIES})
	target_include_directories(drv_vive PRIVATE ${ZLIB_INCLUDE_DIRS})
	list(APPEND ENABLED_HEADSET_DRIVERS vive)
endif()

if(BUILD_DRIVER_V4L2)
	set(V4L2_SOURCE_FILES
		v4l2/v4l2_driver.c
		)

	add_library(drv_v4l2 STATIC ${V4L2_SOURCE_FILES})
	target_link_libraries(drv_v4l2 PRIVATE xrt-interfaces aux_os)
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
