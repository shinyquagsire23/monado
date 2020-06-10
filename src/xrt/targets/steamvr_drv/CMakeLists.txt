# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


add_library(driver_monado MODULE
	main.c
	)

target_link_libraries(driver_monado PRIVATE
	xrt-external-openvr
	aux_util
	st_ovrd
	st_prober
	target_lists
	target_instance_no_comp
	)


# meta data that the steamvr plugin needs in the base directory of the steamvr plugin
file(COPY driver.vrdrivermanifest DESTINATION ${CMAKE_BINARY_DIR}/steamvr-monado)
file(COPY resources DESTINATION ${CMAKE_BINARY_DIR}/steamvr-monado)

#determine the output directory for the steamvr plugin
if (WIN32)
  # FIXME need to account for different architectures
  if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(PLUGINDIR ${CMAKE_BINARY_DIR}/steamvr-monado/bin/win64 CACHE TYPE INTERNAL)
  else()
    set(PLUGINDIR ${CMAKE_BINARY_DIR}/steamvr-monado/bin/win32 CACHE TYPE INTERNAL)
  endif()
elseif(APPLE)
  if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(PLUGINDIR ${CMAKE_BINARY_DIR}/steamvr-monado/bin/osx64 CACHE STRING INTERNAL)
  else()
    set(PLUGINDIR ${CMAKE_BINARY_DIR}/steamvr-monado/bin/osx32 CACHE STRING INTERNAL)
  endif()
elseif(NOT ANDROID)
  if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    set(PLUGINDIR ${CMAKE_BINARY_DIR}/steamvr-monado/bin/linux64 CACHE STRING INTERNAL)
  else()
    set(PLUGINDIR ${CMAKE_BINARY_DIR}/steamvr-monado/bin/linux32 CACHE STRING INTERNAL)
  endif()
endif()

MESSAGE("SteamVR plugin path: ${PLUGINDIR}")
set_target_properties(driver_monado PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${PLUGINDIR}")

# don't add lib prefix to driver_monado.so
SET_TARGET_PROPERTIES(driver_monado PROPERTIES PREFIX "")

install(DIRECTORY "${PROJECT_BINARY_DIR}/steamvr-monado" DESTINATION "${CMAKE_INSTALL_PREFIX}/share")
