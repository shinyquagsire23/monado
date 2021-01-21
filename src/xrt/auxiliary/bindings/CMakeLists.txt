# Copyright 2019-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

###
# Binding generation
#

function(bindings_gen output custom_target)
	add_custom_command(OUTPUT ${output}
		COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/bindings.py
			${CMAKE_CURRENT_SOURCE_DIR}/bindings.json
			${output}
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bindings.py
			${CMAKE_CURRENT_SOURCE_DIR}/bindings.json
			)
	add_custom_target(${custom_target} DEPENDS ${output})
endfunction(bindings_gen)

bindings_gen(${CMAKE_CURRENT_BINARY_DIR}/b_generated_bindings.h generated_bindings_h)
bindings_gen(${CMAKE_CURRENT_BINARY_DIR}/b_generated_bindings.c generated_bindings_c)

# Bindings library.
add_library(aux_generated_bindings STATIC ${CMAKE_CURRENT_BINARY_DIR}/b_generated_bindings.c)
add_dependencies(aux_generated_bindings generated_bindings_h)

# needed globally for steamvr input profile generation in steamvr target
set_property(GLOBAL PROPERTY AUX_BINDINGS_DIR_PROP "${CMAKE_CURRENT_SOURCE_DIR}")

target_include_directories(aux_generated_bindings PRIVATE aux-includes xrt-interfaces)
target_link_libraries(aux_generated_bindings PRIVATE xrt-interfaces aux_util)
