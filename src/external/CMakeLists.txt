# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(xrt-external-openxr INTERFACE)
target_include_directories(xrt-external-openxr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/openxr_includes)

add_library(xrt-external-glad INTERFACE)
target_include_directories(xrt-external-glad INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include)

add_library(xrt-external-cjson INTERFACE)
if(XRT_HAVE_SYSTEM_CJSON)
	target_link_libraries(xrt-external-cjson INTERFACE cJSON::cJSON)
else()
	target_include_directories(xrt-external-cjson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cjson)
endif()

add_library(xrt-external-flexkalman INTERFACE)
target_include_directories(xrt-external-flexkalman INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/flexkalman)

add_library(xrt-external-catch2 INTERFACE)
target_include_directories(xrt-external-catch2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Catch2)
