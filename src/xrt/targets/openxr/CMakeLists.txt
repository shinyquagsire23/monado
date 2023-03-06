# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a loadable OpenXR driver.

set(RUNTIME_SUFFIX _${XRT_OXR_RUNTIME_SUFFIX})

set(RUNTIME_TARGET
    ${RUNTIME_PREFIX}openxr${RUNTIME_SUFFIX}
    CACHE INTERNAL "" FORCE
	)

# OpenXR 1.0
set(XR_API_MAJOR "1")

add_library(${RUNTIME_TARGET} MODULE target.c libopenxr.def)

# Note: Order may matter in these lists!
target_link_libraries(${RUNTIME_TARGET} PUBLIC aux_vk aux_os aux_util aux_math)

if(XRT_FEATURE_SERVICE)
	target_link_libraries(${RUNTIME_TARGET} PUBLIC st_oxr ipc_client comp_client)
else()
	target_link_libraries(
		${RUNTIME_TARGET}
		PUBLIC
			st_oxr
			st_prober
			target_lists
			target_instance
			comp_main
			comp_client
		)
endif()

if(NOT WIN32)
	# Force the main "negotiate" symbol's inclusion
	# and use a version script to ensure that's the only one we expose.
	set_property(
		TARGET ${RUNTIME_TARGET}
		APPEND_STRING
		PROPERTY
			LINK_FLAGS
			"-u xrNegotiateLoaderRuntimeInterface -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libopenxr.version"
		)
	# Re-link if the version script changes.
	set_property(
		TARGET ${RUNTIME_TARGET}
		APPEND
		PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libopenxr.version"
		)
endif()

###
# Generate runtime manifest with absolute path to runtime intended for development without installing

# 3.9+ have a global property with the truth
get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(IS_MULTI_CONFIG)
	set(DEV_MANIFEST_OUTPUT "${CMAKE_BINARY_DIR}/$<CONFIG>/${RUNTIME_TARGET}-dev.json")
else()
	set(DEV_MANIFEST_OUTPUT "${CMAKE_BINARY_DIR}/${RUNTIME_TARGET}-dev.json")
endif()

include(GenerateOpenXRRuntimeManifest)
generate_openxr_runtime_manifest_buildtree(
	RUNTIME_TARGET ${RUNTIME_TARGET} #
	OUT_FILE ${DEV_MANIFEST_OUTPUT}
	)

###
# Prepare the installable manifest: will be generated completely at install time,
# by a script we generate now.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	# Windows: install manifest to top level
	set(_extra_args)
	if(XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH)
		set(_extra_args ABSOLUTE_RUNTIME_PATH)
	elseif(XRT_OPENXR_INSTALL_MANIFEST_RELATIVE_RUNTIME_PATH)
		set(_extra_args RUNTIME_DIR_RELATIVE_TO_MANIFEST ${CMAKE_INSTALL_BINDIR})
	endif()
	generate_openxr_runtime_manifest_at_install(
		${_extra_args}
		RUNTIME_TARGET ${RUNTIME_TARGET}
		DESTINATION .
		RELATIVE_RUNTIME_DIR ${CMAKE_INSTALL_BINDIR}
		)
	install(
		TARGETS ${RUNTIME_TARGET}
		LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime
		)
	if("${CMAKE_VERSION}" VERSION_GREATER_EQUAL 3.21)
		# This generator expression is easier to control than install(RUNTIME_DEPENDENCY_SETS),
		# which is prone to trying to distribute system DLLs.
		install(
			FILES $<TARGET_RUNTIME_DLLS:${RUNTIME_TARGET}>
			DESTINATION ${CMAKE_INSTALL_BINDIR}
			COMPONENT Runtime
			)
	endif()
elseif(NOT ANDROID)
	# Linux, BSD, etc.
	set(MANIFEST_RELATIVE_DIR share/openxr/${XR_API_MAJOR}/)
	if(XRT_OPENXR_INSTALL_ACTIVE_RUNTIME)
		configure_file(
			active_runtime.cmake ${CMAKE_CURRENT_BINARY_DIR}/active_runtime.cmake @ONLY
			)
		install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/active_runtime.cmake)
	endif()

	set(_extra_args)
	if(XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH)
		set(_extra_args ABSOLUTE_RUNTIME_PATH)
	elseif(XRT_OPENXR_INSTALL_MANIFEST_RELATIVE_RUNTIME_PATH)
		set(_extra_args RUNTIME_DIR_RELATIVE_TO_MANIFEST ../../../${CMAKE_INSTALL_LIBDIR})
	endif()
	generate_openxr_runtime_manifest_at_install(
		${_extra_args}
		RUNTIME_TARGET ${RUNTIME_TARGET}
		DESTINATION share/openxr/${XR_API_MAJOR}
		RELATIVE_RUNTIME_DIR ${CMAKE_INSTALL_LIBDIR}
		)
	install(
		TARGETS ${RUNTIME_TARGET} #
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
		RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Runtime
		)
endif()

# Optional debug ui
if(XRT_FEATURE_CLIENT_DEBUG_GUI)
	target_link_libraries(${RUNTIME_TARGET} PRIVATE aux_util_debug_gui)
endif()
