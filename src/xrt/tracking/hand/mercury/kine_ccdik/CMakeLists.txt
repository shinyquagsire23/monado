# Copyright 2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(t_ht_mercury_kine_ccdik STATIC ccdik_interface.hpp ccdik_main.cpp)

target_link_libraries(t_ht_mercury_kine_ccdik PRIVATE aux_math aux_tracking aux_os aux_util)

target_include_directories(t_ht_mercury_kine_ccdik SYSTEM PRIVATE ${EIGEN3_INCLUDE_DIR})
