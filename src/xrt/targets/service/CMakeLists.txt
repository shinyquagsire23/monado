# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


add_executable(monado-service
	main.c
	)

target_link_libraries(monado-service PRIVATE
	aux_util
	st_prober
	ipc_server
	comp_main
	target_lists
	target_instance
	)

install(TARGETS monado-service
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)
