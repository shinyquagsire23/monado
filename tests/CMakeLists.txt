# Copyright 2018-2021, Collabora, Ltd.
#
# SPDX-License-Identifier: BSL-1.0

# Catch2 main test driver
add_library(tests_main STATIC tests_main.cpp)
target_link_libraries(tests_main PUBLIC xrt-external-catch2)
if(ANDROID)
	target_link_libraries(tests_main PUBLIC log)
endif()

# Input transform test
add_executable(tests_input_transform tests_input_transform.cpp)
target_link_libraries(tests_input_transform PRIVATE tests_main)
target_link_libraries(
	tests_input_transform PRIVATE st_oxr xrt-interfaces xrt-external-openxr aux_util
	)
add_test(NAME input_transform COMMAND tests_input_transform --success)

# Generic callbacks
add_executable(tests_generic_callbacks tests_generic_callbacks.cpp)
target_link_libraries(tests_generic_callbacks PRIVATE tests_main)
target_link_libraries(tests_generic_callbacks PRIVATE aux_util)
add_test(NAME tests_generic_callbacks COMMAND tests_generic_callbacks --success)

# cJSON Wrapper
add_executable(tests_json tests_json.cpp)
target_link_libraries(tests_json PRIVATE tests_main)
target_link_libraries(tests_json PRIVATE aux_util)
add_test(NAME tests_json COMMAND tests_json --success)
