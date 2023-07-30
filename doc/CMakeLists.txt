# Copyright 2018-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# check if Doxygen is installed
find_package(Doxygen)

option_with_deps(
	BUILD_DOC "Build documentation"
	DEFAULT OFF
	DEPENDS DOXYGEN_FOUND
	)
option_with_deps(
	BUILD_DOC_WARN_UNDOCUMENTED "Warn on undocumented entities when building documentation"
	DEFAULT OFF
	DEPENDS DOXYGEN_FOUND
	)
option_with_deps(
	BUILD_DOC_EXTRACT_ALL
	"Extract all entities for documentation, not only documented ones (conflicts with BUILD_DOC_WARN_UNDOCUMENTED)"
	DEFAULT OFF
	DEPENDS DOXYGEN_FOUND "NOT BUILD_DOC_WARN_UNDOCUMENTED"
	)

if(BUILD_DOC)
	if(BUILD_DOC_WARN_UNDOCUMENTED)
		set(DOXYGEN_WARN_UNDOCUMENTED YES)
	else()
		set(DOXYGEN_WARN_UNDOCUMENTED NO)
	endif()
	if(BUILD_DOC_EXTRACT_ALL)
		set(DOXYGEN_EXTRACT_ALL YES)
	else()
		set(DOXYGEN_EXTRACT_ALL NO)
	endif()

	# set input and output files
	set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
	set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

	set(CURBUILDDIR ${CMAKE_CURRENT_BINARY_DIR})
	set(SRCDIR ${PROJECT_SOURCE_DIR})
	set(BUILDDIR ${PROJECT_BINARY_DIR})
	# request to configure the file
	configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)

	# copy the schemas
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/example_configs/config_v0.schema.json
		${CMAKE_CURRENT_BINARY_DIR}/html/config_v0.schema.json @ONLY
		)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/example_configs/config_v0.schema.json.license
		${CMAKE_CURRENT_BINARY_DIR}/html/config_v0.schema.json.license @ONLY
		)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/example_configs/calibration_v2.schema.json
		${CMAKE_CURRENT_BINARY_DIR}/html/calibration_v2.schema.json @ONLY
		)
	configure_file(
		${CMAKE_CURRENT_SOURCE_DIR}/example_configs/calibration_v2.schema.json.license
		${CMAKE_CURRENT_BINARY_DIR}/html/calibration_v2.schema.json.license @ONLY
		)

	# note the option ALL which allows to build the docs together with the application
	add_custom_target(
		doc_doxygen ALL
		COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
		WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
		COMMENT "Generating API documentation with Doxygen"
		VERBATIM
		)
endif()
