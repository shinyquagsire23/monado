# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Catch2
add_library(xrt-external-catch2 INTERFACE)
target_include_directories(xrt-external-catch2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Catch2)
add_library(xrt-external-openvr INTERFACE)
target_include_directories(xrt-external-openvr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/openvr_includes)


# cJSON
add_library(xrt-external-cjson INTERFACE)
if(XRT_HAVE_SYSTEM_CJSON)
	target_link_libraries(xrt-external-cjson INTERFACE cJSON::cJSON)
else()
	target_include_directories(xrt-external-cjson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cjson)
	target_compile_definitions(xrt-external-cjson INTERFACE CJSON_HIDE_SYMBOLS)
endif()

# FlexKalman
add_library(xrt-external-flexkalman INTERFACE)
target_include_directories(xrt-external-flexkalman INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/flexkalman)

# Glad
add_library(xrt-external-glad INTERFACE)
target_include_directories(xrt-external-glad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include)

# Hungarian graph algorithm
add_library(xrt-external-hungarian INTERFACE)
target_include_directories(xrt-external-hungarian INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/hungarian)

# JNIPP and Android JNI wrappers
if(ANDROID)
	add_library(xrt-external-jnipp STATIC
		jnipp/jnipp.cpp)
	target_include_directories(xrt-external-jnipp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/jnipp)

	file(GLOB WRAP_SOURCES android-jni-wrap/wrap/*.cpp)
	add_library(xrt-external-jni-wrap STATIC
		${WRAP_SOURCES})
	target_include_directories(xrt-external-jni-wrap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/android-jni-wrap)
	target_link_libraries(xrt-external-jni-wrap PUBLIC xrt-external-jnipp)
	set_target_properties(xrt-external-jni-wrap
		PROPERTIES
		CXX_STANDARD 17
		CXX_STANDARD_REQUIRED ON)
endif()

# OpenXR
add_library(xrt-external-openxr INTERFACE)
target_include_directories(xrt-external-openxr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/openxr_includes)
