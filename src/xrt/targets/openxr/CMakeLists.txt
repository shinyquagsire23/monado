# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a loadable OpenXR driver.

set(RUNTIME_BARE_SUFFIX monado)
set(RUNTIME_SUFFIX _${RUNTIME_BARE_SUFFIX})

set(RUNTIME_TARGET ${RUNTIME_PREFIX}openxr${RUNTIME_SUFFIX} CACHE INTERNAL "" FORCE)

# OpenXR 1.0
set(XR_API_MAJOR "1")

set(SOURCE_FILES
	target.c
	oxr_sdl2_hack.c
	)

# depends on above generated files
add_library(${RUNTIME_TARGET} SHARED
	${MANIFEST_DEV_PATH}
	${MANIFEST_PATH}
	${SOURCE_FILES}
	)

# Note: Order matters in this list!
target_link_libraries(${RUNTIME_TARGET} PUBLIC
	aux_vk
	aux_os
	aux_ogl
	aux_util
	aux_math
	st_oxr
	st_gui
	st_prober
	comp_main
	comp_client
	target_lists
	)

if(BUILD_WITH_SDL2)
	target_link_libraries(${RUNTIME_TARGET} PUBLIC imgui_impl_sdl)
endif()

if(NOT MSVC)
	# Force the main "negotiate" symbol's inclusion
	set_property(TARGET ${RUNTIME_TARGET}
		APPEND_STRING
		PROPERTY LINK_FLAGS
		"-u xrNegotiateLoaderRuntimeInterface")
endif()


include(GNUInstallDirs)

# Install the runtime itself
install(TARGETS ${RUNTIME_TARGET}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)


# $LIBPATH is a generator expression that is evaluated after configure_file, so we have to use file(GENERATE) instead

###
# Generate runtime manifest with absolute path to runtime intended for development without installing
set(MANIFEST_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/openxr_monado.in.json)
set(RUNTIME_PATH $<TARGET_SONAME_FILE:${RUNTIME_TARGET}>)

# Need this step because file(GENERATE) only evaluates generator expressions, and not what configure_file does.
configure_file(${MANIFEST_INPUT} ${CMAKE_CURRENT_BINARY_DIR}/intermediate_manifest.json)
file(GENERATE
	OUTPUT "${CMAKE_BINARY_DIR}/${RUNTIME_TARGET}-dev.json"
	INPUT ${CMAKE_CURRENT_BINARY_DIR}/intermediate_manifest.json)

###
# Prepare the installable manifest: will be generated completely at install time,
# by a script we generate now.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(MANIFEST_RELATIVE_DIR share/openxr/${XR_API_MAJOR}/)
	if(INSTALL_ACTIVE_RUNTIME)
		configure_file(active_runtime.cmake ${CMAKE_CURRENT_BINARY_DIR}/active_runtime.cmake @ONLY)
		install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/active_runtime.cmake)
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	# TODO: install target on windows just needs to know where to install manifest
	set(MANIFEST_RELATIVE_DIR)
endif()

# If we know where to install the manifest, we can set it up to be installed.
if(MANIFEST_RELATIVE_DIR)
	set(RUNTIME_RELATIVE_DIR ${CMAKE_INSTALL_LIBDIR})
	set(RUNTIME_FILENAME ${CMAKE_SHARED_MODULE_PREFIX}${RUNTIME_TARGET}${CMAKE_SHARED_MODULE_SUFFIX})
	configure_file(make_manifest.cmake ${CMAKE_CURRENT_BINARY_DIR}/make_manifest.cmake @ONLY)
	install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/make_manifest.cmake)
endif()
