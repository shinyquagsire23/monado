# Copyright 2019-2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

######
# Create a cli interface for Monado.


set(SOURCE_FILES
	cli_cmd_calibrate.c
	cli_cmd_probe.c
	cli_cmd_test.c
	cli_common.h
	cli_main.c
	cli_prober.c
	)

add_executable(cli
	${SOURCE_FILES}
	)

set_target_properties(cli PROPERTIES
	OUTPUT_NAME monado-cli
	PREFIX ""
	)

target_link_libraries(cli PRIVATE
	aux_os
	aux_util
	aux_math
	st_prober
	target_lists
	)

