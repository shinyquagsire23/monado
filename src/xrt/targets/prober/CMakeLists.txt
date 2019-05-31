# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a test executable for prober functionality.

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../targets/common
	${CMAKE_CURRENT_SOURCE_DIR}/../../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	)

set(SOURCE_FILES
	main.c
	)

# depends on above generated files
add_executable(prober
	${SOURCE_FILES}
	$<TARGET_OBJECTS:aux_os>
	$<TARGET_OBJECTS:aux_util>
	$<TARGET_OBJECTS:aux_math>
	$<TARGET_OBJECTS:st_prober>
	$<TARGET_OBJECTS:target_lists>
	)

set_target_properties(prober PROPERTIES
	OUTPUT_NAME prober
	PREFIX ""
	)

target_link_libraries(prober PRIVATE
	${LIBUSB_LIBRARIES}
	${LIBUVC_LIBRARIES}
	${UDEV_LIBRARIES}
	)

if(BUILD_DRIVER_HDK)
	target_sources(prober PRIVATE $<TARGET_OBJECTS:drv_hdk>)
	target_link_libraries(prober PUBLIC ${HIDAPI_LIBRARIES})
endif()

if(BUILD_DRIVER_OHMD)
	target_sources(prober PRIVATE $<TARGET_OBJECTS:drv_ohmd>)
	target_link_libraries(prober PRIVATE OpenHMD::OpenHMD)
endif()

if(BUILD_DRIVER_PSMV)
	target_sources(prober PRIVATE $<TARGET_OBJECTS:drv_psmv>)
endif()

if(BUILD_DRIVER_PSVR)
	target_sources(prober PRIVATE $<TARGET_OBJECTS:drv_psvr>)
	target_link_libraries(prober PUBLIC ${HIDAPI_LIBRARIES})
endif()
