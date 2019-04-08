# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a loadable OpenXR driver.

# set(RUNTIME_BARE_PREFIX xrt)
# set(RUNTIME_PREFIX ${RUNTIME_BARE_PREFIX}_)

set(RUNTIME_BARE_SUFFIX monado)
set(RUNTIME_SUFFIX _${RUNTIME_BARE_SUFFIX})

set(RUNTIME_TARGET ${RUNTIME_PREFIX}openxr${RUNTIME_SUFFIX} CACHE INTERNAL "" FORCE)

set(XR_API_MAJOR "0")


# $LIBPATH is a generator expression that is evaluated after configure_file, so we have to use file(GENERATE) instead

###
# Generate runtime manifest with absolute path to runtime intended for development without installing
file(
	GENERATE
	OUTPUT "${CMAKE_BINARY_DIR}/${RUNTIME_TARGET}-dev.json"
	CONTENT
"{
    \"file_format_version\": \"1.0.0\",
    \"runtime\": {
        \"library_path\": \"$<TARGET_SONAME_FILE:${RUNTIME_TARGET}>\"
    }
}
")

###
# Generate runtime manifest with relative path to runtime intendend for installed release build
# (assumes the runtime library is in the loader's search path)
file(
	GENERATE
	OUTPUT "${CMAKE_BINARY_DIR}/${RUNTIME_TARGET}.json"
	CONTENT
"{
    \"file_format_version\": \"1.0.0\",
    \"runtime\": {
        \"library_path\": \"$<TARGET_SONAME_FILE_NAME:${RUNTIME_TARGET}>\"
    }
}
")

include_directories(
	${CMAKE_CURRENT_SOURCE_DIR}/../../include
	${CMAKE_CURRENT_SOURCE_DIR}/../../drivers
	)

set(SOURCE_FILES
	target.c
	)

# depends on above generated files
add_library(${RUNTIME_TARGET} SHARED
	${MANIFEST_DEV_PATH}
	${MANIFEST_PATH}
	${SOURCE_FILES}
	$<TARGET_OBJECTS:aux_util>
	$<TARGET_OBJECTS:aux_math>
	$<TARGET_OBJECTS:comp>
	$<TARGET_OBJECTS:drv_ohmd>
	$<TARGET_OBJECTS:st_oxr>
	)

target_link_libraries(${RUNTIME_TARGET}
	PUBLIC
	${Vulkan_LIBRARIES}
	${XCB_LIBRARIES}
	OpenGL::GLX
	)


if(BUILD_WITH_OPENHMD)
	target_sources(${RUNTIME_TARGET} PRIVATE $<TARGET_OBJECTS:drv_ohmd>)
	target_compile_definitions(${RUNTIME_TARGET} PRIVATE XRT_HAVE_OHMD)
	target_link_libraries(${RUNTIME_TARGET} INTERFACE OpenHMD::OpenHMD)
endif()

if(BUILD_WITH_HIDAPI)
	target_sources(${RUNTIME_TARGET} PRIVATE $<TARGET_OBJECTS:drv_hdk>)
	target_link_libraries(${RUNTIME_TARGET} PUBLIC ${HIDAPI_LIBRARIES})
	target_compile_definitions(${RUNTIME_TARGET} PRIVATE XRT_HAVE_HDK)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	include(GNUInstallDirs)
	install(TARGETS ${RUNTIME_TARGET}
		LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
		ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
		RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
	install(FILES "${CMAKE_BINARY_DIR}/${RUNTIME_TARGET}.json" DESTINATION "share/openxr/${XR_API_MAJOR}/")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
	# TODO: install target on windows
endif()
