# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

####
# Lists
#

add_library(
	target_lists STATIC target_builder_interface.h target_builder_legacy.c target_lists.c
			    target_lists.h
	)
target_link_libraries(
	target_lists
	PRIVATE
		xrt-interfaces
		aux_util # TODO Remove this after removing #include "util/u_time.h" from xrt_defines.h
		drv_includes
	)
target_include_directories(target_lists PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

###
# Builders
#
if(XRT_BUILD_DRIVER_REMOTE)
	target_sources(target_lists PRIVATE target_builder_remote.c)
endif()

if(XRT_BUILD_DRIVER_PSVR OR XRT_BUILD_DRIVER_PSMV)
	target_sources(target_lists PRIVATE target_builder_rgb_tracking.c)
endif()

if(XRT_BUILD_DRIVER_SURVIVE OR XRT_BUILD_DRIVER_VIVE)
	target_sources(target_lists PRIVATE target_builder_lighthouse.c)
endif()

if(XRT_BUILD_DRIVER_SIMULAVR)
	target_sources(target_lists PRIVATE target_builder_simulavr.c)
endif()

if(XRT_BUILD_DRIVER_NS)
	target_sources(target_lists PRIVATE target_builder_north_star.c)
	target_link_libraries(target_lists PRIVATE drv_ns)
endif()

###
# Drivers
#

if(XRT_BUILD_DRIVER_ARDUINO)
	target_link_libraries(target_lists PRIVATE drv_arduino)
endif()

target_link_libraries(target_lists PRIVATE drv_cemu)

if(XRT_BUILD_DRIVER_DAYDREAM)
	target_link_libraries(target_lists PRIVATE drv_daydream)
endif()

if(XRT_BUILD_DRIVER_DEPTHAI)
	target_link_libraries(target_lists PRIVATE drv_depthai)
endif()

if(XRT_BUILD_DRIVER_SIMULATED)
	target_link_libraries(target_lists PRIVATE drv_simulated)
endif()

if(XRT_BUILD_DRIVER_HDK)
	target_link_libraries(target_lists PRIVATE drv_hdk)
endif()

if(XRT_BUILD_DRIVER_HYDRA)
	target_link_libraries(target_lists PRIVATE drv_hydra)
endif()

if(XRT_BUILD_DRIVER_NS)
	target_link_libraries(target_lists PRIVATE drv_ns)
endif()

if(XRT_BUILD_DRIVER_TWRAP)
	target_link_libraries(target_lists PRIVATE drv_twrap)
endif()

if(XRT_BUILD_DRIVER_ULV2)
	target_link_libraries(target_lists PRIVATE drv_ulv2)
endif()

if(XRT_BUILD_DRIVER_OHMD)
	target_link_libraries(target_lists PRIVATE drv_ohmd)
endif()

if(XRT_BUILD_DRIVER_OPENGLOVES)
	target_link_libraries(target_lists PRIVATE drv_opengloves)
endif()

if(XRT_BUILD_DRIVER_HANDTRACKING)
	target_link_libraries(target_lists PRIVATE drv_ht)
endif()

if(XRT_BUILD_DRIVER_PSMV)
	target_link_libraries(target_lists PRIVATE drv_psmv)
endif()

if(XRT_BUILD_DRIVER_PSVR)
	target_link_libraries(target_lists PRIVATE drv_psvr)
endif()

if(XRT_BUILD_DRIVER_REALSENSE)
	target_link_libraries(target_lists PRIVATE drv_realsense)
endif()

if(XRT_BUILD_DRIVER_REMOTE)
	target_link_libraries(target_lists PRIVATE drv_remote)
endif()

if(XRT_HAVE_V4L2)
	target_link_libraries(target_lists PRIVATE drv_v4l2)
endif()

if(XRT_BUILD_DRIVER_VF)
	target_link_libraries(target_lists PRIVATE drv_vf)
endif()

if(XRT_BUILD_DRIVER_VIVE)
	target_link_libraries(target_lists PRIVATE drv_vive)
endif()

if(XRT_BUILD_DRIVER_SURVIVE)
	target_link_libraries(target_lists PRIVATE drv_survive)
endif()

if(XRT_BUILD_DRIVER_ANDROID)
	target_link_libraries(target_lists PRIVATE drv_android)
endif()

if(XRT_BUILD_DRIVER_ILLIXR)
	target_link_libraries(target_lists PRIVATE drv_illixr)
endif()

target_link_libraries(target_lists PRIVATE drv_multi)

if(XRT_BUILD_DRIVER_QWERTY)
	target_link_libraries(target_lists PRIVATE drv_qwerty)
endif()

if(XRT_BUILD_DRIVER_WMR)
	target_link_libraries(target_lists PRIVATE drv_wmr)
endif()

if(XRT_BUILD_DRIVER_EUROC)
	target_link_libraries(target_lists PRIVATE drv_euroc)
endif()

if(XRT_BUILD_DRIVER_SIMULAVR)
	target_link_libraries(target_lists PRIVATE drv_svr)
endif()

####
# Instance
#

if(XRT_FEATURE_COMPOSITOR_MAIN OR XRT_FEATURE_COMPOSITOR_NULL)
	add_library(target_instance STATIC target_instance.c)
	target_link_libraries(
		target_instance
		PRIVATE
			xrt-interfaces
			aux_util
			st_prober
			target_lists
			drv_includes
		)
	target_include_directories(target_instance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

	if(XRT_FEATURE_COMPOSITOR_MAIN)
		target_link_libraries(target_instance PRIVATE comp_main)
	endif()

	if(XRT_FEATURE_COMPOSITOR_NULL)
		target_link_libraries(target_instance PRIVATE comp_null)
	endif()
endif()

####
# Instance without Compositor
#

add_library(target_instance_no_comp STATIC target_instance_no_comp.c)
target_link_libraries(
	target_instance_no_comp
	PRIVATE
		xrt-interfaces
		aux_util
		st_prober
		target_lists
		drv_includes
	)
target_include_directories(target_instance_no_comp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
