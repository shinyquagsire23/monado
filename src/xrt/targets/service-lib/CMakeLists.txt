# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


add_library(monado-service MODULE
	service_target.cpp
	)

target_link_libraries(monado-service PRIVATE
	aux_util
	st_prober
	ipc_server
	comp_main
	target_lists
	target_instance
	xrt-external-jni-wrap
	)

set_target_properties(monado-service
	PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON)
