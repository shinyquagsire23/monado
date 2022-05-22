# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

# Binding generation: pass filename to generate
function(bindings_gen output)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${output}"
		COMMAND
			${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings.py
			${CMAKE_CURRENT_SOURCE_DIR}/bindings.json
			"${CMAKE_CURRENT_BINARY_DIR}/${output}"
		VERBATIM
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bindings.py
			${CMAKE_CURRENT_SOURCE_DIR}/bindings.json
		COMMENT "Generating ${output}"
		)
endfunction()

bindings_gen(b_generated_bindings.h)
bindings_gen(b_generated_bindings.c)

# Bindings library.
add_library(
	aux_generated_bindings STATIC ${CMAKE_CURRENT_BINARY_DIR}/b_generated_bindings.c
				      ${CMAKE_CURRENT_BINARY_DIR}/b_generated_bindings.h
	)

# needed globally for steamvr input profile generation in steamvr target
set_property(GLOBAL PROPERTY AUX_BINDINGS_DIR_PROP "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(aux_generated_bindings PRIVATE xrt-interfaces)
