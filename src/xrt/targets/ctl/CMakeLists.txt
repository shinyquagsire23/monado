# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


add_executable(monado-ctl
	main.c
	)

target_include_directories(monado-ctl PRIVATE
	ipc
	)

target_link_libraries(monado-ctl PRIVATE
	aux_util
	ipc_client
	)

install(TARGETS monado-ctl
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	)

