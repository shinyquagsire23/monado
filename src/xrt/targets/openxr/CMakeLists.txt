# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a loadable OpenXR driver.

set(RUNTIME_BARE_SUFFIX monado)
set(RUNTIME_SUFFIX _${RUNTIME_BARE_SUFFIX})

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

if(NOT MSVC)
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

# Install the runtime itself
install(
	TARGETS ${RUNTIME_TARGET} #
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} #
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} #
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)

###
# Generate runtime manifest with absolute path to runtime intended for development without installing
set(MANIFEST_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/openxr_monado.in.json)
set(runtime_path $<TARGET_FILE:${RUNTIME_TARGET}>)

# Need this step because file(GENERATE) only evaluates generator expressions, and not what configure_file does.
configure_file(${MANIFEST_INPUT} ${CMAKE_CURRENT_BINARY_DIR}/intermediate_manifest.json)

if(CMAKE_VERSION VERSION_LESS 3.9)
	# best guess
	if(CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
		set(IS_MULTI_CONFIG FALSE)
	else()
		set(IS_MULTI_CONFIG TRUE)
	endif()
else()
	# 3.9+ have a global property with the truth
	get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
endif()

if(IS_MULTI_CONFIG)
	set(DEV_MANIFEST_OUTPUT "${CMAKE_BINARY_DIR}/$<CONFIG>/${RUNTIME_TARGET}-dev.json")
else()
	set(DEV_MANIFEST_OUTPUT "${CMAKE_BINARY_DIR}/${RUNTIME_TARGET}-dev.json")
endif()

file(
	GENERATE
	OUTPUT "${DEV_MANIFEST_OUTPUT}"
	INPUT ${CMAKE_CURRENT_BINARY_DIR}/intermediate_manifest.json
	)

###
# Prepare the installable manifest: will be generated completely at install time,
# by a script we generate now.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(MANIFEST_RELATIVE_DIR share/openxr/${XR_API_MAJOR}/)
	if(XRT_OPENXR_INSTALL_ACTIVE_RUNTIME)
		configure_file(
			active_runtime.cmake ${CMAKE_CURRENT_BINARY_DIR}/active_runtime.cmake @ONLY
			)
		install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/active_runtime.cmake)
	endif()
	set(RUNTIME_RELATIVE_DIR ${CMAKE_INSTALL_LIBDIR})
	if(XRT_OPENXR_INSTALL_MANIFEST_RELATIVE_RUNTIME_PATH
	   AND NOT XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH
		)
		set(RUNTIME_DIR_RELATIVE_TO_MANIFEST ../../../${RUNTIME_RELATIVE_DIR})
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	# Windows: install manifest to top level
	set(MANIFEST_RELATIVE_DIR .)
	set(RUNTIME_RELATIVE_DIR ${CMAKE_INSTALL_BINDIR})
	if(XRT_OPENXR_INSTALL_MANIFEST_RELATIVE_RUNTIME_PATH
	   AND NOT XRT_OPENXR_INSTALL_ABSOLUTE_RUNTIME_PATH
		)
		set(RUNTIME_DIR_RELATIVE_TO_MANIFEST ./${RUNTIME_RELATIVE_DIR})
	endif()
endif()

# If we know where to install the manifest, we can set it up to be installed.
if(MANIFEST_RELATIVE_DIR)
	set(RUNTIME_FILENAME
	    ${CMAKE_SHARED_MODULE_PREFIX}${RUNTIME_TARGET}${CMAKE_SHARED_MODULE_SUFFIX}
		)
	configure_file(make_manifest.cmake ${CMAKE_CURRENT_BINARY_DIR}/make_manifest.cmake @ONLY)
	install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/make_manifest.cmake)
endif()

###
# Inelegant but effective SDL2-based debug GUI
add_library(oxr_sdl2 STATIC oxr_sdl2_hack.c)
target_link_libraries(oxr_sdl2 PRIVATE aux_util)
if(XRT_HAVE_OPENGL)
	target_link_libraries(oxr_sdl2 PUBLIC aux_ogl)
endif()
if(XRT_HAVE_SDL2)
	target_link_libraries(oxr_sdl2 PRIVATE st_gui xrt-external-imgui-sdl2 ${SDL2_LIBRARIES})

	if(XRT_BUILD_DRIVER_QWERTY)
		target_link_libraries(oxr_sdl2 PRIVATE drv_qwerty drv_qwerty_includes)
	endif()
endif()
target_link_libraries(${RUNTIME_TARGET} PRIVATE oxr_sdl2)
