# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(MATH_SOURCE_FILES
	math/m_api.h
	math/m_base.cpp
	math/m_eigen_interop.h
	math/m_hash.cpp
	math/m_optics.c
	math/m_quatexpmap.cpp
	)

set(SOURCE_FILES
	util/u_misc.c
	util/u_misc.h
	util/u_debug.c
	util/u_debug.h
	util/u_device.c
	util/u_device.h
	util/u_documentation.h
	util/u_hashset.cpp
	util/u_hashset.h
	util/u_time.cpp
	util/u_time.h
	)

# Common includes
include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${CMAKE_CURRENT_SOURCE_DIR}
	)

# Use OBJECT to not create a archive, since it just gets in the way.
add_library(aux_util OBJECT ${SOURCE_FILES})

set_property(TARGET aux_util PROPERTY POSITION_INDEPENDENT_CODE ON)


# Math library has different includes.
add_library(aux_math OBJECT ${MATH_SOURCE_FILES})

target_include_directories(aux_math SYSTEM
	PRIVATE ${EIGEN3_INCLUDE_DIR}
	)

set_property(TARGET aux_math PROPERTY POSITION_INDEPENDENT_CODE ON)
