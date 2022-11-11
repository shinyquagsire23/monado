# Copyright 2020-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

add_executable(monado-wivrn main.cpp target_instance_wivrn.cpp)
add_sanitizers(monado-wivrn)

target_link_libraries(
	monado-wivrn
	PRIVATE
		aux_util
		ipc_server
		drv_wivrn
		comp_main
	)
