# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

get_property(AUX_BINDINGS_DIR GLOBAL PROPERTY AUX_BINDINGS_DIR_PROP)
set(INPUT_PROFILES_INPUT_DIR ${AUX_BINDINGS_DIR})
set(INPUT_PROFILES_OUTPUT_DIR "${PROJECT_BINARY_DIR}/steamvr-monado/resources/input/")

function(input_profiles_gen output custom_target)
	add_custom_command(OUTPUT ${output}
		COMMAND ${PYTHON_EXECUTABLE} ${INPUT_PROFILES_INPUT_DIR}/steamvr_profiles.py
			${INPUT_PROFILES_INPUT_DIR}/bindings.json
			${output}
		DEPENDS ${INPUT_PROFILES_INPUT_DIR}/bindings.py
			${INPUT_PROFILES_INPUT_DIR}/bindings.json
			)
	add_custom_target(${custom_target} DEPENDS ${output})
endfunction(input_profiles_gen)

input_profiles_gen(${INPUT_PROFILES_OUTPUT_DIR} generated_input_profiles)

add_library(driver_monado MODULE main.c)
add_dependencies(driver_monado generated_input_profiles)

target_link_libraries(
	driver_monado PRIVATE xrt-external-openvr aux_util st_ovrd st_prober target_lists target_instance_no_comp
	)

# meta data that the steamvr plugin needs in the base directory of the steamvr plugin
file(COPY driver.vrdrivermanifest DESTINATION ${PROJECT_BINARY_DIR}/steamvr-monado)
file(COPY resources DESTINATION ${PROJECT_BINARY_DIR}/steamvr-monado)

#determine the output directory for the steamvr plugin
if(WIN32)
	# FIXME need to account for different architectures
	if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
		set(PLUGINDIR
			${PROJECT_BINARY_DIR}/steamvr-monado/bin/win64
			CACHE INTERNAL ""
			)
	else()
		set(PLUGINDIR
			${PROJECT_BINARY_DIR}/steamvr-monado/bin/win32
			CACHE INTERNAL ""
			)
	endif()
elseif(APPLE)
	if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
		set(PLUGINDIR
			${PROJECT_BINARY_DIR}/steamvr-monado/bin/osx64
			CACHE INTERNAL ""
			)
	else()
		set(PLUGINDIR
			${PROJECT_BINARY_DIR}/steamvr-monado/bin/osx32
			CACHE INTERNAL ""
			)
	endif()
elseif(NOT ANDROID)
	if("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
		set(PLUGINDIR
			${PROJECT_BINARY_DIR}/steamvr-monado/bin/linux64
			CACHE INTERNAL ""
			)
	else()
		set(PLUGINDIR
			${PROJECT_BINARY_DIR}/steamvr-monado/bin/linux32
			CACHE INTERNAL ""
			)
	endif()
endif()

message("SteamVR plugin path: ${PLUGINDIR}")
set_target_properties(driver_monado PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PLUGINDIR}")

# don't add lib prefix to driver_monado.so
set_target_properties(driver_monado PROPERTIES PREFIX "")

install(DIRECTORY "${PROJECT_BINARY_DIR}/steamvr-monado" DESTINATION "${CMAKE_INSTALL_PREFIX}/share")
