# Copyright 2019, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

find_program(GLSLANGVALIDATOR_COMMAND
	glslangValidator)
if(NOT GLSLANGVALIDATOR_COMMAND)
	message(FATAL_ERROR "glslangValidator required - source maintained at https://github.com/KhronosGroup/glslang")
endif()

#
# Generate SPIR-V header files from the arguments. Returns a list of headers.
#
function(spirv_shaders ret)
	set(options)
	set(oneValueArgs SPIRV_VERSION)
	set(multiValueArgs SOURCES)
	cmake_parse_arguments(_spirvshaders "${options}" "${oneValueArgs}"
	                      "${multiValueArgs}" ${ARGN})

	if(NOT _spirvshaders_SPIRV_VERSION)
		set(_spirvshaders_SPIRV_VERSION 1.0)
	endif()

	foreach(GLSL ${_spirvshaders_SOURCES})
		string(MAKE_C_IDENTIFIER ${GLSL} IDENTIFIER)
		set(HEADER "${CMAKE_CURRENT_BINARY_DIR}/${GLSL}.h")
		set(GLSL "${CMAKE_CURRENT_SOURCE_DIR}/${GLSL}")

		add_custom_command(
			OUTPUT ${HEADER}
			COMMAND ${GLSLANGVALIDATOR_COMMAND} -V --target-env spirv${_spirvshaders_SPIRV_VERSION} ${GLSL} --vn ${IDENTIFIER} -o ${HEADER}
			DEPENDS ${GLSL})
		list(APPEND HEADERS ${HEADER})
	endforeach()

	set(${ret} "${HEADERS}" PARENT_SCOPE)
endfunction(spirv_shaders)
