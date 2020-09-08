# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Generate a header containing defines for each enabled driver
set(FILE_CONTENTS "")
foreach(DRIVER ${AVAILABLE_DRIVERS})
	if(XRT_BUILD_DRIVER_${DRIVER})
		string(APPEND FILE_CONTENTS "#define XRT_BUILD_DRIVER_${DRIVER}\n")
	endif()
endforeach()

# First setup all of the config headers.
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/xrt_config_drivers.h.cmake_in
	${CMAKE_CURRENT_BINARY_DIR}/xrt_config_drivers.h
	@ONLY
	)
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/xrt_config_have.h.cmake_in
	${CMAKE_CURRENT_BINARY_DIR}/xrt_config_have.h
	@ONLY
	)
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/xrt_config_build.h.cmake_in
	${CMAKE_CURRENT_BINARY_DIR}/xrt_config_build.h
	@ONLY
	)
configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/xrt_config_vulkan.h.cmake_in
	${CMAKE_CURRENT_BINARY_DIR}/xrt_config_vulkan.h
	@ONLY
	)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/xrt_config_android.h.cmake_in
	${CMAKE_CURRENT_BINARY_DIR}/xrt_config_android.h
	@ONLY
	)
