# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(t_ht_mercury_kine_lm STATIC lm_interface.hpp lm_main.cpp)

target_link_libraries(
	t_ht_mercury_kine_lm
	PRIVATE
		aux_math
		aux_tracking
		aux_os
		aux_util
		xrt-external-tinyceres
	)

target_include_directories(t_ht_mercury_kine_lm SYSTEM PRIVATE ${EIGEN3_INCLUDE_DIR})

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
	target_compile_options(t_ht_mercury_kine_lm PRIVATE -ftemplate-backtrace-limit=20)
endif()

# Below is entirely just so that tests can find us.
add_library(t_ht_mercury_kine_lm_includes INTERFACE)

target_include_directories(
	t_ht_mercury_kine_lm_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
						${CMAKE_CURRENT_BINARY_DIR}
	)
