# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(xrt-external-openxr INTERFACE)
target_include_directories(xrt-external-openxr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/openxr_includes)

add_library(xrt-external-glad INTERFACE)
target_include_directories(xrt-external-glad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glad)

if(BUILD_WITH_SYSTEM_CJSON)
	add_library(xrt-external-cjson ALIAS cJSON::cJSON)
else()
	add_library(xrt-external-cjson INTERFACE)
	target_include_directories(xrt-external-cjson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cjson)
endif()

add_library(xrt-external-flexkalman INTERFACE)
target_include_directories(xrt-external-flexkalman INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/flexkalman)
