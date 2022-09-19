# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	aux_math STATIC
	m_api.h
	m_base.cpp
	m_eigen_interop.hpp
	m_filter_fifo.c
	m_filter_fifo.h
	m_filter_one_euro.c
	m_filter_one_euro.h
	m_hash.cpp
	m_imu_3dof.c
	m_imu_3dof.h
	m_imu_pre.c
	m_imu_pre.h
	m_lowpass_float.cpp
	m_lowpass_float.h
	m_lowpass_float.hpp
	m_lowpass_float_vector.hpp
	m_lowpass_integer.cpp
	m_lowpass_integer.h
	m_lowpass_integer.hpp
	m_optics.c
	m_permutation.c
	m_permutation.h
	m_predict.c
	m_predict.h
	m_quatexpmap.cpp
	m_rational.hpp
	m_relation_history.cpp
	m_relation_history.h
	m_space.cpp
	m_space.h
	m_vec2.h
	m_vec3.h
	)
target_link_libraries(aux_math PUBLIC aux-includes aux_util)
target_include_directories(aux_math SYSTEM PRIVATE ${EIGEN3_INCLUDE_DIR})
