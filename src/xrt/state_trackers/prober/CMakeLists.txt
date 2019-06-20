# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../auxiliary
	${CMAKE_CURRENT_SOURCE_DIR}/../../../external
	)

set(PROBER_INCLUDES)

set(PROBER_SOURCE_FILES
	p_auto_wrap.c
	p_documentation.h
	p_dump.c
	p_prober.c
	p_prober.h
	)

# Add libudev
if(BUILD_WITH_LIBUDEV)
	list(APPEND PROBER_SOURCE_FILES
		p_udev.c
		)
	list(APPEND PROBER_INCLUDES
		${LIBUDEV_INCLUDES}
		)
endif()

# Add libusb
if(BUILD_WITH_LIBUSB)
	list(APPEND PROBER_SOURCE_FILES
		p_libusb.c
		)
	list(APPEND PROBER_INCLUDES
		${LIBUSB_INCLUDES}
		)
endif()

# Add libuvc
if(BUILD_WITH_LIBUVC)
	list(APPEND PROBER_SOURCE_FILES
		p_libuvc.c
		)
	list(APPEND PROBER_INCLUDES
		${LIBUVC_INCLUDES}
		)
endif()

# Use OBJECT to not create a archive, since it just gets in the way.
add_library(st_prober OBJECT ${PROBER_SOURCE_FILES})

target_include_directories(st_prober
	PRIVATE
	${PROBER_INCLUDES}
	)

set_property(TARGET st_prober PROPERTY POSITION_INDEPENDENT_CODE ON)
