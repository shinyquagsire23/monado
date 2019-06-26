# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a cli interface for Monado.

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../targets/common
	${CMAKE_CURRENT_SOURCE_DIR}/../../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	)

set(SOURCE_FILES
	cli_cmd_calibrate.c
	cli_cmd_test.c
	cli_common.h
	cli_main.c
	cli_prober.c
	)

add_executable(prober
	${SOURCE_FILES}
	$<TARGET_OBJECTS:aux_os>
	$<TARGET_OBJECTS:aux_util>
	$<TARGET_OBJECTS:aux_math>
	$<TARGET_OBJECTS:st_prober>
	$<TARGET_OBJECTS:target_lists>
	)

set_target_properties(prober PROPERTIES
	OUTPUT_NAME monado-cli
	PREFIX ""
	)

target_link_libraries(prober PRIVATE
	${LIBUSB_LIBRARIES}
	${LIBUVC_LIBRARIES}
	${UDEV_LIBRARIES}
	)

if(DRIVER_OBJECTS)
	target_sources(prober PRIVATE ${DRIVER_OBJECTS})
endif()

if(DRIVER_LIBRARIES)
	target_link_libraries(prober PRIVATE ${DRIVER_LIBRARIES})
endif()
