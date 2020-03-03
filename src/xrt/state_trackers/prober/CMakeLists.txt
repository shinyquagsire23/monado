# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(PROBER_INCLUDES)

set(PROBER_SOURCE_FILES
	p_documentation.h
	p_dump.c
	p_prober.c
	p_prober.h
	p_tracking.c
	)

# Add libudev
if(BUILD_WITH_LIBUDEV)
	list(APPEND PROBER_SOURCE_FILES
		p_udev.c
		)
endif()

# Add libusb
if(BUILD_WITH_LIBUSB)
	list(APPEND PROBER_SOURCE_FILES
		p_libusb.c
		)
endif()

# Add libuvc
if(BUILD_WITH_LIBUVC)
	list(APPEND PROBER_SOURCE_FILES
		p_libuvc.c
		)
endif()

add_library(st_prober STATIC ${PROBER_SOURCE_FILES})
target_link_libraries(st_prober PUBLIC xrt-interfaces PRIVATE aux_util aux_os aux_tracking)
target_include_directories(st_prober PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../drivers)

if(BUILD_WITH_LIBUDEV)
	target_include_directories(st_prober
		PRIVATE
		${UDEV_INCLUDE_DIRS}
		)
	target_link_libraries(st_prober PRIVATE ${UDEV_LIBRARIES})
endif()

if(BUILD_WITH_LIBUSB)
	target_include_directories(st_prober
		PRIVATE
		${LIBUSB1_INCLUDE_DIRS}
		)
	target_link_libraries(st_prober PRIVATE ${LIBUSB1_LIBRARIES})
endif()

if(BUILD_WITH_LIBUVC)
	target_include_directories(st_prober
		PRIVATE
		${LIBUVC_INCLUDES}
		)
	target_link_libraries(st_prober PRIVATE ${LIBUVC_LIBRARIES})
endif()
