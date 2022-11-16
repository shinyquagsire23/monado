# Copyright 2022, Guillaume Meunier
# Copyright 2022, Patrick Nicolas
# SPDX-License-Identifier: BSL-1.0

add_executable(monado-wivrn main.cpp target_instance_wivrn.cpp hostname.cpp avahi_publisher.cpp)

add_sanitizers(monado-wivrn)

target_link_libraries(
	monado-wivrn
	PRIVATE
		aux_util
		ipc_server
		drv_wivrn
		comp_main
		PkgConfig::AVAHI
		systemd
	)
