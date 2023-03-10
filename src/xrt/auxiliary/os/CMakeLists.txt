# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

####
# Main os library
#
# Avoid linking with libraries that brings in DSOs as this
# library is used everywhere. Including libraries loaded in
# by applications like the OpenXR runtime library.
#

add_library(
	aux_os STATIC
	os_documentation.h
	os_hid.h
	os_hid_hidraw.c
	os_threading.h
	os_time.cpp
	)
target_link_libraries(aux_os PUBLIC aux-includes xrt-pthreads)

# Only uses normal Windows libraries, doesn't add anything extra.
if(WIN32)
	target_link_libraries(aux_os PRIVATE winmm)
endif()

####
# BLE library
#

add_library(aux_os_ble STATIC os_ble.h)
target_link_libraries(aux_os_ble PUBLIC aux-includes aux_os)

if(XRT_HAVE_DBUS)
	target_sources(aux_os_ble PRIVATE os_ble_dbus.c)
	target_link_libraries(aux_os_ble PRIVATE ${DBUS_LIBRARIES})
	target_include_directories(aux_os_ble SYSTEM PRIVATE ${DBUS_INCLUDE_DIRS})
else()
	target_sources(aux_os_ble PRIVATE os_ble_stubs.c)
endif()
