# Copyright 2019-2022, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_library(
	aux_os STATIC
	os_ble.h
	os_documentation.h
	os_hid.h
	os_hid_hidraw.c
	os_threading.h
	)
target_link_libraries(aux_os PUBLIC aux-includes xrt-pthreads)

if(XRT_HAVE_DBUS)
	target_sources(aux_os PRIVATE os_ble_dbus.c)
	target_link_libraries(aux_os PRIVATE ${DBUS_LIBRARIES})
	target_include_directories(aux_os SYSTEM PRIVATE ${DBUS_INCLUDE_DIRS})
endif()
